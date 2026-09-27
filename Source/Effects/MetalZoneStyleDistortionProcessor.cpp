#include "MetalZoneStyleDistortionProcessor.h"
#include "IconKit.h"
#include "NobelsCommon.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace nobels; // vBias, siIs, siNVt, jfetSwitchOhms

    constexpr double supplyVolts = 9.0;
    constexpr double distPotMax = 250.0e3;    // VR01, 250kA
    constexpr double toneA = 100.0e3;         // VR03a / VR03b / VR02b, 100kG
    constexpr double midFreqPotMax = 50.0e3;  // VR02a, 50kC x2
    constexpr double levelPotMax = 50.0e3;    // VR04, 50kA

    // M5218AL-class dual op-amp on a 9 V supply (the drawing names it; its output swings ~1.5 V short of each rail).
    const NodalCircuit::OpAmpMacro m5218 { 1.0e5, 2.5e6, 75.0, 1.5, 7.5, 0.0, 0.0 };
    // 2SC3378GR: a small-signal NPN (the drawing gives no gain figure).
    const NodalCircuit::BjtParams npn2SC3378 { 1.0e-14, 25.85e-3, 300.0, 4.0 };
    // 2SK184GR: the input JFET's Idss / pinch-off (same class of part as the BD-2's 2SK184).
    const NodalCircuit::JfetParams jfet2SK184 { 4.5e-3, -1.2, 0.02 };

    /** One of the drawing's emitter-follower "inductor" branches on an op-amp's (-) leg: C1 to N1; N1 -> rE -> the emitter;
        N1 -> C2 -> the base (rBase to the reference); the collector on the rail, the emitter's 10K to ground. */
    void addBootstrapBranch (NodalCircuit& c, NodalCircuit::Node inv, NodalCircuit::Node rail, NodalCircuit::Node vb,
                             double c1, double rE, double c2, double rBase)
    {
        const auto gnd = NodalCircuit::ground;
        const auto n1 = c.addNode(), b = c.addNode(), e = c.addNode();
        c.addCapacitor (inv, n1, c1);
        c.addResistor (n1, e, rE);
        c.addCapacitor (n1, b, c2);
        c.addResistor (b, vb, rBase);
        c.addBjt (rail, b, e, false, npn2SC3378);
        c.addResistor (e, gnd, 10.0e3);
        c.setInitialGuess (n1, vBias);
        c.setInitialGuess (b, vBias);
        c.setInitialGuess (e, vBias - 0.65);
    }

    /** A linear op-amp (finite gain, a dominant pole from its gain-bandwidth product, an output resistance, no swing limit): for a
        stage whose output cannot reach the rails, so it needs no block of its own. */
    void addLinearOpAmp (NodalCircuit& c, NodalCircuit::Node inPlus, NodalCircuit::Node inMinus, NodalCircuit::Node out, const NodalCircuit::OpAmpMacro& m)
    {
        const auto amp = c.addNode(), px = c.addNode(), ob = c.addNode();
        const double pole = m.gainBandwidth / m.dcGain;
        c.addFiniteGainOpAmp (inPlus, inMinus, amp, m.dcGain, 0.0, 0.0);
        c.addResistor (amp, px, 1.0e6);
        c.addCapacitor (px, NodalCircuit::ground, 1.0 / (2.0 * 3.14159265358979323846 * 1.0e6 * pole));
        c.addFollower (px, ob, 0.0);
        c.addResistor (ob, out, m.outputOhms);
    }
}

MetalZoneStyleDistortionProcessor::MetalZoneStyleDistortionProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };

    auto dist = make ("mt2_dist", "Dist", 0.5f);
    auto high = make ("mt2_high", "High", 0.5f);
    auto middle = make ("mt2_middle", "Middle", 0.5f);
    auto midFreq = make ("mt2_midfreq", "Mid Freq", 0.5f);
    auto low = make ("mt2_low", "Low", 0.5f);
    auto level = make ("mt2_level", "Level", 0.5f);
    distParam = dist.get();
    highParam = high.get();
    middleParam = middle.get();
    midFreqParam = midFreq.get();
    lowParam = low.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "metalzone", "Metal Zone-Style Distortion", "|", std::move (dist));
    group->addChild (std::move (high));
    group->addChild (std::move (middle));
    group->addChild (std::move (midFreq));
    group->addChild (std::move (low));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void MetalZoneStyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const double rOut = m5218.outputOhms;

    // A block's own rail, reference and (for blocks 1..5) the previous block's op-amp output as a source.
    auto rails = [&] (NodalCircuit& c, bool withPrev, int blockIndex, NodalCircuit::Node& rail, NodalCircuit::Node& vb, NodalCircuit::Node& prev)
    {
        rail = c.addNode();
        vb = c.addNode();
        c.addSource (rail, supplyVolts);
        c.addSource (vb, vBias);
        if (withPrev)
        {
            const auto src = c.addNode();
            ch.srcPrev[(size_t) blockIndex] = c.addSource (src, vBias);
            prev = c.addNode();
            c.addResistor (src, prev, rOut);
        }
    };

    // ================================================================ a: input JFET, bypass switch, IC3b
    {
        auto& c = ch.blk[0];
        NodalCircuit::Node rail, vb, unused;
        rails (c, false, 0, rail, vb, unused);
        const auto in = c.addNode();
        ch.srcIn = c.addSource (in, 0.0);

        // R059 10K, C042 0.047 uF into Q011's gate (R058 1M to the reference); Q011 a JFET follower, R060 10K to ground
        const auto nR = c.addNode(), g = c.addNode(), s = c.addNode();
        c.addResistor (in, nR, 10.0e3);
        c.addCapacitor (nR, g, 0.047e-6);
        c.addResistor (g, vb, 1.0e6);
        c.addJfet (rail, g, s, jfet2SK184, 5.0);
        c.addResistor (s, gnd, 10.0e3);

        // C039 1 uF, (R052 100K to the reference), Q009 the series switch (on), (R047 1M), C033 0.015 uF, R043 100K into IC3b's (+)
        const auto nA = c.addNode(), nB = c.addNode(), nC = c.addNode();
        c.addCapacitor (s, nA, 1.0e-6);
        c.addResistor (nA, vb, 100.0e3);
        c.addResistor (nA, nB, jfetSwitchOhms);
        c.addResistor (nB, vb, 1.0e6);
        c.addCapacitor (nB, nC, 0.015e-6);
        c.addResistor (nC, vb, 100.0e3);

        // IC3b: (+) = nC; (-) = m; feedback R044 220K || C032 100 pF; the (-) leg is C034 0.027 uF into Q010's bootstrapped branch
        const auto m = c.addNode(), o = c.addNode();
        c.addOpAmpMacro (nC, m, o, m5218);
        c.addResistor (m, o, 220.0e3);
        c.addCapacitor (m, o, 100.0e-12);
        addBootstrapBranch (c, m, rail, vb, 0.027e-6, 2.2e3, 0.01e-6, 47.0e3);

        for (auto n : { g, nA, nB, nC, m, o })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (s, 3.4);
        ch.out[0] = o;
    }

    // ================================================================ b: IC3a (Dist)
    {
        auto& c = ch.blk[1];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 1, rail, vb, prev);

        // R045 10K; R042 10K and C031 0.047 uF to the reference; C029 0.033 uF; R040 100K into IC3a's (+)
        const auto p = c.addNode(), q = c.addNode();
        c.addResistor (prev, p, 10.0e3);
        c.addResistor (p, vb, 10.0e3);
        c.addCapacitor (p, vb, 0.047e-6);
        c.addCapacitor (p, q, 0.033e-6);
        c.addResistor (q, vb, 100.0e3);

        // IC3a: (-) = m with R041 1K + C030 10 uF to ground; feedback C028 47 pF || (the Dist rheostat + R051 1K)
        const auto m = c.addNode(), o = c.addNode(), mid = c.addNode(), nR = c.addNode();
        c.addOpAmpMacro (q, m, o, m5218);
        c.addResistor (m, nR, 1.0e3);
        c.addCapacitor (nR, gnd, 10.0e-6);
        c.addCapacitor (m, o, 47.0e-12);
        ch.rDist = c.addResistor (m, mid, 1.0e3);
        c.addResistor (mid, o, 1.0e3);

        ch.dbgPrev = prev;
        ch.dbgP = p;
        ch.dbgQ = q;
        for (auto n : { p, q, m, o, mid, prev })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nR, 0.0);
        ch.out[1] = o;
    }

    // ================================================================ c: the clipper and IC4b
    {
        auto& c = ch.blk[2];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 2, rail, vb, prev);

        // C027 10 uF, R033 2K2, two 1SS133 back to back to ground, R032 10K, R031 4K7 + C023 0.015 uF to ground, C021 1 uF, R029 100K
        const auto n27 = c.addNode(), k = c.addNode(), l = c.addNode(), n31 = c.addNode(), m2 = c.addNode();
        c.addCapacitor (prev, n27, 10.0e-6);
        c.addResistor (n27, k, 2.2e3);
        c.addDiode (k, gnd, siIs, siNVt, 4.0e-9);
        c.addDiode (gnd, k, siIs, siNVt, 4.0e-9);
        c.addResistor (k, l, 10.0e3);
        c.addResistor (l, n31, 4.7e3);
        c.addCapacitor (n31, gnd, 0.015e-6);
        c.addCapacitor (l, m2, 1.0e-6);
        c.addResistor (m2, vb, 100.0e3);

        // IC4b: (-) = m; feedback R030 3K3 || C022 47 pF; two bootstrapped branches: (C024 0.015 uF, R034 1K, C025 0.0015 uF, R036 47K)
        // and (C020 0.22 uF, R027 470, C017 0.047 uF, R025 470K)
        const auto m = c.addNode(), o = c.addNode();
        c.addOpAmpMacro (m2, m, o, m5218);
        c.addResistor (m, o, 3.3e3);
        c.addCapacitor (m, o, 47.0e-12);
        addBootstrapBranch (c, m, rail, vb, 0.015e-6, 1.0e3, 0.0015e-6, 47.0e3);
        addBootstrapBranch (c, m, rail, vb, 0.22e-6, 470.0, 0.047e-6, 470.0e3);

        // IC4a: a unity-gain inverter, R028 22K in, R026 22K || C018 10 pF feedback, (+) at the reference. Its input is IC4b's output, which the
        // rails already limit, so it cannot clip in turn and shares this block as a linear op-amp.
        const auto m4a = c.addNode(), o4a = c.addNode();
        c.addResistor (o, m4a, 22.0e3);
        addLinearOpAmp (c, vb, m4a, o4a, m5218);
        c.addResistor (m4a, o4a, 22.0e3);
        c.addCapacitor (m4a, o4a, 10.0e-12);

        for (auto n : { m2, m, o, prev, m4a, o4a })
            c.setInitialGuess (n, vBias);
        for (auto n : { n27, k, l, n31 })
            c.setInitialGuess (n, 0.0);
        ch.dbgIc4b = o;
        ch.out[2] = o4a;
    }

    // ================================================================ e: IC1a with High and Low
    {
        auto& c = ch.blk[3];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 3, rail, vb, prev);

        // R014 22K into node 7 (IC1a's (+)); feedback R015 22K || C010 10 pF from the output to node 8 (IC1a's (-))
        const auto n7 = c.addNode(), n8 = c.addNode(), o = c.addNode();
        c.addResistor (prev, n7, 22.0e3);
        c.addOpAmpMacro (n7, n8, o, m5218);
        c.addResistor (n8, o, 22.0e3);
        c.addCapacitor (n8, o, 10.0e-12);

        // High (VR03b 100K): node 7 -> wiper -> node 8, the wiper to ground through R061 2K2 and C044 0.01 uF
        const auto wH = c.addNode(), tH = c.addNode();
        ch.rHighA = c.addResistor (n7, wH, 1.0e3);
        ch.rHighB = c.addResistor (wH, n8, 1.0e3);
        c.addResistor (wH, tH, 2.2e3);
        c.addCapacitor (tH, gnd, 0.01e-6);

        // Low (VR03a 100K): node 7 -> wiper -> node 8; the wiper into C008 0.22 uF, then R012 2K2 / C009 0.047 uF around IC1b (a follower):
        // node X -> R012 -> IC1b's output; X -> C009 -> IC1b's (+) (R013 100K to the reference)
        const auto wL = c.addNode(), x = c.addNode(), p5 = c.addNode(), vf = c.addNode();
        ch.rLowA = c.addResistor (n7, wL, 1.0e3);
        ch.rLowB = c.addResistor (wL, n8, 1.0e3);
        c.addCapacitor (wL, x, 0.22e-6);
        c.addResistor (x, vf, 2.2e3);
        c.addCapacitor (x, p5, 0.047e-6);
        c.addResistor (p5, vb, 100.0e3);
        c.addFollower (p5, vf, 0.0);

        for (auto n : { n7, n8, o, wH, wL, x, p5, vf, prev })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (tH, 0.0);
        ch.out[3] = o;
    }

    // ================================================================ f: IC2a, Middle / Mid Freq, Level, output stage
    {
        auto& c = ch.blk[4];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 4, rail, vb, prev);

        // C011 1 uF into node Y: R050 330 to node 12, R038 47K into IC2a's (-); IC2a's (+) = Z (R039 1M to the reference)
        const auto y = c.addNode(), n12 = c.addNode(), m = c.addNode(), z = c.addNode(), o = c.addNode();
        c.addCapacitor (prev, y, 1.0e-6);
        c.addResistor (y, n12, 330.0);
        c.addResistor (y, m, 47.0e3);
        c.addResistor (z, vb, 1.0e6);
        c.addOpAmpMacro (z, m, o, m5218);
        c.addResistor (m, o, 47.0e3);
        c.addCapacitor (m, o, 100.0e-12);

        // IC2a's output: C037 1 uF and R049 330 to node 11. Middle (VR02b 100K): node 12 -> wiper -> node 11; the wiper into IC2b (a follower)
        const auto n37 = c.addNode(), n11 = c.addNode(), wM = c.addNode(), fb = c.addNode();
        c.addCapacitor (o, n37, 1.0e-6);
        c.addResistor (n37, n11, 330.0);
        ch.rMidA = c.addResistor (n12, wM, 1.0e3);
        ch.rMidB = c.addResistor (wM, n11, 1.0e3);
        c.addFollower (wM, fb, 0.0);

        // IC2b's output through C036 0.022 uF and R048 2K2 to node 9. Mid Freq (VR02a, two sections, both a rheostat): node 10 -> node 9 and
        // node 10 -> R062 2K2 -> ground; C043 0.0082 uF from node 10 to ground; C038 0.1 uF from node 10 to Z
        const auto n36 = c.addNode(), n9 = c.addNode(), n10 = c.addNode(), n62 = c.addNode();
        c.addCapacitor (fb, n36, 0.022e-6);
        c.addResistor (n36, n9, 2.2e3);
        ch.rFreqA = c.addResistor (n10, n9, 1.0e3);
        ch.rFreqB = c.addResistor (n10, n62, 1.0e3);
        c.addResistor (n62, gnd, 2.2e3);
        c.addCapacitor (n10, gnd, 0.0082e-6);
        c.addCapacitor (n10, z, 0.1e-6);

        // C005 10 uF into the Level pot (VR04 50KA), C004 1 uF, R006 1M to the reference, Q002 (on), C002 10 uF, R004 100K, Q001 an emitter
        // follower (R002 10K), C001 10 uF, R001 1K to the jack (R003 100K across it)
        const auto n4 = c.addNode(), n3 = c.addNode(), nQ = c.addNode(), nJ = c.addNode(), nBase = c.addNode(), nE = c.addNode(), nOutJack = c.addNode(), nJack = c.addNode();
        c.addCapacitor (o, n4, 10.0e-6);
        ch.rLevTop = c.addResistor (n4, n3, 1.0e3);
        ch.rLevBottom = c.addResistor (n3, gnd, 1.0e3);
        c.addCapacitor (n3, nQ, 1.0e-6);
        c.addResistor (nQ, vb, 1.0e6);
        c.addResistor (nQ, nJ, jfetSwitchOhms);
        c.addCapacitor (nJ, nBase, 10.0e-6);
        c.addResistor (nBase, vb, 100.0e3);
        c.addFollower (nBase, nE, 0.65);
        c.addResistor (nE, gnd, 10.0e3);
        c.addCapacitor (nE, nOutJack, 10.0e-6);
        c.addResistor (nOutJack, gnd, 100.0e3);
        c.addResistor (nOutJack, nJack, 1.0e3);
        c.addResistor (nJack, gnd, downstreamLoad);

        for (auto n : { y, n12, m, z, o, n37, n11, wM, fb, nQ, nJ, nBase })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nE, vBias - 0.65);
        c.setInitialGuess (prev, vBias);
        ch.out[4] = o;
        ch.nOut = nJack;
    }
}

void MetalZoneStyleDistortionProcessor::updatePots (const Knobs& k)
{
    // Dist: VR01 250kA as a rheostat in IC3a's feedback. Clockwise = more resistance = more gain.
    const double rDist = juce::jmax (1.0, distPotMax * pots::audio (k.dist));

    // High / Low: 100K linear between node 7 (input) and node 8 (IC1a's (-)); the wiper goes to a shunt to ground. The wiper toward node 8
    // (a large "A" segment) = boost, measured on the network. Middle: node 12 -> wiper -> node 11, the wiper toward 11 (IC2a's output) = boost.
    auto split = [] (double fraction, double& a, double& b)
    {
        a = juce::jmax (1.0, toneA * fraction);
        b = juce::jmax (1.0, toneA - a);
    };
    double highA, highB, lowA, lowB, midA, midB;
    split (k.high, highA, highB);
    split (k.low, lowA, lowB);
    split (k.middle, midA, midB);

    // Mid Freq: VR02a "C" taper, both sections rheostats that move together. Measured on the network: MORE resistance moves the
    // mid boost/cut LOWER (about 200-400 Hz at 50K, above 1.5 kHz at ~0), so clockwise = higher frequency = LESS resistance
    // (an assumption about how the real knob is wired), on the reverse-log law of a "C" pot.
    const double rFreq = juce::jmax (1.0, midFreqPotMax * pots::law (1.0 - k.midFreq, 0.85));

    // Level: VR04 50KA, wiper-to-ground segment
    const double levBottom = juce::jmax (1.0, levelPotMax * pots::audio (k.level));
    const double levTop = juce::jmax (1.0, levelPotMax - levBottom);

    for (auto& ch : channels)
    {
        ch.blk[1].setResistance (ch.rDist, rDist);
        ch.blk[3].setResistance (ch.rHighA, highA);
        ch.blk[3].setResistance (ch.rHighB, highB);
        ch.blk[3].setResistance (ch.rLowA, lowA);
        ch.blk[3].setResistance (ch.rLowB, lowB);
        ch.blk[4].setResistance (ch.rMidA, midA);
        ch.blk[4].setResistance (ch.rMidB, midB);
        ch.blk[4].setResistance (ch.rFreqA, rFreq);
        ch.blk[4].setResistance (ch.rFreqB, rFreq);
        ch.blk[4].setResistance (ch.rLevTop, levTop);
        ch.blk[4].setResistance (ch.rLevBottom, levBottom);
    }
}

double MetalZoneStyleDistortionProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::ic3b:   return ch.blk[0].voltage (ch.out[0]);
        case Probe::ic3a:   return ch.blk[1].voltage (ch.out[1]);
        case Probe::ic4b:   return ch.blk[2].voltage (ch.dbgIc4b);
        case Probe::ic4a:   return ch.blk[2].voltage (ch.out[2]);
        case Probe::ic1a:   return ch.blk[3].voltage (ch.out[3]);
        case Probe::ic2a:   return ch.blk[4].voltage (ch.out[4]);
        case Probe::output: return ch.blk[4].voltage (ch.nOut);
        case Probe::ic3aPrev: return ch.blk[1].voltage (ch.dbgPrev);
        case Probe::ic3aP: return ch.blk[1].voltage (ch.dbgP);
        case Probe::ic3aQ: return ch.blk[1].voltage (ch.dbgQ);
    }
    return 0.0;
}

double MetalZoneStyleDistortionProcessor::debugIterations() const noexcept
{
    double total = 0.0;
    for (const auto& b : channels[0].blk)
        total += b.averageIterations();
    return total;
}

void MetalZoneStyleDistortionProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p)
    {
        s.reset (newSampleRate, 0.02);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedDist, distParam);
    setup (smoothedHigh, highParam);
    setup (smoothedMiddle, middleParam);
    setup (smoothedMidFreq, midFreqParam);
    setup (smoothedLow, lowParam);
    setup (smoothedLevel, levelParam);

    updatePots ({ distParam->get(), highParam->get(), middleParam->get(), midFreqParam->get(), lowParam->get(), levelParam->get() });

    dcOk = true;
    for (auto& ch : channels)
        for (int b = 0; b < numBlocks; ++b)
        {
            if (b > 0)
                ch.blk[(size_t) b].setSource (ch.srcPrev[(size_t) b], ch.blk[(size_t) b - 1].voltage (ch.out[(size_t) b - 1]));
            dcOk = ch.blk[(size_t) b].prepare (newSampleRate) && dcOk;
        }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void MetalZoneStyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDist.setTargetValue (distParam->get());
    smoothedHigh.setTargetValue (highParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedMidFreq.setTargetValue (midFreqParam->get());
    smoothedLow.setTargetValue (lowParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const Knobs k { smoothedDist.getNextValue(), smoothedHigh.getNextValue(), smoothedMiddle.getNextValue(),
                        smoothedMidFreq.getNextValue(), smoothedLow.getNextValue(), smoothedLevel.getNextValue() };

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (k);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.blk[0].setSource (ch.srcIn, (double) data[i]);
            bool ok = ch.blk[0].solveSample();
            for (int b = 1; b < numBlocks; ++b)
            {
                ch.blk[(size_t) b].setSource (ch.srcPrev[(size_t) b], ch.blk[(size_t) b - 1].voltage (ch.out[(size_t) b - 1]));
                ok = ch.blk[(size_t) b].solveSample() && ok;
            }

            data[i] = (float) ch.blk[4].voltage (ch.nOut);

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

void MetalZoneStyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
