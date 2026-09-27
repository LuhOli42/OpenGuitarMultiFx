#include "GE7StyleEqualizerProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;          // R20/R23 33K/33K + 47 uF: taken as ideal
    constexpr double sliderOhms = 10.0e3;  // "10KG x 8": taken as linear (a G taper is not documented anywhere I found)
    constexpr double sliderMinOhms = 10.0; // the wiper's end resistance
    constexpr double jfetSwitchOhms = 200.0; // Q4, the effect-mode series switch, conducting
    constexpr double downstreamLoad = 1.0e6;

    // HA1457W / uRC4558-class dual op-amp on 9 V: 100 dB, 3 MHz, ~75 ohm out, swing ~2 V short of each rail.
    const NodalCircuit::OpAmpMacro opAmp { 1.0e5, 3.0e6, 75.0, 2.0, 7.0, 0.0, 0.0 };

    /** One band's gyrator branch: wiper -> C_series -> P; P -> C_gyr -> Q; Q -> R_gyr -> bias; a follower Q -> F; F -> R_loss -> P.
        Its impedance is 1/(sC_series) + R_loss (1 + s C_gyr R_gyr) / (1 + s C_gyr R_loss), an inductor L = C_gyr R_gyr R_loss with
        R_loss in series, up to a corner at 1 / (2 pi C_gyr R_loss); the resonance is at 1 / (2 pi sqrt (L C_series)). */
    struct Gyrator { double cSeries, cGyr, rGyr, rLoss; };
    constexpr Gyrator gyrators[GE7StyleEqualizerProcessor::numBands - 1] = {
        { 1.5e-6, 0.056e-6, 100.0e3, 330.0 },   // 100 Hz  (C10, C13, R16, R17)
        { 0.68e-6, 0.033e-6, 82.0e3, 330.0 },   // 200 Hz  (C9,  C8,  R9,  R8)
        { 0.33e-6, 0.015e-6, 100.0e3, 330.0 },  // 400 Hz  (C7,  C14, R18, R19)
        { 0.15e-6, 0.0082e-6, 100.0e3, 330.0 }, // 800 Hz  (C5,  C6,  R7,  R6)
        { 0.1e-6, 0.0039e-6, 82.0e3, 330.0 },   // 1.6 kHz (C4,  C15, R21, R22)
        { 0.039e-6, 0.0022e-6, 82.0e3, 330.0 }, // 3.2 kHz (C2,  C3,  R5,  R4)
    };
    // 6.4 kHz: no gyrator, just C1 0.047 uF + R3 820 ohm to the bias.

    double sliderPosition (float nominalDb) noexcept { return juce::jlimit (0.0, 1.0, 0.5 + (double) nominalDb / 30.0); }
}

GE7StyleEqualizerProcessor::GE7StyleEqualizerProcessor()
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

    const char* ids[numBands] = { "ge7_100", "ge7_200", "ge7_400", "ge7_800", "ge7_1600", "ge7_3200", "ge7_6400" };
    const char* names[numBands] = { "100 Hz", "200 Hz", "400 Hz", "800 Hz", "1.6 kHz", "3.2 kHz", "6.4 kHz" };

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("ge7", "GE-7-Style Equalizer", "|");
    for (int i = 0; i < numBands; ++i)
    {
        auto p = makeBand (ids[i], names[i]);
        bands[(size_t) i] = p.get();
        group->addChild (std::move (p));
    }
    auto levelParam = makeBand ("ge7_level", "Level");
    level = levelParam.get();
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void GE7StyleEqualizerProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the input stage (IC5)
    // R30 10K, C22 47 nF into (+) (R29 470K to the bias). (-) leg: R28 470 + C20 15 nF to the bias; feedback R31 4.7K:
    // gain 1 + R31 / (R28 + 1 / sC20): x1 at low frequencies rising to x11 (a zero at 2.3 kHz, a pole at 23 kHz).
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

    // ================================================================ block B: the Level stage (IC4a)
    // R27 10K into (+); feedback R26 10K. The slider (10K) joins (+) and (-); its wiper goes through R2 2.2K and 10 uF to the bias:
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

    // ================================================================ block C: R24 / IC4b with the seven bands
    {
        auto& c = ch.c;
        const auto nb = c.addNode(), src = c.addNode(), nOutB = c.addNode(), pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcC = c.addSource (src, vBias);

        // R24 3.3K into IC4b's (+) node P; feedback R25 3.3K on its (-) node N
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
                // The gyrator is an inductor L = C_gyr R_gyr R_loss in parallel with R_gyr, in series with R_loss (worked out from
                // its op-amp follower in docs/circuits/GE7StyleEqualizer.md): fewer nodes than the follower, and it fits one block.
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
                c.addCapacitor (wiper, m, 0.047e-6);
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

        // C19 1 uF, Q4 (a JFET switch, 200 ohm), R33 4.7K into 15 nF + 470 ohm to the bias (the de-emphasis), C26 47 nF, the base of Q1
        // (R34 1M to the bias), the emitter follower (drop 0.6 V, R15 10K), R14 1K, C11 1 uF, R12 100K
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

void GE7StyleEqualizerProcessor::updateSliders()
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

    // A slider at position u (0 .. 1, 0.5 = flat): boost is towards the (-) node, so the wiper-to-(-) segment shrinks as u rises.
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

void GE7StyleEqualizerProcessor::prepare (double newSampleRate, int, int)
{
    // Same-rate re-prepare is a no-op (see the Centaur/DS-1 processors for why).
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

void GE7StyleEqualizerProcessor::process (juce::AudioBuffer<float>& buffer)
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

void GE7StyleEqualizerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No equaliser glyph on the icon sheet yet: the pulse trace (the sheet's Compressor / Expander glyph) stands in until one is added.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
