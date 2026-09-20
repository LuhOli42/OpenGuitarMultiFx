#include "DS1StyleDistortionProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double v9 = 9.0;
    constexpr double vb = 4.5; // R24/R25 divider, treated as an ideal fixed rail (see docs)

    // Representative small-signal silicon NPN (Q1/Q2/Q3) and 2SK30ATM-GR-grade JFET (Q6) parameters:
    // same "documented, adjustable assumption" status as before, see docs/circuits/DS1StyleDistortion.md.
    const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, 200.0, 4.0 };
    const NodalCircuit::JfetParams jfetQ6 { 3.0e-3, -2.0, 0.02 };

    // 1N4148-class: Is = 2.52 nA with the emission coefficient N = 1.752 (the same pair every other pedal here uses).
    // It used to be N = 1, which is not a real diode: with this Is it clips at ~0.33 V instead of the ~0.6-0.7 V a
    // 1S1588/1N4148 really does (ElectroSmash measures 1.4 Vpp after the DS-1's clipper), and a knee twice as sharp --
    // harsher, quieter, and out of scale with every other pedal.
    constexpr double diodeIs = 2.52e-9;
    constexpr double diodeNVt = 1.752 * 25.85e-3;

    constexpr double driveMax = 100.0e3, toneMax = 20.0e3, levelMax = 100.0e3;
    constexpr double closedSwitchResistance = 10.0; // Q7 modelled as a closed bypass switch
    constexpr double outputLoadResistance = 1.0e6;  // assumed downstream input impedance
}

DS1StyleDistortionProcessor::DS1StyleDistortionProcessor()
{
    auto driveParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "ds1_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    drive = driveParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ds1", "DS-1-Style Distortion", "|", std::move (driveParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void DS1StyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ---------------------------------------------------------------- pre
    {
        auto& c = ch.pre;
        const auto nv9 = c.addNode(), nvb = c.addNode(), in = c.addNode();
        const auto x1 = c.addNode(), b1 = c.addNode(), e1 = c.addNode(), nA = c.addNode(), nB = c.addNode();
        const auto b2 = c.addNode(), c2 = c.addNode(), e2 = c.addNode();
        const auto vp = c.addNode(), nm = c.addNode(), x8 = c.addNode(), o = c.addNode();
        c.addSource (nv9, v9);
        c.addSource (nvb, vb);
        ch.srcIn = c.addSource (in, 0.0);

        // Q1: emitter follower input buffer.
        c.addCapacitor (in, x1, 0.047e-6);            // C1
        c.addResistor (x1, b1, 1.0e3);                // R1
        c.addResistor (b1, nvb, 470.0e3);             // R2
        c.addFollower (b1, e1, 0.62);                 // Q1 emitter follower (Vbe 0.62 V at 0.33 mA): a buffer, nothing that clips
        c.addResistor (e1, gnd, 10.0e3);              // R3

        // Q6: JFET used as a voltage-controlled resistor between node A and node B (gate on the bias rail).
        c.addCapacitor (e1, nA, 0.47e-6);             // C2
        c.addResistor (nA, nvb, 100.0e3);             // R4
        // Q6: a JFET used as a resistor with its gate on the bias rail, so vgs ~ 0 and it sits in the triode region at
        // r_ds = 1/(2 beta (0 - Vp)) = 333 ohm; the signal at this point is < 0.3 V against Vp = -2 V, i.e. a 15% wobble
        // of a 333 ohm resistor in series with 100K and 47 nF. Same sound, two Newton ports fewer.
        c.addResistor (nA, nB, 333.0);                // Q6
        c.addResistor (nB, nvb, 1.0e6);               // R5

        // Q2: common-emitter gain stage with collector-to-base shunt feedback (R7 || C4).
        c.addCapacitor (nB, b2, 0.047e-6);            // C3
        c.addResistor (b2, gnd, 100.0e3);             // R6
        c.addResistor (b2, c2, 470.0e3);              // R7
        c.addCapacitor (b2, c2, 250.0e-12);           // C4
        c.addBjt (c2, b2, e2, false, npn);            // Q2
        c.addResistor (e2, gnd, 22.0);                // R9
        c.addResistor (nv9, c2, 10.0e3);              // R8

        // Op-amp gain stage (ideal): (+) via C5 from Q2's collector, R11 bias; (-) leg R13 + C8 to ground;
        // Drive pot in the feedback.
        c.addCapacitor (c2, vp, 0.47e-6);             // C5
        c.addResistor (vp, nvb, 100.0e3);             // R11
        // TA7136AP on a single 9 V supply: output swing "+1.5 V to Vcc - 1.5 V" (datasheet), so it runs out of room
        // long before the diodes after it do -- a big part of the DS-1's sound, and what an ideal op-amp lacks.
        NodalCircuit::OpAmpSpec ta7136;
        ta7136.lowRail = 1.5;
        ta7136.highRail = v9 - 1.5;
        c.addSaturatingOpAmp (vp, nm, o, ta7136);
        c.addResistor (nm, x8, 4.7e3);                // R13
        c.addCapacitor (x8, gnd, 1.0e-6);             // C8
        ch.rDrive = c.addResistor (nm, o, 50.0e3);    // VR1 (Drive)

        ch.nB2 = b2; ch.nE2 = e2; ch.nC2 = c2; ch.nO = o;
        c.setInitialGuess (b1, 3.9); c.setInitialGuess (e1, 3.3); c.setInitialGuess (nA, vb); c.setInitialGuess (nB, vb);
        c.setInitialGuess (b2, 0.65); c.setInitialGuess (c2, 4.0); c.setInitialGuess (e2, 0.02);
        c.setInitialGuess (vp, vb); c.setInitialGuess (nm, vb); c.setInitialGuess (x8, vb); c.setInitialGuess (o, vb);
    }

    // ---------------------------------------------------------------- post
    {
        auto& c = ch.post;
        const auto nv9 = c.addNode(), nvb = c.addNode(), o = c.addNode();
        const auto x9 = c.addNode(), n0 = c.addNode(), n1 = c.addNode(), n2 = c.addNode(), x11 = c.addNode();
        const auto n3 = c.addNode(), w = c.addNode(), p13 = c.addNode(), b3 = c.addNode(), e3 = c.addNode();
        const auto x14 = c.addNode(), out = c.addNode();
        c.addSource (nv9, v9);
        c.addSource (nvb, vb);
        ch.srcO = c.addSource (o, vb);

        // R14 + C9 into the clipper node; anti-parallel diode pair to the bias rail.
        c.addResistor (o, x9, 2.2e3);                 // R14
        c.addCapacitor (x9, n0, 0.47e-6);             // C9
        c.addDiode (n0, nvb, diodeIs, diodeNVt);      // D4
        c.addDiode (nvb, n0, diodeIs, diodeNVt);      // D5

        // Big Muff-style Tone network (a genuine bridged network, not a ladder).
        c.addCapacitor (n0, nvb, 0.01e-6);            // C10
        c.addResistor (n0, n1, 6.8e3);                // R16
        c.addCapacitor (n1, nvb, 0.1e-6);             // C12
        c.addCapacitor (n0, x11, 0.022e-6);           // C11
        c.addResistor (x11, n2, 2.2e3);               // R15
        c.addResistor (n2, nvb, 6.8e3);               // R17
        ch.rToneA = c.addResistor (n1, n3, 10.0e3);   // VR2: lug A -> wiper
        ch.rToneB = c.addResistor (n2, n3, 10.0e3);   // VR2: lug B -> wiper

        // Level pot, the closed bypass switch + R18, C13 into Q3.
        ch.rLevelTop = c.addResistor (n3, w, 50.0e3);
        ch.rLevelBottom = c.addResistor (w, nvb, 50.0e3);
        c.addResistor (w, p13, closedSwitchResistance + 10.0e3); // Q7 (closed) + R18
        c.addCapacitor (p13, b3, 0.047e-6);           // C13
        c.addResistor (b3, nvb, 1.0e6);               // R19
        c.addFollower (b3, e3, 0.62);                 // Q3 emitter follower
        c.addResistor (e3, gnd, 10.0e3);              // R21
        c.addResistor (e3, x14, 1.0e3);               // R22
        c.addCapacitor (x14, out, 1.0e-6);            // C14
        c.addResistor (out, gnd, 1.0 / (1.0 / 100.0e3 + 1.0 / outputLoadResistance)); // R20 || assumed load

        ch.nOut = out;
        for (auto n : { o, x9, n0, n1, n2, x11, n3, w, p13 })
            c.setInitialGuess (n, vb);
        c.setInitialGuess (b3, 3.9); c.setInitialGuess (e3, 3.3);
    }
}

void DS1StyleDistortionProcessor::updatePots (double driveKnob, double toneKnob, double levelKnob)
{
    const double rDrive = juce::jmax (1.0, driveMax * driveKnob);
    const double rToneA = juce::jmax (1.0, toneMax * toneKnob);
    const double rToneB = juce::jmax (1.0, toneMax - rToneA);
    const double rLevelTop = juce::jmax (1.0, levelMax * (1.0 - levelKnob));
    const double rLevelBottom = juce::jmax (1.0, levelMax - rLevelTop);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rDrive, rDrive);
        ch.post.setResistance (ch.rToneA, rToneA);
        ch.post.setResistance (ch.rToneB, rToneB);
        ch.post.setResistance (ch.rLevelTop, rLevelTop);
        ch.post.setResistance (ch.rLevelBottom, rLevelBottom);
    }
}

void DS1StyleDistortionProcessor::prepare (double newSampleRate, int, int)
{
    // A same-rate re-prepare is a no-op (the UI's chain reorder re-prepares every processor already in the chain):
    // rebuilding would wipe live circuit state.
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
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        ch.post.setSource (ch.srcO, ch.pre.voltage (ch.nO));
        dcOk = ch.post.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true;
    channel1Stale = false;
    identicalRun = 0;
}

void DS1StyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    // Dual-mono shortcut: see CentaurStyleOverdriveProcessor::process().
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

            ch.pre.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.pre.solveSample();

            ch.post.setSource (ch.srcO, ch.pre.voltage (ch.nO));
            ok = ch.post.solveSample() && ok;

            data[i] = (float) ch.post.voltage (ch.nOut);

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

void DS1StyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // The sheet lists "Distortion" as its own distinct glyph, but it still isn't saved in the repo (see
    // docs/icons/AGENT-icon-notes.md), so this reuses the overdrive icon as a placeholder.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
