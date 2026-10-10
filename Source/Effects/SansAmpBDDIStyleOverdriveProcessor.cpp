#include "SansAmpBDDIStyleOverdriveProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;

    // TLC2264-class op-amp on 9 V (RRIO: ~1.5 V short of each rail); see the doc.
    const NodalCircuit::OpAmpMacro tlc2264 { 2.0e5, 0.7e6, 150.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double inputLoad = 1.0e6;
}

SansAmpBDDIStyleOverdriveProcessor::SansAmpBDDIStyleOverdriveProcessor()
{
    auto presence = std::make_unique<juce::AudioParameterFloat> (
        "bddi_presence", "Presence", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto drive = std::make_unique<juce::AudioParameterFloat> (
        "bddi_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto blend = std::make_unique<juce::AudioParameterFloat> (
        "bddi_blend", "Blend", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "bddi_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "bddi_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto level = std::make_unique<juce::AudioParameterFloat> (
        "bddi_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    presenceParam = presence.get();
    driveParam = drive.get();
    blendParam = blend.get();
    bassParam = bass.get();
    trebleParam = treble.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "bddi", "SansAmp BDDI-Style Overdrive", "|", std::move (presence));
    group->addChild (std::move (drive));
    group->addChild (std::move (blend));
    group->addChild (std::move (bass));
    group->addChild (std::move (treble));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void SansAmpBDDIStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ============================================================ block A: buffer + 750 Hz notch + 72 Hz HPF
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), nIn = c.addNode(), a1 = c.addNode(), plus = c.addNode(), buf = c.addNode();
        const auto t1 = c.addNode(), t2 = c.addNode(), nt = c.addNode(), hpf = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (nIn, 0.0);

        c.addResistor (nIn, a1, 10.0e3);
        c.addCapacitor (a1, plus, 1.0e-6);
        c.addResistor (plus, nb, 470.0e3);
        c.addOpAmp (plus, buf, buf);           // follower

        // Bridged-T (twin-T) notch @ ~750 Hz: R 21.2k / C 10 nF pair
        c.addResistor (buf, t1, 21.2e3);
        c.addResistor (t1, nt, 21.2e3);
        c.addCapacitor (buf, t2, 10.0e-9);
        c.addCapacitor (t2, nt, 10.0e-9);
        c.addCapacitor (t1, nb, 20.0e-9);
        c.addResistor (t2, nb, 10.6e3);

        // First-order HPF @ ~72 Hz (the cab's low-end roll-off): 2.2 uF into 1k
        c.addCapacitor (nt, hpf, 2.2e-6);
        c.addResistor (hpf, nb, 1.0e3);
        ch.nNotch = hpf;
        ch.nBuf = buf;

        for (auto n : { plus, buf, t1, t2, nt, hpf })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block B: Presence gain stage
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), plus = c.addNode(), minus = c.addNode(), out = c.addNode();
        const auto wiper = c.addNode(), wr = c.addNode();
        c.addSource (nb, vBias);
        ch.srcB = c.addSource (src, vBias);

        c.addResistor (src, plus, 10.0e3);
        c.addOpAmpMacro (plus, minus, out, tlc2264);
        // feedback 10k with C 68n -> LP pole ~ 2.2 kHz; presence pot straddles (+)/(-): wiper -> 1k -> C -> bias,
        // moving both gain and the high-pass knee together (see the doc for the measured law this mirrors).
        c.addResistor (minus, out, 10.0e3);
        c.addCapacitor (minus, out, 6.8e-9);   // ~2.3 kHz LP over the 10k feedback R
        ch.rPresUp = c.addResistor (plus, wiper, 5.0e3);
        ch.rPresDown = c.addResistor (wiper, minus, 5.0e3);
        c.addResistor (wiper, wr, 1.0e3);
        c.addCapacitor (wr, nb, 100.0e-9);
        ch.nPres = out;

        for (auto n : { plus, minus, out, wiper, wr })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block C: Drive stage (op-amp clip)
    {
        auto& c = ch.c;
        const auto nb = c.addNode(), src = c.addNode(), plus = c.addNode(), minus = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcC = c.addSource (src, vBias);

        c.addResistor (src, plus, 10.0e3);
        c.addResistor (plus, nb, 100.0e3);
        c.addOpAmpMacro (plus, minus, out, tlc2264);
        c.addResistor (minus, nb, 1.0e3);
        ch.rFb = c.addResistor (minus, out, 4.7e3 + 47.5e3);
        c.addCapacitor (minus, out, 680.0e-12); // ~2.2 kHz LP across max feedback R, opens up at low drive
        ch.nDrive = out;

        for (auto n : { plus, minus, out })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block D: cabinet-sim filters
    {
        auto& c = ch.d;
        const auto nb = c.addNode(), src = c.addNode();
        const auto t1 = c.addNode(), t2 = c.addNode(), nt = c.addNode();
        const auto c1 = c.addNode(), c2 = c.addNode(), plus = c.addNode(), buf = c.addNode();
        c.addSource (nb, vBias);
        ch.srcD = c.addSource (src, vBias);

        // Bridged-T notch @ ~450 Hz: R 35.4k / C 10 nF
        c.addResistor (src, t1, 35.4e3);
        c.addResistor (t1, nt, 35.4e3);
        c.addCapacitor (src, t2, 10.0e-9);
        c.addCapacitor (t2, nt, 10.0e-9);
        c.addCapacitor (t1, nb, 20.0e-9);
        c.addResistor (t2, nb, 17.7e3);

        // two cascaded RC poles ~5 kHz approximating the 24 dB/oct cab LP (simplification, see the doc)
        c.addResistor (nt, c1, 10.0e3);
        c.addCapacitor (c1, nb, 6.8e-9);
        c.addResistor (c1, c2, 10.0e3);
        c.addCapacitor (c2, nb, 6.8e-9);

        // follower buffer (the TLC2264's last section)
        c.addResistor (c2, plus, 10.0e3);
        c.addResistor (plus, nb, 100.0e3);
        c.addOpAmp (plus, buf, buf);
        c.addResistor (buf, nb, 10.0e3);
        ch.nCab = buf;

        for (auto n : { t1, t2, nt, c1, c2, plus, buf })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block E: Blend + Baxandall + Level
    {
        auto& c = ch.e;
        const auto nb = c.addNode(), srcDry = c.addNode(), srcWet = c.addNode();
        const auto pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        const auto volTop = c.addNode(), volW = c.addNode();
        c.addSource (nb, vBias);
        ch.srcDry = c.addSource (srcDry, vBias);
        ch.srcWet = c.addSource (srcWet, vBias);

        ch.rDryIn = c.addResistor (srcDry, pNode, 23.5e3);
        ch.rWetIn = c.addResistor (srcWet, pNode, 23.5e3);
        c.addResistor (pNode, nb, 100.0e3);
        c.addOpAmpMacro (pNode, nNode, out, tlc2264);
        c.addResistor (nNode, out, 3.3e3);

        constexpr double sliderOhms = 10.0e3, rLoss = 330.0;
        {
            // Bass shelf ~60 Hz: gyrator-inductor branch (same trick as the B7K low band).
            const auto wiper = c.addNode(), y = c.addNode();
            ch.rBassUp = c.addResistor (wiper, pNode, sliderOhms * 0.5);
            ch.rBassDown = c.addResistor (wiper, nNode, sliderOhms * 0.5);
            c.addResistor (wiper, y, rLoss);
            c.addResistor (y, nb, 47.0e3);
            c.addCoupledInductors ({ { y, nb } }, { 17.0e-9 * 47.0e3 * rLoss });  // pole ~200 Hz: broad bass shelf
            c.setInitialGuess (wiper, vBias);
            c.setInitialGuess (y, vBias);
        }
        {
            // Treble shelf ~4 kHz: series RC (with the 10k boost limiter folded into the branch R).
            const auto wiper = c.addNode(), m = c.addNode();
            ch.rTrebleUp = c.addResistor (wiper, pNode, sliderOhms * 0.5);
            ch.rTrebleDown = c.addResistor (wiper, nNode, sliderOhms * 0.5);
            c.addCapacitor (wiper, m, 6.8e-9);
            c.addResistor (m, nb, 4.7e3);
            c.setInitialGuess (wiper, vBias);
            c.setInitialGuess (m, vBias);
        }

        // Level divider after a coupling cap
        c.addCapacitor (out, volTop, 4.7e-6);
        ch.rVolTop = c.addResistor (volTop, volW, 25.0e3);
        ch.rVolBottom = c.addResistor (volW, gnd, 25.0e3);
        c.addResistor (volW, gnd, inputLoad);
        ch.nOut = volW;

        for (auto n : { pNode, nNode, out })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (volTop, 0.0);
    }
}

void SansAmpBDDIStyleOverdriveProcessor::updatePots()
{
    const auto clampFraction = [] (double x) { return juce::jlimit (0.001, 0.999, x); };
    const double pres = clampFraction (presenceParam->get());
    const double drive = clampFraction (driveParam->get());
    const double blend = clampFraction (blendParam->get());
    const double bass = clampFraction (bassParam->get());
    const double treble = clampFraction (trebleParam->get());
    const double level = clampFraction (levelParam->get());

    for (auto& ch : channels)
    {
        // presence: wiper toward (-) raises gain and the knee (GE7 Level-stage mapping)
        ch.b.setResistance (ch.rPresUp, juce::jmax (10.0, 10.0e3 * (1.0 - pres)));
        ch.b.setResistance (ch.rPresDown, juce::jmax (10.0, 10.0e3 * pres));
        ch.c.setResistance (ch.rFb, 4.7e3 + drive * 95.3e3);
        ch.e.setResistance (ch.rDryIn, 470.0 + blend * 47.0e3);
        ch.e.setResistance (ch.rWetIn, 470.0 + (1.0 - blend) * 47.0e3);
        ch.e.setResistance (ch.rBassUp, juce::jmax (10.0, 10.0e3 * bass));
        ch.e.setResistance (ch.rBassDown, juce::jmax (10.0, 10.0e3 * (1.0 - bass)));
        ch.e.setResistance (ch.rTrebleUp, juce::jmax (10.0, 10.0e3 * treble));
        ch.e.setResistance (ch.rTrebleDown, juce::jmax (10.0, 10.0e3 * (1.0 - treble)));
        ch.e.setResistance (ch.rVolTop, 50.0e3 * (1.0 - level));
        ch.e.setResistance (ch.rVolBottom, 50.0e3 * level);
    }
}

void SansAmpBDDIStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    updatePots();

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcB, ch.a.voltage (ch.nNotch));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
        ch.c.setSource (ch.srcC, ch.b.voltage (ch.nPres));
        dcOk = ch.c.prepare (newSampleRate) && dcOk;
        ch.d.setSource (ch.srcD, ch.c.voltage (ch.nDrive));
        dcOk = ch.d.prepare (newSampleRate) && dcOk;
        ch.e.setSource (ch.srcDry, ch.a.voltage (ch.nBuf));
        ch.e.setSource (ch.srcWet, ch.d.voltage (ch.nCab));
        dcOk = ch.e.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void SansAmpBDDIStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
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
            updatePots();
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();
            ch.b.setSource (ch.srcB, ch.a.voltage (ch.nNotch));
            ok = ch.b.solveSample() && ok;
            ch.c.setSource (ch.srcC, ch.b.voltage (ch.nPres));
            ok = ch.c.solveSample() && ok;
            ch.d.setSource (ch.srcD, ch.c.voltage (ch.nDrive));
            ok = ch.d.solveSample() && ok;
            ch.e.setSource (ch.srcDry, ch.a.voltage (ch.nBuf));
            ch.e.setSource (ch.srcWet, ch.d.voltage (ch.nCab));
            ok = ch.e.solveSample() && ok;

            data[i] = (float) ch.e.voltage (ch.nOut);

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

void SansAmpBDDIStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
