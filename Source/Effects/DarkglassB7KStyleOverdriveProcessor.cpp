#include "DarkglassB7KStyleOverdriveProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5;

    // 1N914 / 1N4148-class silicon (docs/circuits/GainAudit.md).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    // TL072 on 9 V and a 4049 CMOS inverter biased at mid-rail (near rail-to-rail swing).
    const NodalCircuit::OpAmpMacro tl072 { 2.0e5, 3.0e6, 75.0, 1.5, 7.5, 0.0, 0.0 };
    const NodalCircuit::OpAmpMacro cmos  { 1.0e4, 0.7e6, 1000.0, 0.4, 8.6, 0.0, 0.0 };

    constexpr double inputLoad = 1.0e6;
}

DarkglassB7KStyleOverdriveProcessor::DarkglassB7KStyleOverdriveProcessor()
{
    auto blend = std::make_unique<juce::AudioParameterFloat> (
        "b7k_blend", "Blend", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto level = std::make_unique<juce::AudioParameterFloat> (
        "b7k_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto drive = std::make_unique<juce::AudioParameterFloat> (
        "b7k_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    blendParam = blend.get();
    levelParam = level.get();
    driveParam = drive.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "b7k", "Darkglass B7K-Style Overdrive", "|", std::move (blend));
    group->addChild (std::move (level));
    group->addChild (std::move (drive));

    const char* bandIds[numBands] = { "b7k_low", "b7k_lo_mid", "b7k_hi_mid", "b7k_treble" };
    const char* bandNames[numBands] = { "Low", "Lo Mid", "Hi Mid", "Treble" };
    for (int i = 0; i < numBands; ++i)
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (
            bandIds[i], bandNames[i], juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
        bandParam[i] = p.get();
        group->addChild (std::move (p));
    }

    auto grunt = std::make_unique<juce::AudioParameterFloat> (
        "b7k_grunt", "Grunt", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f);
    auto attack = std::make_unique<juce::AudioParameterFloat> (
        "b7k_attack", "Attack", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f);
    gruntParam = grunt.get();
    attackParam = attack.get();
    group->addChild (std::move (grunt));
    group->addChild (std::move (attack));

    parameters = std::move (group);
}

void DarkglassB7KStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ============================================================ block A: buffer, Grunt/Attack shaping, drive
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), nIn = c.addNode(), a1 = c.addNode(), plus = c.addNode(), buf = c.addNode();
        const auto g1 = c.addNode(), drvIn = c.addNode(), sh = c.addNode(), minus = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (nIn, 0.0);

        // input: R 10k + C 22n into the buffer's (+), 470k to the bias (~500 kOhm input Z)
        c.addResistor (nIn, a1, 10.0e3);
        c.addCapacitor (a1, plus, 22.0e-9);
        c.addResistor (plus, nb, 470.0e3);
        c.addOpAmp (plus, buf, buf);           // follower
        ch.nBuf = buf;

        // Grunt: series-RC shunt (1 uF then the switched resistor) bleeds highs pre-clip = more low end
        c.addCapacitor (buf, g1, 1.0e-6);
        ch.rGrunt = c.addResistor (g1, nb, 1.0e9);

        // Attack: handled series R into drvIn, a bright cap across it, a shunt cap to the bias
        ch.rAttack = c.addResistor (buf, drvIn, 100.0);
        ch.cAttack = c.addCapacitor (buf, drvIn, 1.0e-12);
        ch.cShunt = c.addCapacitor (drvIn, sh, 1.0e-12);
        c.addResistor (sh, nb, 4.7e3);
        c.addResistor (drvIn, nb, 100.0e3);

        // drive stage: inverting, gain -(rFb/4.7k) ~= -4.7..-47x on the Drive pot. Inverting so the
        // wet path has an even number of sign flips (drive x 4049) and re-combines with the dry at the
        // blend in phase, like the real pedal.
        c.addOpAmpMacro (nb, minus, out, tl072);
        c.addResistor (drvIn, minus, 4.7e3);
        ch.rFb = c.addResistor (minus, out, 22.0e3 + 100.0e3);
        ch.nDrive = out;

        for (auto n : { plus, buf, g1, drvIn, sh, minus, out })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block B: CMOS clipper + diode shunt
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), minus = c.addNode(), out = c.addNode();
        const auto cn = c.addNode(), clip = c.addNode();
        c.addSource (nb, vBias);
        ch.srcB = c.addSource (src, vBias);

        // 4049 inverter as an inverting amp: R 10k in, R 220k feedback -> gain -22 into near rail-to-rail rails
        c.addResistor (src, minus, 10.0e3);
        c.addOpAmpMacro (nb, minus, out, cmos);
        c.addResistor (minus, out, 220.0e3);

        // recovery coupling + the anti-parallel 1N4148 shunt to the bias (the clone's D1/D2 to GND via R15)
        c.addCapacitor (out, cn, 1.0e-6);
        c.addResistor (cn, nb, 47.0e3);
        c.addResistor (cn, clip, 10.0e3);
        c.addDiode (clip, nb, siIs, siNVt, 4.0e-9);
        c.addDiode (nb, clip, siIs, siNVt, 4.0e-9);
        c.addResistor (clip, nb, 100.0e3);
        ch.nClip = clip;

        for (auto n : { minus, out, cn, clip })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block C: blend + 4-band EQ (GE7-style summing)
    {
        auto& c = ch.eq;
        const auto nb = c.addNode(), srcDry = c.addNode(), srcWet = c.addNode();
        const auto pNode = c.addNode(), nNode = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcDry = c.addSource (srcDry, vBias);
        ch.srcWet = c.addSource (srcWet, vBias);

        // Blend pot: each source through half a 47k track into the summing (+) node
        ch.rDryIn = c.addResistor (srcDry, pNode, 23.5e3);
        ch.rWetIn = c.addResistor (srcWet, pNode, 23.5e3);
        c.addResistor (pNode, nb, 100.0e3);

        c.addOpAmpMacro (pNode, nNode, out, tl072);
        c.addResistor (nNode, out, 3.3e3);

        constexpr double sliderOhms = 10.0e3, rLoss = 330.0;
        for (int k = 0; k < numBands; ++k)
        {
            const auto wiper = c.addNode();
            ch.rUp[k] = c.addResistor (wiper, pNode, sliderOhms * 0.5);
            ch.rDown[k] = c.addResistor (wiper, nNode, sliderOhms * 0.5);

            if (k == 3)
            {
                // Treble shelf @ ~5 kHz: plain series RC (the GE7 top-band trick, retuned).
                const auto m = c.addNode();
                c.addCapacitor (wiper, m, 39.0e-9);
                c.addResistor (m, nb, 820.0);
                c.setInitialGuess (m, vBias);
            }
            else
            {
                // Bass + peaking mids: series-C + gyrator resonators (GE7 topology). The bass band is a
                // broad ~90 Hz resonator (the 100 Hz bass band's quasi-shelf), the mids peak at ~1 kHz
                // and ~1.2 kHz (the real pedal's 1 kHz / 3.3 kHz centres, constrained by this network's
                // resonance-vs-LP-corner range -- see docs/circuits/DarkglassB7KStyleOverdrive.md).
                const double cs    = k == 0 ? 4.7e-6  : k == 1 ? 0.1e-6  : 0.039e-6;
                const double cGyr  = k == 0 ? 42.9e-9 : k == 1 ? 8.2e-9  : 2.2e-9;
                const double rGyr  = k == 0 ? 47.0e3  : k == 1 ? 100.0e3 : 82.0e3;
                const auto x = c.addNode(), y = c.addNode();
                c.addCapacitor (wiper, x, cs);
                c.addResistor (x, y, rLoss);
                c.addResistor (y, nb, rGyr);
                c.addCoupledInductors ({ { y, nb } }, { cGyr * rGyr * rLoss });
                c.setInitialGuess (x, vBias);
                c.setInitialGuess (y, vBias);
            }
            c.setInitialGuess (wiper, vBias);
        }

        ch.nEq = out;
        for (auto n : { pNode, nNode, out })
            c.setInitialGuess (n, vBias);
    }

    // ============================================================ block D: coupling + Level divider
    {
        auto& c = ch.d;
        const auto nb = c.addNode(), src = c.addNode(), volTop = c.addNode(), volW = c.addNode();
        c.addSource (nb, vBias);
        ch.srcD = c.addSource (src, vBias);

        c.addCapacitor (src, volTop, 10.0e-6);
        ch.rVolTop = c.addResistor (volTop, volW, 25.0e3);
        ch.rVolBottom = c.addResistor (volW, gnd, 25.0e3);
        c.addResistor (volW, gnd, 100.0e3);
        c.addResistor (volW, gnd, inputLoad);
        ch.nOut = volW;
        c.setInitialGuess (volTop, 0.0);
    }
}

void DarkglassB7KStyleOverdriveProcessor::updatePots()
{
    const auto clampFraction = [] (double x) { return juce::jlimit (0.001, 0.999, x); };
    const double blend = clampFraction (blendParam->get());
    const double drive = clampFraction (driveParam->get());
    const double level = clampFraction (levelParam->get());
    const int grunt = juce::roundToInt (gruntParam->get());
    const int attack = juce::roundToInt (attackParam->get());

    // Attack: Boost = bright cap across a 10k series; Flat = plain wire; Cut = series R + shunt C.
    const double rAtt = attack == 0 ? 4.7e3 : (attack == 2 ? 10.0e3 : 100.0);
    const double cAtt = attack == 2 ? 22.0e-9 : 1.0e-12;
    const double cSh  = attack == 0 ? 3.3e-9 : 1.0e-12;
    // Grunt: the switched shunt resistor after the 1 uF (off / mild / raw).
    const double rGr = grunt == 0 ? 1.0e9 : (grunt == 2 ? 4.7e3 : 22.0e3);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rFb, 22.0e3 + drive * 200.0e3);
        ch.a.setResistance (ch.rGrunt, rGr);
        ch.a.setResistance (ch.rAttack, rAtt);
        ch.a.setCapacitance (ch.cAttack, cAtt);
        ch.a.setCapacitance (ch.cShunt, cSh);

        ch.eq.setResistance (ch.rDryIn, 470.0 + blend * 47.0e3);
        ch.eq.setResistance (ch.rWetIn, 470.0 + (1.0 - blend) * 47.0e3);
        for (int k = 0; k < numBands; ++k)
        {
            const double u = clampFraction (bandParam[k]->get());
            ch.eq.setResistance (ch.rUp[k], juce::jmax (10.0, 10.0e3 * u));
            ch.eq.setResistance (ch.rDown[k], juce::jmax (10.0, 10.0e3 * (1.0 - u)));
        }
        ch.d.setResistance (ch.rVolTop, 50.0e3 * (1.0 - level));
        ch.d.setResistance (ch.rVolBottom, 50.0e3 * level);
    }
    appliedGrunt = grunt;
    appliedAttack = attack;
}

void DarkglassB7KStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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
        ch.b.setSource (ch.srcB, ch.a.voltage (ch.nDrive));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
        ch.eq.setSource (ch.srcDry, ch.a.voltage (ch.nBuf));
        ch.eq.setSource (ch.srcWet, ch.b.voltage (ch.nClip));
        dcOk = ch.eq.prepare (newSampleRate) && dcOk;
        ch.d.setSource (ch.srcD, ch.eq.voltage (ch.nEq));
        dcOk = ch.d.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void DarkglassB7KStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
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
            ch.b.setSource (ch.srcB, ch.a.voltage (ch.nDrive));
            ok = ch.b.solveSample() && ok;
            ch.eq.setSource (ch.srcDry, ch.a.voltage (ch.nBuf));
            ch.eq.setSource (ch.srcWet, ch.b.voltage (ch.nClip));
            ok = ch.eq.solveSample() && ok;
            ch.d.setSource (ch.srcD, ch.eq.voltage (ch.nEq));
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

void DarkglassB7KStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
