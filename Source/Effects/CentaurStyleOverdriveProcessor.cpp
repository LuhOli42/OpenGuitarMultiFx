#include "CentaurStyleOverdriveProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vBias = 4.5; // the 4.5 V virtual ground the whole circuit is referenced to

    // 1N34A germanium: Is ~ 200 nA, emission coefficient ~ 1.3 gives the ~0.35 V forward drop
    // ElectroSmash quotes (at ~10 mA); see the doc for how these were chosen.
    constexpr double geIs = 200.0e-9;
    constexpr double geNVt = 1.3 * 25.85e-3;

    constexpr double gainPot = 100.0e3;   // dual gang; value taken from clone BOMs, not printed on the drawing
    constexpr double treblePot = 10.0e3;  // "10kB"
    constexpr double levelPot = 10.0e3;   // "10kB"
}

CentaurStyleOverdriveProcessor::CentaurStyleOverdriveProcessor()
{
    auto gainParam = std::make_unique<juce::AudioParameterFloat> (
        "centaur_gain", "Gain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto trebleParam = std::make_unique<juce::AudioParameterFloat> (
        "centaur_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "centaur_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    gain = gainParam.get();
    treble = trebleParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "centaur", "Centaur-Style Overdrive", "|", std::move (gainParam));
    group->addChild (std::move (trebleParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void CentaurStyleOverdriveProcessor::buildChannel (Channel& ch, double /*sr*/)
{
    const auto gnd = NodalCircuit::ground;

    // ---------------------------------------------------------------- block 0
    // Input buffer: IC1_A is a unity-gain follower on a TL072 (JFET input, no input
    // current), so its output is exactly the (+) node's voltage, which depends only on
    // R1 / C1 / R2. Blocks 1 and 2 take that voltage as a driven node "B".
    {
        auto& c = ch.block0;
        const auto in = c.addNode(), x1 = c.addNode(), a = c.addNode(), vb = c.addNode();
        ch.srcIn = c.addSource (in, 0.0);
        c.addSource (vb, vBias);
        c.addResistor (in, x1, 10.0e3);         // R1
        c.addCapacitor (x1, a, 0.1e-6);         // C1
        c.addResistor (a, vb, 1.0e6);           // R2
        ch.nodeA = a;
        c.setInitialGuess (a, vBias);
    }

    // ---------------------------------------------------------------- block 1
    {
        auto& c = ch.block1;
        const auto vb = c.addNode(), B = c.addNode();
        const auto in1 = c.addNode(), ff1 = c.addNode(), g = c.addNode(), h = c.addNode(), inv = c.addNode();
        const auto o1 = c.addNode(), c9o = c.addNode(), d = c.addNode(), p = c.addNode(), n11 = c.addNode();
        const auto n12 = c.addNode(), B2 = c.addNode(), nc6 = c.addNode(), BG = c.addNode();
        const auto s = c.addNode(), u = c.addNode();

        c.addSource (vb, vBias);
        ch.srcB1 = c.addSource (B, vBias);

        // Gain stage, non-inverting (IC1_B). in1 is the node after C3.
        c.addCapacitor (B, in1, 0.1e-6);        // C3
        c.addResistor (in1, g, 10.0e3);         // R6
        c.addCapacitor (in1, g, 68.0e-9);       // C5 (across R6)
        ch.rG1 = c.addResistor (g, vb, 50.0e3); // Gain pot gang 1: pin1 -> wiper (wiper on 4.5 V)
        ch.rH = c.addResistor (h, vb, 52.0e3);  // R10 + gang 1 pin3 -> wiper
        c.addResistor (h, inv, 15.0e3);         // R11
        c.addCapacitor (h, inv, 82.0e-9);       // C7
        c.addResistor (inv, o1, 422.0e3);       // R12
        c.addCapacitor (inv, o1, 390.0e-12);    // C8
        // TL072 on the Klon's +-9 V rails (a charge pump makes them from the 9 V battery), about 1.5 V short of each:
        // at full Gain a guitar drives this stage past +-7.5 V, and unlike the summing amp (27 V of headroom) it does
        // not have the room, so it runs into its rails before the germanium diodes after it clip.
        NodalCircuit::OpAmpSpec tl072;
        tl072.lowRail = vBias - 7.5;
        tl072.highRail = vBias + 7.5;
        c.addSaturatingOpAmp (g, inv, o1, tl072);

        // Germanium clipper after the gain stage: C9 -> R13 -> antiparallel diodes to GND.
        c.addCapacitor (o1, c9o, 1.0e-6);       // C9
        c.addResistor (c9o, d, 1.0e3);          // R13
        c.addDiode (d, gnd, geIs, geNVt);       // D2
        c.addDiode (gnd, d, geIs, geNVt);       // D3
        c.addCapacitor (d, p, 1.0e-6);          // C10

        // Feed-forward network 1: R7 + C16 low-pass, then R19 into the summing node.
        c.addResistor (in1, ff1, 1.5e3);        // R7
        c.addCapacitor (ff1, gnd, 1.0e-6);      // C16
        c.addResistor (ff1, s, 15.0e3);         // R19

        // Feed-forward network 2 (referenced through the second gang of the Gain pot).
        c.addResistor (p, s, 47.0e3);           // R16
        c.addCapacitor (p, n11, 2.2e-9);        // C11
        c.addResistor (n11, BG, 22.0e3);        // R15
        c.addResistor (s, BG, 2.0e3);           // R17
        c.addCapacitor (s, n12, 27.0e-9);       // C12
        c.addResistor (n12, BG, 2.0e3);         // R18

        // B -> (R5 || C4) -> B2, which hangs R8, C6+R9 and gang 2 off it.
        c.addResistor (B, B2, 5.1e3);           // R5
        c.addCapacitor (B, B2, 68.0e-9);        // C4
        c.addResistor (B2, vb, 1.5e3);          // R8
        c.addCapacitor (B2, nc6, 0.39e-6);      // C6
        c.addResistor (nc6, vb, 1.0e3);         // R9
        ch.rB2BG = c.addResistor (B2, BG, 50.0e3); // gang 2: pin1 -> wiper
        ch.rBGVB = c.addResistor (BG, vb, 50.0e3); // gang 2: wiper -> pin3

        // Summing amplifier (IC2_A), inverting, (+) on 4.5 V.
        c.addOpAmp (vb, s, u);
        c.addResistor (s, u, 392.0e3);          // R20
        c.addCapacitor (s, u, 820.0e-12);       // C13

        ch.o1 = o1;
        ch.u = u;
        for (auto n : { in1, ff1, g, h, inv, o1, p, n11, n12, B2, nc6, BG, s, u })
            c.setInitialGuess (n, vBias);
    }

    // ---------------------------------------------------------------- block 2
    {
        auto& c = ch.block2;
        const auto vb = c.addNode(), B = c.addNode(), un = c.addNode();
        const auto v = c.addNode(), wT = c.addNode(), w = c.addNode(), n15 = c.addNode();
        const auto out = c.addNode(), nc2 = c.addNode(), nr4 = c.addNode();

        c.addSource (vb, vBias);
        ch.srcB2 = c.addSource (B, vBias);
        ch.srcU = c.addSource (un, vBias);

        // Active treble stage (IC2_B, inverting).
        c.addResistor (un, v, 100.0e3);            // R22
        c.addResistor (v, w, 100.0e3);             // R24
        c.addOpAmp (vb, v, w);
        ch.rToneTop = c.addResistor (un, wT, 6.8e3); // R21 + treble pot pin3 -> wiper
        c.addCapacitor (wT, v, 3.9e-9);            // C14
        ch.rToneBot = c.addResistor (wT, w, 9.7e3);  // treble pot wiper -> pin1 + R23

        // Output: C15 -> R25 -> Volume pot; the wiper is the pedal output (bypass switch closed).
        c.addCapacitor (w, n15, 4.7e-6);           // C15
        ch.rVolSeries = c.addResistor (n15, out, 5.56e3); // R25 + volume pot top segment
        ch.rVolBottom = c.addResistor (out, gnd, 5.0e3);  // volume pot bottom segment
        c.addResistor (out, gnd, 100.0e3);         // R28
        c.addResistor (out, gnd, 1.0e6);           // assumed downstream input impedance

        // Clean bleed that runs past the whole effect: B -> C2 -> R4 -> R26 -> output.
        c.addCapacitor (B, nc2, 4.7e-6);           // C2
        c.addResistor (nc2, gnd, 100.0e3);         // R3
        c.addResistor (nc2, nr4, 560.0);           // R4
        c.addResistor (nr4, out, 68.0e3);          // R26

        ch.out = out;
        for (auto n : { v, wT, w, n15, nc2, nr4 })
            c.setInitialGuess (n, vBias);
    }
}

void CentaurStyleOverdriveProcessor::updatePots (double gainKnob, double trebleKnob, double levelKnob)
{
    // Gain: audio taper approximated as knob^2, both gangs turn together; f = pin1 -> wiper fraction.
    const double f = gainKnob * gainKnob;
    const double rPin1 = juce::jmax (0.5, gainPot * f);
    const double rPin3 = juce::jmax (0.5, gainPot * (1.0 - f));

    // Treble: linear "B" pot. Wiper toward pin 3 (R21 side) = maximum boost (Gvmax = (RV+R23)/R21).
    const double tTop = juce::jmax (0.0, treblePot * (1.0 - trebleKnob));
    const double tBot = juce::jmax (0.0, treblePot * trebleKnob);

    // Level: linear "B" pot; wiper is the output.
    const double vTop = juce::jmax (0.0, levelPot * (1.0 - levelKnob));
    const double vBot = juce::jmax (1.0, levelPot * levelKnob);

    for (auto& ch : channels)
    {
        ch.block1.setResistance (ch.rG1, rPin1);
        ch.block1.setResistance (ch.rH, 2.0e3 + rPin3);   // R10 + gang 1 pin3 -> wiper
        ch.block1.setResistance (ch.rB2BG, rPin1);
        ch.block1.setResistance (ch.rBGVB, rPin3);
        ch.block2.setResistance (ch.rToneTop, 1.8e3 + tTop);  // R21 + pot
        ch.block2.setResistance (ch.rToneBot, 4.7e3 + tBot);  // pot + R23
        ch.block2.setResistance (ch.rVolSeries, 560.0 + vTop); // R25 + pot
        ch.block2.setResistance (ch.rVolBottom, vBot);
    }
}

void CentaurStyleOverdriveProcessor::prepare (double newSampleRate, int, int)
{
    // Same-rate re-prepare is a no-op (the UI's chain reorder re-prepares every processor):
    // rebuilding would wipe live circuit state and re-settle under running audio.
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch, newSampleRate);
    }

    smoothedGain.reset (newSampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (gain->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (treble->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (gain->get(), treble->get(), level->get());

    // The blocks were prepared with nominal pot values in buildChannel(); now that the real
    // pot resistances are set, re-solve each block's DC point by preparing again in order.
    for (auto& ch : channels)
    {
        ch.block0.prepare (newSampleRate);
        ch.block1.setSource (ch.srcB1, ch.block0.voltage (ch.nodeA));
        ch.block1.prepare (newSampleRate);
        ch.block2.setSource (ch.srcB2, ch.block0.voltage (ch.nodeA));
        ch.block2.setSource (ch.srcU, ch.block1.voltage (ch.u));
        ch.block2.prepare (newSampleRate);
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true; // freshly built channels hold identical state
    channel1Stale = false;
    identicalRun = 0;
}

void CentaurStyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    // Dual-mono shortcut: identical INPUT does not imply identical OUTPUT unless the two circuits also have
    // identical STATE (capacitor charges), so it is used only while the channels are known to be in sync.
    // While it runs, channel 1's circuit is left behind; it is caught up by copying channel 0's the moment the
    // channels diverge. After a divergence the shortcut stays off until the input has been identical for 10 s,
    // by which time the circuits' memory has decayed enough that re-syncing by copy is inaudible.
    const bool dualMono = numChannels == 2 && blockIsDualMono (buffer);
    bool useShortcut = false;
    if (numChannels == 2)
    {
        identicalRun = dualMono ? identicalRun + numSamples : 0;

        if (! channelsSynced && identicalRun >= (long long) (10.0 * sampleRate))
        {
            channels[1] = channels[0];
            channelsSynced = true;
            channel1Stale = false;
        }

        useShortcut = dualMono && channelsSynced;

        if (! useShortcut && channel1Stale)
        {
            channels[1] = channels[0];
            channel1Stale = false;
        }

        if (! dualMono)
            channelsSynced = false;
    }
    const int solveChannels = useShortcut ? 1 : numChannels;
    if (useShortcut)
        channel1Stale = true;

    smoothedGain.setTargetValue (gain->get());
    smoothedTreble.setTargetValue (treble->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        const float t = smoothedTreble.getNextValue();
        const float l = smoothedLevel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (g, t, l);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.block0.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.block0.solveSample();
            const double B = ch.block0.voltage (ch.nodeA);

            ch.block1.setSource (ch.srcB1, B);
            ok = ch.block1.solveSample() && ok;
            const double u = ch.block1.voltage (ch.u);

            ch.block2.setSource (ch.srcB2, B);
            ch.block2.setSource (ch.srcU, u);
            ok = ch.block2.solveSample() && ok;

            data[i] = (float) ch.block2.voltage (ch.out);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void CentaurStyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same reference-sheet category/entry ("Overdrive") as the other overdrives -- reusing that glyph.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
