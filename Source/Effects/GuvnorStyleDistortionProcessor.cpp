#include "GuvnorStyleDistortionProcessor.h"
#include "PotTaper.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 9.0;
    constexpr double vBias = 4.5; // R13/R14 47K divider + C14 10 uF

    // Red LED: ~1.75 V at 0.5 mA, ~1.9 V at 3 mA (ElectroSmash: the clipping point is 1.8-2 V).
    constexpr double ledIs = 1.3e-19;
    constexpr double ledNVt = 1.9 * 25.85e-3;

    // TL072: gain 200 000, gain-bandwidth 3 MHz, output resistance ~100 ohm, swing ~1.5 V short of each rail. No slew limit
    // (13 V/us: no problem at this audio bandwidth).
    const NodalCircuit::OpAmpMacro tl072 { 2.0e5, 3.0e6, 100.0, 1.5, 7.5, 0.0, 0.0 };

    constexpr double gainPotMax = 100.0e3;   // VR1
    constexpr double tonePotMax = 10.0e3;    // VR2 (bass), VR3 (middle), VR4 (treble)
    constexpr double levelPotMax = 100.0e3;  // VR5
    constexpr double downstreamLoad = 1.0e6;
}

GuvnorStyleDistortionProcessor::GuvnorStyleDistortionProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };

    auto gainParam = make ("guvnor_gain", "Gain", 0.5f);
    auto bassParam = make ("guvnor_bass", "Bass", 0.5f);
    auto midParam = make ("guvnor_mid", "Middle", 0.5f);
    auto trebleParam = make ("guvnor_treble", "Treble", 0.5f);
    auto levelParam = make ("guvnor_level", "Level", 0.5f);

    gain = gainParam.get();
    bass = bassParam.get();
    mid = midParam.get();
    treble = trebleParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "guvnor", "Guv'nor-Style Distortion", "|", std::move (gainParam));
    group->addChild (std::move (bassParam));
    group->addChild (std::move (midParam));
    group->addChild (std::move (trebleParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void GuvnorStyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A: the gain stage
    // C1 -> (+) with R2 1M to the bias; (-) leg = R3 2.2K + C3 100 nF to ground (unity gain at DC); feedback = the Gain pot
    // from (-) to its wiper, which is the op-amp output, with C2 120 pF across it.
    {
        auto& c = ch.a;
        const auto v9 = c.addNode(), nb = c.addNode(), in = c.addNode();
        const auto plus = c.addNode(), minus = c.addNode(), n3 = c.addNode(), out = c.addNode();
        c.addSource (v9, vcc);
        ch.srcIn = c.addSource (in, 0.0);

        c.addResistor (v9, nb, 47.0e3);       // R13
        c.addResistor (nb, gnd, 47.0e3);      // R14
        c.addCapacitor (nb, gnd, 10.0e-6);    // C14

        c.addCapacitor (in, plus, 9.6e-9);    // C1
        c.addResistor (plus, nb, 1.0e6);      // R2
        c.addOpAmpMacro (plus, minus, out, tl072);
        c.addResistor (minus, n3, 2.2e3);     // R3
        c.addCapacitor (n3, gnd, 100.0e-9);   // C3
        c.addCapacitor (minus, out, 120.0e-12); // C2
        ch.rGainFeedback = c.addResistor (minus, out, 1.0e3); // VR1, (-) side

        ch.nOp1 = out;
        for (auto n : { nb, plus, minus, n3, out })
            c.setInitialGuess (n, vBias);
    }

    // ================================================================ block B: clipping stage, tone stack, level
    {
        auto& c = ch.b;
        const auto v9 = c.addNode(), nb = c.addNode(), src = c.addNode();
        const auto nOut1 = c.addNode(), nSer = c.addNode(), nX = c.addNode(), nY = c.addNode();
        const auto minus = c.addNode(), out2 = c.addNode(), nC7 = c.addNode(), nQ = c.addNode();
        const auto p1 = c.addNode(), p3 = c.addNode(), p4 = c.addNode(), t = c.addNode(), w3 = c.addNode();
        const auto mTop = c.addNode(), mC = c.addNode(), outT = c.addNode(), wl = c.addNode(), outN = c.addNode();
        c.addSource (v9, vcc);
        ch.srcOp1 = c.addSource (src, vBias);

        c.addResistor (v9, nb, 47.0e3);       // R13
        c.addResistor (nb, gnd, 47.0e3);      // R14
        c.addCapacitor (nb, gnd, 10.0e-6);    // C14

        // op-amp 1's output resistance, the Gain pot's wiper-to-end segment, C4, R4, C5 into (-) of op-amp 2
        c.addResistor (src, nOut1, tl072.outputOhms);
        ch.rGainSeries = c.addResistor (nOut1, nSer, 1.0e3);
        c.addCapacitor (nSer, nX, 220.0e-9);  // C4
        c.addResistor (nX, nY, 10.0e3);       // R4
        c.addResistor (nX, nb, 1.0e8);        // DC path for the coupling-cap island (the status LED's leg on the pedal)
        c.addCapacitor (nY, minus, 100.0e-9); // C5

        // op-amp 2: inverting, R5 680K || C6 220 pF, (+) at the bias
        c.addOpAmpMacro (nb, minus, out2, tl072);
        c.addResistor (minus, out2, 680.0e3); // R5
        c.addCapacitor (minus, out2, 220.0e-12); // C6

        // C7 -> R6 -> the red LED pair to ground = the tone stack's input
        c.addCapacitor (out2, nC7, 220.0e-9); // C7
        c.addResistor (nC7, nQ, 1.0e3);       // R6
        c.addDiode (nQ, gnd, ledIs, ledNVt, 30.0e-9);  // D1
        c.addDiode (gnd, nQ, ledIs, ledNVt, 30.0e-9);  // D2

        // Tone stack (ElectroSmash's "Tone Control" drawing): IN = nQ, OUT = outT
        c.addResistor (nQ, p1, 1.5e3);        // R7
        c.addCapacitor (nQ, t, 4.7e-9);       // C9
        c.addCapacitor (p1, p3, 100.0e-9);    // C8
        c.addResistor (p3, gnd, 680.0);       // R8
        c.addResistor (p1, p4, 680.0);        // R9
        ch.rBassTop = c.addResistor (p4, p3, 1.0e3);      // VR2 bass: top end (P4) to wiper (P3)
        ch.rBassBottom = c.addResistor (p3, gnd, 1.0e3);  //           wiper to ground
        c.addCapacitor (p1, w3, 220.0e-9);    // C10 into the Middle pot's wiper
        ch.rMidBottom = c.addResistor (w3, gnd, 1.0e3);   // VR3 mid: wiper to ground
        ch.rMidTop = c.addResistor (w3, mTop, 1.0e3);     //          wiper to the top end
        c.addResistor (mTop, mC, 100.0);      // R10
        c.addCapacitor (mC, t, 10.0e-9);      // C11
        ch.rTrebleTop = c.addResistor (t, outT, 1.0e3);   // VR4 treble: top end (T) to wiper (OUT)
        ch.rTrebleBottom = c.addResistor (outT, p4, 1.0e3); //            wiper to the bottom end (P4)
        c.addCapacitor (p4, gnd, 68.0e-9);    // C12

        // Level pot (top = tone OUT, bottom = ground), R11 22K, C13 470 pF, the next input
        ch.rLevelTop = c.addResistor (outT, wl, 1.0e3);
        ch.rLevelBottom = c.addResistor (wl, gnd, 1.0e3);
        c.addResistor (wl, outN, 22.0e3);     // R11
        c.addCapacitor (outN, gnd, 470.0e-12); // C13
        c.addResistor (outN, gnd, downstreamLoad);

        ch.nOp2 = out2;
        ch.nLed = nQ;
        ch.nOut = outN;
        for (auto n : { nb, src, nOut1, nSer, nX, nY, minus, out2 })
            c.setInitialGuess (n, vBias);
    }
}

void GuvnorStyleDistortionProcessor::updatePots (double gainKnob, double bassKnob, double midKnob, double trebleKnob, double levelKnob)
{
    // Gain: 100K linear; the wiper is the op-amp's output, so the (-) side is the feedback and the rest is a series
    // resistance into the second stage.
    const double gFb = juce::jmax (1.0, gainPotMax * gainKnob);
    const double gSer = juce::jmax (1.0, gainPotMax - gFb);

    // Tone pots: 10K linear. The schematic does not say which way each one turns, so the direction is the one in which
    // the knob raises its own band (the tests check that): Middle's wiper rises from the ground end with the knob; Bass's
    // and Treble's wipers approach their far ends the more the knob is turned down.
    auto seg = [] (double knob) { return juce::jmax (1.0, tonePotMax * knob); };
    const double bassBottom = seg (1.0 - bassKnob), bassTop = juce::jmax (1.0, tonePotMax - bassBottom);
    const double midBottom = seg (midKnob), midTop = juce::jmax (1.0, tonePotMax - midBottom);
    const double trebleBottom = seg (trebleKnob), trebleTop = juce::jmax (1.0, tonePotMax - trebleBottom);

    // Level: 100K audio taper, wiper-to-ground segment.
    const double lBottom = juce::jmax (1.0, levelPotMax * pots::audio (levelKnob));
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.a.setResistance (ch.rGainFeedback, gFb);
        ch.b.setResistance (ch.rGainSeries, gSer);
        ch.b.setResistance (ch.rBassTop, bassTop);
        ch.b.setResistance (ch.rBassBottom, bassBottom);
        ch.b.setResistance (ch.rMidTop, midTop);
        ch.b.setResistance (ch.rMidBottom, midBottom);
        ch.b.setResistance (ch.rTrebleTop, trebleTop);
        ch.b.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.b.setResistance (ch.rLevelTop, lTop);
        ch.b.setResistance (ch.rLevelBottom, lBottom);
    }
}

void GuvnorStyleDistortionProcessor::prepare (double newSampleRate, int, int)
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

    smoothedGain.reset (newSampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (gain->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bass->get());
    smoothedMid.reset (newSampleRate, 0.02);
    smoothedMid.setCurrentAndTargetValue (mid->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (treble->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (gain->get(), bass->get(), mid->get(), treble->get(), level->get());

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

void GuvnorStyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gain->get());
    smoothedBass.setTargetValue (bass->get());
    smoothedMid.setTargetValue (mid->get());
    smoothedTreble.setTargetValue (treble->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        const float b = smoothedBass.getNextValue();
        const float m = smoothedMid.getNextValue();
        const float t = smoothedTreble.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (g, b, m, t, l);
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

void GuvnorStyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Distortion glyph (Assets/Icons/distortion.svg): the overdrive wave with flat, hard-clipped tops and steeper sides -- between the
    // overdrive's rounded one and the (future) fuzz's square one. See docs/icons/AGENT-icon-notes.md.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
