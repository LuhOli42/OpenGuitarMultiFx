#include "BD2StyleOverdriveProcessor.h"
#include "DualMono.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double vcc = 8.0; // the audio circuit runs from ~8 V (capacitance-multiplier output), see the doc
    constexpr double vb = 4.0;  // the +4 V reference every "arrow" resistor returns to

    // Assumed device parameters (documented in the doc; the circuit is closed-loop biased).
    const NodalCircuit::JfetParams jfet2SK184 { 4.5e-3, -1.2, 0.02 };
    const NodalCircuit::BjtParams pnp2SA1335 { 1.0e-14, 25.85e-3, 200.0, 4.0 };
    const NodalCircuit::BjtParams npn2SC2459 { 1.0e-14, 25.85e-3, 300.0, 4.0 };

    // 1SS133 silicon switching diode ~ 1N4148 class (same SPICE parameters as the Tube Screamer model).
    constexpr double siIs = 2.52e-9;
    constexpr double siNVt = 1.752 * 25.85e-3;

    constexpr double switchOnResistance = 100.0; // JFET bypass switches in the effect path, "on"
    constexpr double gainPotMax = 250.0e3;       // VR1A/B 250KA rheostats
    constexpr double tonePotMax = 10.0e3;        // VR2 10KB
    constexpr double levelPotMax = 100.0e3;      // VR3 100KA
}

BD2StyleOverdriveProcessor::BD2StyleOverdriveProcessor()
{
    auto gainParam = std::make_unique<juce::AudioParameterFloat> (
        "bd2_gain", "Gain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "bd2_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "bd2_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    gain = gainParam.get();
    tone = toneParam.get();
    level = levelParam.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "bd2", "BD-2-Style Overdrive", "|", std::move (gainParam));
    group->addChild (std::move (toneParam));
    group->addChild (std::move (levelParam));
    parameters = std::move (group);
}

void BD2StyleOverdriveProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ block A
    // Input JFET source-follower buffer (Q3) and the effect-path coupling: C15 -> switch (Q6, on) -> C18
    // into stage 1's gate. Every "arrow" resistor returns to +4 V.
    {
        auto& c = ch.a;
        const auto in = c.addNode(), x18 = c.addNode(), g3 = c.addNode(), s3 = c.addNode();
        const auto nc15 = c.addNode(), n16 = c.addNode(), g10 = c.addNode();
        const auto v8 = c.addNode(), v4 = c.addNode();
        c.addSource (v8, vcc);
        c.addSource (v4, vb);
        ch.srcIn = c.addSource (in, 0.0);

        c.addResistor (in, x18, 10.0e3);          // R18
        c.addCapacitor (x18, g3, 0.047e-6);       // C14
        c.addResistor (g3, v4, 1.0e6);            // R15
        c.addFollower (g3, s3, -0.819);           // Q3 2SK184GR source follower (vgs -0.82 V at DC): a buffer
        c.addResistor (s3, gnd, 10.0e3);          // R19
        c.addCapacitor (s3, nc15, 10.0e-6);       // C15
        c.addResistor (nc15, v4, 100.0e3);        // R17
        c.addResistor (nc15, n16, switchOnResistance); // Q6 (JFET switch, on)
        c.addResistor (n16, v4, 100.0e3);         // R22
        c.addCapacitor (n16, g10, 0.1e-6);        // C18
        c.addResistor (g10, v4, 220.0e3);         // R23

        ch.nG10 = g10;
        c.setInitialGuess (x18, vb); c.setInitialGuess (g3, vb); c.setInitialGuess (s3, 2.0);
        c.setInitialGuess (nc15, 2.0); c.setInitialGuess (n16, vb); c.setInitialGuess (g10, vb);
    }

    // ================================================================ block B
    // Gain stage 1: JFET differential pair Q10/Q11 (shared 4.7K tail R30) driving PNP Q9; feedback from Q9's
    // collector through R29 + the Gain rheostat to Q11's gate, with R31 + C22 as the shunt leg; then the
    // fixed tone stack and the two back-to-back pairs of series diodes; C27 + R35 into stage 2's gate.
    {
        auto& c = ch.b;
        const auto v8 = c.addNode(), v4 = c.addNode(), g10 = c.addNode();
        const auto d10 = c.addNode(), s10 = c.addNode(), g11 = c.addNode(), c9 = c.addNode();
        const auto nfb = c.addNode(), n22 = c.addNode();
        const auto t1 = c.addNode(), t2 = c.addNode(), t3 = c.addNode(), n26 = c.addNode();
        const auto g14 = c.addNode();
        c.addSource (v8, vcc);
        c.addSource (v4, vb);
        ch.srcG10 = c.addSource (g10, vb);

        c.addJfet (d10, g10, s10, jfet2SK184);    // Q10
        c.addResistor (v8, d10, 2.2e3);           // R28
        c.addResistor (s10, gnd, 4.7e3);          // R30 (tail)
        c.addJfet (v8, g11, s10, jfet2SK184, 5.0); // Q11 (drain straight to the rail: always saturated, vds ~ 5 V)
        c.addBjt (c9, d10, v8, true, pnp2SA1335); // Q9 2SA1335R PNP
        c.addCapacitor (d10, c9, 47.0e-12);       // C21 (Miller)
        c.addResistor (c9, gnd, 2.2e3);           // R32
        c.addResistor (g11, n22, 1.5e3);          // R31
        c.addCapacitor (n22, gnd, 0.15e-6);       // C22
        c.addCapacitor (g11, c9, 47.0e-12);       // C23
        c.addResistor (g11, nfb, 22.0e3);         // R29
        ch.rGain1 = c.addResistor (nfb, c9, 60.0e3); // VR1A rheostat (250KA)

        // Fixed tone stack.
        c.addResistor (c9, t1, 100.0e3);          // R38
        c.addCapacitor (t1, t2, 0.1e-6);          // C34
        c.addCapacitor (t1, t3, 0.047e-6);        // C35
        c.addResistor (t2, t3, 1.0e6);            // R50
        c.addResistor (t3, gnd, 15.0e3);          // R51
        c.addCapacitor (c9, n26, 220.0e-12);      // C26
        c.addResistor (n26, t2, 330.0e3);         // R37

        // Clippers: two diodes in series each way (D7+D8 to clip positive, D9+D10 for negative). Two identical
        // diodes in series with nothing on the middle node are EXACTLY one diode with twice nVt (same current,
        // twice the voltage per e-fold), which drops a node and a Newton port per pair.
        c.addDiode (t2, gnd, siIs, 2.0 * siNVt);  // D7 + D8
        c.addDiode (gnd, t2, siIs, 2.0 * siNVt);  // D10 + D9

        c.addCapacitor (t2, g14, 0.0022e-6);      // C27
        c.addResistor (g14, v4, 1.0e6);           // R35

        ch.nC9 = c9;
        ch.nG14 = g14;
        c.setInitialGuess (g10, vb); c.setInitialGuess (d10, 7.3); c.setInitialGuess (s10, 4.9);
        c.setInitialGuess (g11, 4.2); c.setInitialGuess (c9, 4.2); c.setInitialGuess (nfb, 4.2);
        // DC: C34/C35/C26/C27 block, so t1 follows the collector while t2/t3/n26 (clipper diodes and R51 to
        // ground) sit at 0 V -- a guess that contradicts this (e.g. 4 V on t2) hands the diodes a huge current.
        c.setInitialGuess (n22, 4.2); c.setInitialGuess (t1, 4.2); c.setInitialGuess (t2, 0.0);
        c.setInitialGuess (t3, 0.0); c.setInitialGuess (n26, 0.0); c.setInitialGuess (g14, vb);
    }

    // ================================================================ block C
    // Gain stage 2 (same topology, its own values), R26/C17/C19 low-pass, Tone (C100/C101 + VR2), Level (VR3),
    // and the coupling C10 + R13 into the peak-filter op-amp's (+) pin.
    {
        auto& c = ch.c;
        const auto v8 = c.addNode(), v4 = c.addNode(), g14 = c.addNode();
        const auto d14 = c.addNode(), s14 = c.addNode(), g13 = c.addNode(), s2 = c.addNode();
        const auto nfb2 = c.addNode(), n24 = c.addNode(), lp = c.addNode();
        const auto nT3 = c.addNode(), nW = c.addNode(), nB = c.addNode(), nLW = c.addNode(), nP = c.addNode();
        c.addSource (v8, vcc);
        c.addSource (v4, vb);
        ch.srcG14 = c.addSource (g14, vb);

        c.addJfet (d14, g14, s14, jfet2SK184);    // Q14
        c.addResistor (v8, d14, 2.2e3);           // R33
        c.addResistor (s14, gnd, 4.7e3);          // R36 (tail)
        c.addJfet (v8, g13, s14, jfet2SK184, 5.0); // Q13 (drain on the rail)
        c.addBjt (s2, d14, v8, true, pnp2SA1335); // Q12 2SA1335R PNP
        c.addCapacitor (d14, s2, 100.0e-12);      // C20 (Miller)
        c.addResistor (s2, gnd, 2.2e3);           // R25
        c.addResistor (g13, n24, 2.2e3);          // R34
        c.addCapacitor (n24, gnd, 1.0e-6);        // C24
        c.addCapacitor (g13, s2, 100.0e-12);      // C25
        c.addResistor (g13, nfb2, 33.0e3);        // R27
        ch.rGain2 = c.addResistor (nfb2, s2, 60.0e3); // VR1B rheostat (250KA)

        // Low-pass after stage 2.
        c.addResistor (s2, lp, 5.6e3);            // R26
        c.addCapacitor (s2, lp, 5.6e-9);          // C17
        c.addCapacitor (lp, gnd, 5.6e-9);         // C19

        // Tone: C100 into VR2's pin 3, C101 from pin 1 to ground, wiper into the Level pot (VR3).
        c.addCapacitor (lp, nT3, 0.018e-6);       // C100
        ch.rToneTop = c.addResistor (nT3, nW, 5.0e3);    // VR2: pin 3 -> wiper
        ch.rToneBottom = c.addResistor (nW, nB, 5.0e3);  // VR2: wiper -> pin 1
        c.addCapacitor (nB, gnd, 0.018e-6);       // C101
        ch.rLevelTop = c.addResistor (nW, nLW, 50.0e3);  // VR3: top -> wiper
        ch.rLevelBottom = c.addResistor (nLW, gnd, 50.0e3); // VR3: wiper -> ground

        // Peak-filter op-amp's (+) pin: C10 in from the Level wiper, R13 bias to +4 V.
        c.addCapacitor (nLW, nP, 0.047e-6);       // C10
        c.addResistor (nP, v4, 470.0e3);          // R13

        ch.nS2 = s2;
        ch.nP = nP;
        c.setInitialGuess (g14, vb); c.setInitialGuess (d14, 7.3); c.setInitialGuess (s14, 4.9);
        c.setInitialGuess (g13, 4.2); c.setInitialGuess (s2, 4.2); c.setInitialGuess (nfb2, 4.2);
        c.setInitialGuess (n24, 4.2); c.setInitialGuess (lp, 4.2); c.setInitialGuess (nP, vb);
    }

    // ================================================================ block D
    // Peak filter: IC1B (ideal) with R8 || C8 feedback; the (-) node's shunt leg is C9 into a gyrator
    // (Q7 emitter follower with C16 coupling, R21 bootstrapped from its emitter) -- a simulated inductor.
    {
        auto& c = ch.d;
        const auto v8 = c.addNode(), v4 = c.addNode(), p = c.addNode();
        const auto m = c.addNode(), o7 = c.addNode(), x = c.addNode(), b7 = c.addNode(), e7 = c.addNode();
        c.addSource (v8, vcc);
        c.addSource (v4, vb);
        ch.srcP = c.addSource (p, vb);

        // IC1B (M5218AL on the 8 V rail): peak filter after two saturating gain stages, output ~1.5 V short of each rail.
        NodalCircuit::OpAmpSpec m5218;
        m5218.lowRail = 1.5;
        m5218.highRail = vcc - 1.5;
        c.addSaturatingOpAmp (p, m, o7, m5218);
        c.addResistor (m, o7, 5.6e3);             // R8
        c.addCapacitor (m, o7, 2.2e-9);           // C8
        c.addCapacitor (m, x, 0.056e-6);          // C9
        c.addCapacitor (x, b7, 0.056e-6);         // C16
        c.addResistor (b7, v4, 470.0e3);          // R10
        c.addFollower (b7, e7, 0.62);             // Q7 2SC2459R emitter follower (the gyrator's buffer)
        c.addResistor (e7, gnd, 10.0e3);          // R20
        c.addResistor (x, e7, 1.2e3);             // R21

        ch.nO7 = o7;
        c.setInitialGuess (m, vb); c.setInitialGuess (o7, vb); c.setInitialGuess (x, vb);
        c.setInitialGuess (b7, vb); c.setInitialGuess (e7, 3.3);
    }

    // ================================================================ block E
    // Output: C7 -> switch (Q4, on) -> C6 -> Q1 emitter follower -> C1 -> R1.
    {
        auto& c = ch.e;
        const auto v8 = c.addNode(), v4 = c.addNode(), o7 = c.addNode();
        const auto nc7 = c.addNode(), nm = c.addNode(), b1 = c.addNode(), e1 = c.addNode(), nc1 = c.addNode();
        const auto out = c.addNode();
        c.addSource (v8, vcc);
        c.addSource (v4, vb);
        ch.srcO7 = c.addSource (o7, vb);

        c.addCapacitor (o7, nc7, 10.0e-6);        // C7
        c.addResistor (nc7, v4, 100.0e3);         // R11
        c.addResistor (nc7, nm, switchOnResistance); // Q4 (JFET switch, on)
        c.addResistor (nm, v4, 100.0e3);          // R8 (bias at the switch's output node)
        c.addCapacitor (nm, b1, 10.0e-6);         // C6
        c.addResistor (b1, v4, 100.0e3);          // R7
        c.addFollower (b1, e1, 0.62);             // Q1 emitter follower
        c.addResistor (e1, gnd, 10.0e3);          // R2
        c.addCapacitor (e1, nc1, 10.0e-6);        // C1
        c.addResistor (nc1, gnd, 100.0e3);        // R3
        c.addResistor (nc1, out, 1.0e3);          // R1
        c.addResistor (out, gnd, 1.0e6);          // assumed downstream input impedance

        ch.nOut = out;
        c.setInitialGuess (nc7, vb); c.setInitialGuess (nm, vb); c.setInitialGuess (b1, vb);
        c.setInitialGuess (e1, 3.3);
    }
}

void BD2StyleOverdriveProcessor::updatePots (double gainKnob, double toneKnob, double levelKnob)
{
    // Gain: VR1A/B are 250KA rheostats (wiper tied to one end); more resistance in the feedback = more gain.
    // Audio taper approximated as knob^2 (same documented stand-in as the other log pots).
    const double rGain = juce::jmax (1.0, gainPotMax * gainKnob * gainKnob);

    // Tone: 10KB linear. Wiper at pin 3 (C100 side) = brightest.
    const double tTop = juce::jmax (1.0, tonePotMax * (1.0 - toneKnob));
    const double tBottom = juce::jmax (1.0, tonePotMax * toneKnob);

    // Level: 100KA, wiper to ground segment = audio-taper attenuation.
    const double lBottom = juce::jmax (1.0, levelPotMax * levelKnob * levelKnob);
    const double lTop = juce::jmax (1.0, levelPotMax - lBottom);

    for (auto& ch : channels)
    {
        ch.b.setResistance (ch.rGain1, rGain);
        ch.c.setResistance (ch.rGain2, rGain);
        ch.c.setResistance (ch.rToneTop, tTop);
        ch.c.setResistance (ch.rToneBottom, tBottom);
        ch.c.setResistance (ch.rLevelTop, lTop);
        ch.c.setResistance (ch.rLevelBottom, lBottom);
    }
}

void BD2StyleOverdriveProcessor::prepare (double newSampleRate, int, int)
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
    smoothedTone.reset (newSampleRate, 0.02);
    smoothedTone.setCurrentAndTargetValue (tone->get());
    smoothedLevel.reset (newSampleRate, 0.02);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    updatePots (gain->get(), tone->get(), level->get());

    // Prepare each block in signal order, feeding it the DC value its driven node has upstream.
    dcOk = true;
    for (auto& ch : channels)
    {
        dcOk = ch.a.prepare (newSampleRate) && dcOk;
        ch.b.setSource (ch.srcG10, ch.a.voltage (ch.nG10));
        dcOk = ch.b.prepare (newSampleRate) && dcOk;
        ch.c.setSource (ch.srcG14, ch.b.voltage (ch.nG14));
        dcOk = ch.c.prepare (newSampleRate) && dcOk;
        ch.d.setSource (ch.srcP, ch.c.voltage (ch.nP));
        dcOk = ch.d.prepare (newSampleRate) && dcOk;
        ch.e.setSource (ch.srcO7, ch.d.voltage (ch.nO7));
        dcOk = ch.e.prepare (newSampleRate) && dcOk;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true; // freshly built channels hold identical state
    channel1Stale = false;
    identicalRun = 0;
}

void BD2StyleOverdriveProcessor::process (juce::AudioBuffer<float>& buffer)
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
    smoothedTone.setTargetValue (tone->get());
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = smoothedGain.getNextValue();
        const float t = smoothedTone.getNextValue();
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

            ch.a.setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.a.solveSample();

            ch.b.setSource (ch.srcG10, ch.a.voltage (ch.nG10));
            ok = ch.b.solveSample() && ok;

            ch.c.setSource (ch.srcG14, ch.b.voltage (ch.nG14));
            ok = ch.c.solveSample() && ok;

            ch.d.setSource (ch.srcP, ch.c.voltage (ch.nP));
            ok = ch.d.solveSample() && ok;

            ch.e.setSource (ch.srcO7, ch.d.voltage (ch.nO7));
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

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void BD2StyleOverdriveProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same reference-sheet category/entry ("Overdrive") as the other overdrives -- reusing that glyph.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
