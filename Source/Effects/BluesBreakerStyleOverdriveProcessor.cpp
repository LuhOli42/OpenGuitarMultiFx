#include "BluesBreakerStyleOverdriveProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 9.0;
    constexpr double vBias = 4.5; // the schematic's "4.5 V bias" (how it is made is not drawn: taken as ideal)

    // 1N4148-class silicon (same SPICE parameters as the Tube Screamer / BD-2 models).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    // TL072: gain 200 000, gain-bandwidth 3 MHz, output resistance ~100 ohm, swing ~1.5 V short of each rail.
    const NodalCircuit::OpAmpMacro tl072 { 2.0e5, 3.0e6, 100.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double drivePotMax = 100.0e3; // 100K linear
    constexpr double tonePotMax = 25.0e3;   // 25K log
    constexpr double levelPotMax = 100.0e3; // 100K log
    constexpr double downstreamLoad = 1.0e6;
}

BluesBreakerStyleOverdriveProcessor::BluesBreakerStyleOverdriveProcessor()
{
    auto make = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    };

    auto driveParam = make ("bluesbreaker_drive", "Drive");
    auto toneParam = make ("bluesbreaker_tone", "Tone");
    auto levelParam = make ("bluesbreaker_volume", "Volume");

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "bluesbreaker", "Blues Breaker-Style Overdrive", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void BluesBreakerStyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the gain stage
    // 10 nF -> (+) with 1M to the bias. (-) leg (27K / 33K version): 27K in parallel with (33K + 10 nF), then 10 nF to the
    // bias -- two corners, so the gain rises in two steps. Feedback = the Drive pot from (-) to its wiper (the op-amp
    // output) with 100 pF across it.
    {
        auto& c = ch.a;
        const auto nb = c.addNode(), in = c.addNode(), plus = c.addNode(), minus = c.addNode();
        const auto n40 = c.addNode(), nm = c.addNode(), out = c.addNode();
        c.addSource (nb, vBias);
        ch.srcIn = c.addSource (in, 0.0);

        c.addCapacitor (in, plus, 10.0e-9);
        c.addResistor (plus, nb, 1.0e6);
        c.addOpAmpMacro (plus, minus, out, tl072);
        c.addResistor (minus, n40, 27.0e3);
        c.addResistor (minus, nm, 33.0e3);
        c.addCapacitor (nm, n40, 10.0e-9);
        c.addCapacitor (n40, nb, 10.0e-9);
        c.addCapacitor (minus, out, 47.0e-12);
        ch.rDriveFeedback = c.addResistor (minus, out, 1.0e3);

        ch.nOp1 = out;
        for (auto n : { plus, minus, n40, nm, out })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block B: clipping stage, Tone, Volume
    {
        auto& c = ch.b;
        const auto nb = c.addNode(), src = c.addNode(), nOut1 = c.addNode(), nSer = c.addNode(), nC = c.addNode();
        const auto minus = c.addNode(), out2 = c.addNode(), pf = c.addNode(), m1 = c.addNode(), m2 = c.addNode();
        const auto t0 = c.addNode(), tw = c.addNode(), tb = c.addNode(), l = c.addNode(), lw = c.addNode(), outN = c.addNode();
        c.addSource (nb, vBias);
        ch.srcOp1 = c.addSource (src, vBias);

        // op-amp 1's output resistance, the Drive pot's wiper-to-end segment, 100 nF, 10K into (-) of op-amp 2
        c.addResistor (src, nOut1, tl072.outputOhms);
        ch.rDriveSeries = c.addResistor (nOut1, nSer, 1.0e3);
        c.addCapacitor (nSer, nC, 100.0e-9);
        c.addResistor (nC, minus, 10.0e3);

        // op-amp 2: inverting, (+) at the bias; feedback = 220K in parallel with 6.8K + (2 diodes each way)
        c.addOpAmpMacro (nb, minus, out2, tl072);
        c.addResistor (minus, out2, 220.0e3);
        c.addResistor (minus, pf, 6.8e3);
        c.addDiode (pf, m1, siIs, siNVt, 4.0e-9);
        c.addDiode (m1, out2, siIs, siNVt, 4.0e-9);
        c.addDiode (out2, m2, siIs, siNVt, 4.0e-9);
        c.addDiode (m2, pf, siIs, siNVt, 4.0e-9);

        // 1K -> Tone (25K linear; its bottom end through 10 nF to the bias) -> wiper 6.8K -> 10 nF to the bias -> Volume
        c.addResistor (out2, t0, 1.0e3);
        ch.rToneTop = c.addResistor (t0, tw, 1.0e3);
        ch.rToneBottom = c.addResistor (tw, tb, 1.0e3);
        c.addCapacitor (tb, nb, 10.0e-9);
        c.addResistor (tw, l, 6.8e3);
        c.addCapacitor (l, nb, 10.0e-9);
        ch.rLevelTop = c.addResistor (l, lw, 1.0e3);
        ch.rLevelBottom = c.addResistor (lw, nb, 1.0e3);
        c.addCapacitor (lw, outN, 100.0e-9);
        c.addResistor (outN, gnd, downstreamLoad);

        ch.nOp2 = out2;
        ch.nOut = outN;
        for (auto n : { src, nOut1, nSer, nC, minus, out2, pf, m1, m2, t0, tw, tb, l, lw })
            c.setInitialGuess (n, vBias);
    }
}

void BluesBreakerStyleOverdriveProcessor::updatePots (double driveKnob, double toneKnob, double levelKnob)
{
    // Drive: 100K linear; the wiper is the op-amp's output, so the (-) side is the feedback and the rest is a series
    // resistance into the second stage.
    const double dFb = juce::jmax (1.0, drivePotMax * driveKnob);
    const double dSer = juce::jmax (1.0, drivePotMax - dFb);

    // Tone: 25K linear (GGG/Apollo and Aion BOMs agree), wiper-to-cap-end segment (wiper at the top = bright).
    const double tBottom = juce::jmax (1.0, tonePotMax * toneKnob);
    const double tTop = juce::jmax (1.0, tonePotMax - tBottom);

    // Volume: 100K log, wiper-to-bias segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * pots::audio (levelKnob));
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rDriveFeedback, dFb);
        ch.b.setResistance (ch.rDriveSeries, dSer);
        ch.b.setResistance (ch.rToneTop, tTop);
        ch.b.setResistance (ch.rToneBottom, tBottom);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void BluesBreakerStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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

    smoothedDrive.reset (newSampleRate, 0.02);
    smoothedDrive.setCurrentAndTargetValue (drive->get());
    smoothedTone.reset (newSampleRate, 0.02);
    smoothedTone.setCurrentAndTargetValue (tone->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (drive->get(), tone->get(), level->get());

    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void BluesBreakerStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDrive.setTargetValue (drive->get());
    smoothedTone.setTargetValue (tone->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = smoothedDrive.getNextValue();
        const float t = smoothedTone.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (d, t, l);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b.setSource (ch.srcOp1, ch.a.voltage (ch.nOp1));
            ok = ch.b.solveSample() && ok;

            data[i] = (float) ch.b.voltage (ch.nOut);

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

void BluesBreakerStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same reference-sheet category/entry ("Overdrive") as the other overdrives -- reusing that glyph.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
