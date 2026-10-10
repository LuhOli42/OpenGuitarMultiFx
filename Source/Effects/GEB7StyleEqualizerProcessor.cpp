#include "GEB7StyleEqualizerProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;          // same 9 V single-supply + 4.5 V bias topology as the GE-7
    constexpr double sliderOhms = 10.0e3;  // same slider rating as the GE-7 (linear taper assumed, as there)
    constexpr double sliderMinOhms = 10.0; // the wiper's end resistance
    constexpr double jfetSwitchOhms = 200.0; // the effect-mode series JFET switch, conducting
    constexpr double downstreamLoad = 1.0e6;

    // Same HA1457W / uRC4558-class dual op-amp on 9 V as the GE-7: 100 dB, 3 MHz, ~75 ohm out, ~2 V of headroom.
    const NodalCircuit::OpAmpMacro opAmp { 1.0e5, 3.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

    /** One band's gyrator branch, the same element as in the GE-7 model (see docs/circuits/GEB7StyleEqualizer.md):
        an inductor L = C_gyr R_gyr R_loss in series with R_loss, resonating with C_series at the band centre.
        The published GEB-7 centres (50 / 120 / 400 / 500 / 800 Hz, 4.5 / 10 kHz) are reached with these values;
        the real pedal's BOM is not published, so these are tuned to the documented frequencies on the same topology. */
    struct Gyrator { double cSeries, cGyr, rGyr, rLoss; };
    constexpr Gyrator gyrators[GEB7StyleEqualizerProcessor::numBands - 1] = {
        { 3.3e-6, 0.1e-6, 100.0e3, 300.0 },     // 50 Hz
        { 0.68e-6, 0.082e-6, 100.0e3, 315.0 },  // 120 Hz
        { 0.33e-6, 0.015e-6, 100.0e3, 330.0 },  // 400 Hz (GE-7's 400 Hz values)
        { 0.22e-6, 0.015e-6, 100.0e3, 310.0 },  // 500 Hz
        { 0.15e-6, 0.0082e-6, 100.0e3, 330.0 }, // 800 Hz (GE-7's 800 Hz values)
        { 0.022e-6, 0.0022e-6, 82.0e3, 315.0 }, // 4.5 kHz
    };
    // 10 kHz: no gyrator, just a series C + R to the bias (the same shelving character as the GE-7's top band,
    // tuned a little higher: 0.027 uF + 820 ohm -> corner ~ 7.2 kHz and it keeps rising into the top octave).

    double sliderPosition (float nominalDb) noexcept { return juce::jlimit (0.0, 1.0, 0.5 + (double) nominalDb / 30.0); }
}

GEB7StyleEqualizerProcessor::GEB7StyleEqualizerProcessor()
{
    auto makeBand = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
            }));
    };

    const char* ids[numBands] = { "geb7_50", "geb7_120", "geb7_400", "geb7_500", "geb7_800", "geb7_4500", "geb7_10000" };
    const char* names[numBands] = { "50 Hz", "120 Hz", "400 Hz", "500 Hz", "800 Hz", "4.5 kHz", "10 kHz" };

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("geb7", "GEB-7-Style Equalizer", "|");
    for (int i = 0; i < numBands; ++i)
    {
        auto p = makeBand (ids[i], names[i]);
        bands[(size_t) i] = p.get();
        group->addChild (std::move (p));
    }
    auto levelParam = makeBand ("geb7_level", "Level");
    level = levelParam.get();
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void GEB7StyleEqualizerProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the input stage
    // Same network as the GE-7 model: non-inverting with R 470 + C 15 nF to the bias in the (-) leg: gain x1 at low
    // frequencies rising to x11 (a zero at 2.3 kHz, a pole at 23 kHz).
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode(), n30 = c.addNode(), plus = c.addNode(), minus = c.addNode();
        const auto n28 = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        c.addResistor (in, n30, 10.0e3);
        c.addCapacitor (n30, plus, 47.0e-9);
        c.addResistor (plus, nb, 470.0e3);
        c.addOpAmpMacro (plus, minus, out, opAmp);
        c.addResistor (minus, n28, 470.0);
        c.addCapacitor (n28, nb, 15.0e-9);
        c.addResistor (minus, out, 4.7e3);

        ch.nA = out;
        for (auto n : { n30, plus, minus, n28, out })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (n30, 0.0);
    }

    // ================================================================ block B: the Level stage
    // 10K into (+); feedback 10K. The slider joins (+) and (-); its wiper goes through 2.2K and 10 uF to the bias:
    // towards (-) it makes the gain 1 + 10K / 2.2K (+15 dB), towards (+) it divides the input by 2.2K / 12.2K (-15 dB).
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), nOutA = c.addNode(), plus = c.addNode(), minus = c.addNode(), out = c.addNode();
        const auto wiper = c.addNode(), nR2 = c.addNode();
        c.addSource (nb, vBias);
        ch.srcB = c.addSource (src, vBias);

        c.addResistor (src, nOutA, opAmp.outputOhms);
        c.addResistor (nOutA, plus, 10.0e3);
        c.addOpAmpMacro (plus, minus, out, opAmp);
        c.addResistor (minus, out, 10.0e3);
        ch.rLevelUp = c.addResistor (plus, wiper, 5.0e3);
        ch.rLevelDown = c.addResistor (wiper, minus, 5.0e3);
        c.addResistor (wiper, nR2, 2.2e3);
        c.addCapacitor (nR2, nb, 10.0e-6);

        ch.nB = out;
        for (auto n : { nOutA, plus, minus, out, wiper, nR2 })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block C: the equaliser op-amp and its seven bands
    {
        auto& c = ch.c;
        const auto nb = c.addNode(), src = c.addNode(), nOutB = c.addNode(), pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcC = c.addSource (src, vBias);

        c.addResistor (src, nOutB, opAmp.outputOhms);
        c.addResistor (nOutB, pNode, 3.3e3);
        c.addOpAmpMacro (pNode, nNode, out, opAmp);
        c.addResistor (nNode, out, 3.3e3);

        for (size_t k = 0; k < (size_t) numBands; ++k)
        {
            const auto wiper = c.addNode();
            ch.rUp[k] = c.addResistor (wiper, pNode, 5.0e3);
            ch.rDown[k] = c.addResistor (wiper, nNode, 5.0e3);

            if (k + 1 < (size_t) numBands)
            {
                // The gyrator modelled as an inductor L = C_gyr R_gyr R_loss in parallel with R_gyr, in series with
                // R_loss (the same equivalence the GE-7 model uses; derived in docs/circuits/GE7StyleEqualizer.md).
                const auto& g = gyrators[k];
                const auto x = c.addNode(), y = c.addNode();
                c.addCapacitor (wiper, x, g.cSeries);
                c.addResistor (x, y, g.rLoss);
                c.addResistor (y, nb, g.rGyr);
                c.addCoupledInductors ({ { y, nb } }, { g.cGyr * g.rGyr * g.rLoss });
                for (auto n : { x, y })
                    c.setInitialGuess (n, vBias);
            }
            else
            {
                const auto m = c.addNode();
                c.addCapacitor (wiper, m, 0.027e-6);
                c.addResistor (m, nb, 820.0);
                c.setInitialGuess (m, vBias);
            }
            c.setInitialGuess (wiper, vBias);
        }

        ch.nEqOut = out;
        for (auto n : { nOutB, pNode, nNode, out })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block D: the output network
    {
        auto& c = ch.d;
        const auto nb = c.addNode(), src = c.addNode();
        c.addSource (nb, vBias);
        ch.srcD = c.addSource (src, vBias);

        // Coupling cap, the JFET switch (200 ohm), the de-emphasis (15 nF + 470 ohm), the emitter follower
        // (drop 0.6 V), output coupling and the 100K pulldown.
        const auto x1 = c.addNode(), x2 = c.addNode(), x3 = c.addNode(), x3r = c.addNode(), x4 = c.addNode();
        const auto e = c.addNode(), e2 = c.addNode(), o = c.addNode();
        c.addCapacitor (src, x1, 1.0e-6);
        c.addResistor (x1, nb, 1.0e6);
        c.addResistor (x1, x2, jfetSwitchOhms);
        c.addResistor (x2, nb, 1.0e6);
        c.addResistor (x2, x3, 4.7e3);
        c.addCapacitor (x3, x3r, 15.0e-9);
        c.addResistor (x3r, nb, 470.0);
        c.addCapacitor (x3, x4, 47.0e-9);
        c.addResistor (x4, nb, 1.0e6);
        c.addFollower (x4, e, 0.6);
        c.addResistor (e, gnd, 10.0e3);
        c.addResistor (e, e2, 1.0e3);
        c.addCapacitor (e2, o, 1.0e-6);
        c.addResistor (o, gnd, 100.0e3);
        c.addResistor (o, gnd, downstreamLoad);

        ch.nOut = o;
        for (auto n : { x2, x3, x3r, x4 })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (x1, 0.0);
        c.setInitialGuess (e, vBias - 0.6);
        c.setInitialGuess (e2, vBias - 0.6);
        c.setInitialGuess (o, 0.0);
    }
}

void GEB7StyleEqualizerProcessor::updateSliders()
{
    bool changed = false;
    for (int k = 0; k <= numBands; ++k)
    {
        const float v = k < numBands ? bands[(size_t) k]->get() : level->get();
        if (v != appliedSliders[(size_t) k])
        {
            appliedSliders[(size_t) k] = v;
            changed = true;
        }
    }
    if (! changed)
        return;

    for (auto& ch : channels)
    {
        for (size_t k = 0; k < (size_t) numBands; ++k)
        {
            const double u = sliderPosition (appliedSliders[k]);
            ch.c.setResistance (ch.rUp[k], juce::jmax (sliderMinOhms, sliderOhms * u));
            ch.c.setResistance (ch.rDown[k], juce::jmax (sliderMinOhms, sliderOhms * (1.0 - u)));
        }
        const double uL = sliderPosition (appliedSliders[(size_t) numBands]);
        ch.b.setResistance (ch.rLevelUp, juce::jmax (sliderMinOhms, sliderOhms * uL));
        ch.b.setResistance (ch.rLevelDown, juce::jmax (sliderMinOhms, sliderOhms * (1.0 - uL)));
    }
}

void GEB7StyleEqualizerProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    appliedSliders.fill (1.0e9f);
    updateSliders();

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcB, ch.a.voltage (ch.nA));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
        ch.c.setSource (ch.srcC, ch.b.voltage (ch.nB));
        dcOk = ch.c.prepare (newSampleRate) && dcOk;
        ch.d.setSource (ch.srcD, ch.c.voltage (ch.nEqOut));
        dcOk = ch.d.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void GEB7StyleEqualizerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updateSliders();
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();
            ch.b.setSource (ch.srcB, ch.a.voltage (ch.nA));
            ok = ch.b.solveSample() && ok;
            ch.c.setSource (ch.srcC, ch.b.voltage (ch.nB));
            ok = ch.c.solveSample() && ok;
            ch.d.setSource (ch.srcD, ch.c.voltage (ch.nEqOut));
            ok = ch.d.solveSample() && ok;

            data[i] = (float) ch.d.voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void GEB7StyleEqualizerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
