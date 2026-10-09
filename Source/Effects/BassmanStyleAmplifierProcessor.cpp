#include "BassmanStyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <cstdlib>

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace tubeamp;

    // ---- supply (docs/circuits/Bassman5F6A.md, "Power supply") ----
    constexpr double railPlatesNominal = 452.0;   // +452 V, output-transformer centre tap
    constexpr double railScreensNominal = 450.0;  // +450 V, after the choke, through 470 ohm to each screen
    constexpr double railPhaseInverterNominal = 385.0;
    constexpr double railPreampNominal = 325.0;
    constexpr double rectifierResistance = 330.0; // GZ34 + power transformer, set so full drive sags the screens ~57 V
    constexpr double chokeResistance = 100.0;
    constexpr double chokeInductance = 10.0;      // H, 13 Hz ring with the filter capacitors (Kuehnel's sag graph)
    constexpr double idlePlateCurrent = 0.058;
    constexpr double idleScreenCurrent = 0.006;
    constexpr double phaseInverterNodeCurrent = 0.0078; // calibrates the 4.7k drop to the schematic's +385 V
    constexpr double preampNodeCurrent = 0.006;         // the 10k drop to +325 V
    constexpr double screenResistor = 470.0;

    // ---- reduced-order power stage (2026-09-27): calibration data, BM_POWERCAL in the test file ----
    // Plate rail vs. the post-tone-stack drive PEAK, measured on the full reference model with a slow (settled) sine at
    // each level so the supply reaches its own quasi-equilibrium (Power Drive at max, Volume Normal at 0.8, matched 8 ohm
    // speaker, Input Normal). Same methodology as the Super Lead's own SL_POWERCAL (docs/circuits/SuperLead1959.md).
    constexpr int bmSagPoints = 24;
    constexpr double bmSagDrive[bmSagPoints] = { 0.002991, 0.007467, 0.014933, 0.029867, 0.059735, 0.104539, 0.149344, 0.224025,
                                                  0.298710, 0.448100, 0.597503, 0.821642, 1.045798, 1.344706, 1.793071, 2.390669,
                                                  2.987568, 4.463159, 7.051008, 10.582891, 15.200820, 21.200175, 27.414895, 32.667039 };
    constexpr double bmSagRail[bmSagPoints] = { 451.91, 451.90, 451.90, 451.90, 451.88, 451.86, 451.84, 451.78,
                                                 451.72, 451.56, 451.36, 450.98, 450.51, 449.75, 448.31, 445.94,
                                                 443.11, 435.16, 431.17, 420.08, 413.63, 410.52, 408.40, 406.95 };

    /** Piecewise-linear lookup through the real measured points above (see SuperLeadStyleAmplifierProcessor's own
        sagRailLookup() for why this is a table, not a fitted shape). */
    double sagRailLookup (double drivePeak) noexcept
    {
        if (drivePeak <= bmSagDrive[0])
            return bmSagRail[0];
        if (drivePeak >= bmSagDrive[bmSagPoints - 1])
            return bmSagRail[bmSagPoints - 1];
        int i = 0;
        while (i < bmSagPoints - 2 && bmSagDrive[i + 1] < drivePeak)
            ++i;
        const double t = (drivePeak - bmSagDrive[i]) / (bmSagDrive[i + 1] - bmSagDrive[i]);
        return bmSagRail[i] + t * (bmSagRail[i + 1] - bmSagRail[i]);
    }

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7()
    {
        return {}; // Koren's ECC83 set
    }

    KorenTriode::Parameters triode12AY7()
    {
        // Fitted to Kuehnel's operating point for the Bassman's 12AY7 (150 V, 1.5 mA, mu 49, rp 29.9k).
        KorenTriode::Parameters p;
        p.mu = 54.3;
        p.kg1 = 986.0;
        p.kp = 172.6;
        return p;
    }

    KorenPentode::Parameters pentode5881()
    {
        // Koren's plain 6L6GC set is too linear around the Bassman's -48 V bias (its gain at the operating point is
        // ~1.7x Kuehnel's composite curve). mu, Kg1 and Kp are fitted so the push-pull stage's static transfer curve
        // through the 4k output transformer reproduces the composite curve of Kuehnel's analysis (grid +-48 V ->
        // +-13 V into 2 ohm, gain 0.18 at the centre rising to 0.27 at full drive); see the doc.
        KorenPentode::Parameters p;
        p.mu = 15.96;
        p.kg1 = 889.4;
        p.kp = 23.2;
        return p;
    }

    // Grid-plate capacitance (the Miller effect; the grid-cathode and plate-cathode ones are far too small to matter
    // against the source impedances here and cost solver state).
    constexpr double cgp = 1.7e-12;

    // Output transformer 45249: 4k plate-to-plate, 2 ohm secondary.
    constexpr double primaryHalfInductance = 6.25;        // H per half (25 H plate to plate)
    constexpr double halfToSecondaryTurns = 11.18;        // (4000/8)^0.5 / 2: a transformer with an 8 ohm secondary (see the doc)
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.9992;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.24;
    constexpr double feedbackResistor = 54.0e3;           // 27k on the original 2 ohm tap: the same fraction of the output

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };

    // Integration of the power section: more damped than the pedals' 0.6 (a trapezoid rule rings at Nyquist on a stiff,
    // high-gain circuit when a tube cuts off; see the doc, "Instability").
    constexpr double powerTheta = 0.9;

}

BassmanStyleAmplifierProcessor::BassmanStyleAmplifierProcessor()
{
    // Input: which of the amp's two front-panel jacks the guitar is actually patched into -- Normal, Bright, or both at
    // once via a jumper cable between them (the real amp's own trick for blending the two channels; the user's own name
    // for it, "jumped"). Not plugging a channel in means its Volume knob does nothing, same as on the real amp.
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "bm_input", "Input", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("Normal"); case 2: return juce::String ("Bright"); default: return juce::String ("Jumped"); }
        }));
    auto normal = std::make_unique<juce::AudioParameterFloat> (
        "bm_vol_normal", "Volume (Normal)", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto bright = std::make_unique<juce::AudioParameterFloat> (
        "bm_vol_bright", "Volume (Bright)", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "bm_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto middle = std::make_unique<juce::AudioParameterFloat> (
        "bm_middle", "Middle", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "bm_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto presence = std::make_unique<juce::AudioParameterFloat> (
        "bm_presence", "Presence", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "bm_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto power = std::make_unique<juce::AudioParameterFloat> (
        "bm_power", "Power Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto bias = std::make_unique<juce::AudioParameterFloat> (
        "bm_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto feel = std::make_unique<juce::AudioParameterFloat> (
        "bm_tube_feel", "Tube Feel", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "bm_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    volNormalParam = normal.get();
    volBrightParam = bright.get();
    trebleParam = treble.get();
    middleParam = middle.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "bassman", "Bassman-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (normal));
    group->addChild (std::move (bright));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    // Page 2 (a sub-group: see EffectProcessor::getParameterPages()).
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("bassman_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcVcc = 0, srcInNormal = 0, srcInBright = 0;
        int rVolTop[2] {}, rVolBot[2] {};
        NodalCircuit::Node mix = 0, plate2 = 0, follower = 0, brightPlate = 0;
    };

    /** Both channels' input triodes, volume pots, mixing resistors and the gain stage; the cathode follower is an
        ideal follower with a DC drop of `followerDrop`. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc = c.addNode(), inNormal = c.addNode(), inBright = c.addNode();
        b.srcVcc = c.addSource (vcc, railPreampNominal);
        b.srcInNormal = c.addSource (inNormal, 0.0);
        b.srcInBright = c.addSource (inBright, 0.0);

        const auto kv1 = c.addNode();
        c.addResistor (kv1, gnd, 820.0);
        c.addCapacitor (kv1, gnd, 250.0e-6);
        c.setInitialGuess (kv1, 2.3);

        b.mix = c.addNode();
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto g = c.addNode(), p = c.addNode(), cp = c.addNode(), w = c.addNode();
            c.addResistor (ch == 0 ? inNormal : inBright, g, 68.0e3); // grid stopper -- each channel's OWN jack now
            c.addTriode (p, g, kv1, triode12AY7());
            c.addCapacitor (g, p, cgp);
                c.addResistor (vcc, p, 100.0e3);
            c.addCapacitor (p, cp, 0.02e-6);
            b.rVolTop[ch] = c.addResistor (cp, w, 500.0e3);
            b.rVolBot[ch] = c.addResistor (w, gnd, 500.0e3);
            if (ch == 1)
                c.addCapacitor (cp, w, 100.0e-12);          // the bright channel's treble bypass
            c.addResistor (w, b.mix, 270.0e3);
            c.setInitialGuess (p, 150.0);
            if (ch == 1)
                b.brightPlate = p;
        }

        const auto kv2 = c.addNode();
        b.plate2 = c.addNode();
        c.addTriode (b.plate2, b.mix, kv2, triode12AX7());
        c.addCapacitor (b.mix, b.plate2, cgp);
        c.addResistor (vcc, b.plate2, 100.0e3);
        c.addResistor (kv2, gnd, 820.0);
        c.setInitialGuess (b.plate2, 180.0);
        c.setInitialGuess (kv2, 1.2);

        b.follower = c.addNode();
        c.addFollower (b.plate2, b.follower, followerDrop);
        return b;
    }
}

void BassmanStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        const auto nl = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();
        ch.sD = c.addNode();

        const double dcDrop = rectifierResistance * (idlePlateCurrent + idleScreenCurrent + phaseInverterNodeCurrent + preampNodeCurrent);
        ch.srcVoc = c.addSource (vo, railPlatesNominal + dcDrop);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 40.0e-6);           // two 20 uF at the plate node
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 20.0e-6);
        c.addResistor (ch.sB, ch.sC, 4.7e3);
        c.addCapacitor (ch.sC, gnd, 20.0e-6);
        c.addResistor (ch.sC, ch.sD, 10.0e3);
        c.addCapacitor (ch.sD, gnd, 8.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -idleScreenCurrent);
        ch.iC = c.addCurrentSource (ch.sC, -phaseInverterNodeCurrent);
        ch.iD = c.addCurrentSource (ch.sD, -preampNodeCurrent);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railScreensNominal);
        c.setInitialGuess (ch.sC, railPhaseInverterNominal);
        c.setInitialGuess (ch.sD, railPreampNominal);
    }

    // ================================================================ preamp
    // The follower's DC drop needs the gain stage's DC plate voltage, which does not depend on the follower: build
    // once with a placeholder, read the plate, solve the follower, build again.
    {
        auto probe = buildPreamp (ch.pre, 0.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plate2);
        const double drop = plate - cathodeFollowerDc (railPreampNominal, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop);
        ch.pSrcVcc = b.srcVcc;
        ch.pSrcInNormal = b.srcInNormal;
        ch.pSrcInBright = b.srcInBright;
        for (int i = 0; i < 2; ++i)
        {
            ch.rVolTop[i] = b.rVolTop[i];
            ch.rVolBot[i] = b.rVolBot[i];
        }
        ch.pMix = b.mix;
        ch.pPlate2 = b.plate2;
        ch.pFollower = b.follower;
        ch.pBrightPlate = b.brightPlate;
    }

    // ================================================================ tone stack (ALWAYS built and solved: a real linear
    // circuit either way, so it costs nothing extra in reducedOrder mode -- see the header's "reduced-order power stage" note)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        // Tone stack (the Bassman / Marshall "TMB" stack): 250 pF treble bypass, 56k slope resistor, 250k treble,
        // 1M bass, 25k middle, two 0.02 uF capacitors. DC-coupled to the follower's cathode.
        const auto ti = c.addNode(), top = c.addNode(), slope = c.addNode(), n1 = c.addNode(), n2 = c.addNode();
        ch.wToneIn = ti;
        ch.wSlope = slope;
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 250.0e-12);
        c.addResistor (ti, slope, 56.0e3);
        ch.rTrebleTop = c.addResistor (top, ch.wTone, 125.0e3);
        ch.rTrebleBottom = c.addResistor (ch.wTone, n1, 125.0e3);
        c.addCapacitor (slope, n1, 0.02e-6);
        ch.rBass = c.addResistor (n1, n2, 500.0e3);
        c.addCapacitor (slope, n2, 0.02e-6);
        ch.rMid = c.addResistor (n2, gnd, 12.5e3);
    }

    // ================================================================ phase inverter, power amp -- the FULL reference
    // netlist only (reducedOrder replaces all of this with behavioralPowerStage(), fitted to this same circuit's own
    // measured output -- docs/circuits/Bassman5F6A.md)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), biasRail = c.addNode();
        ch.wSrcPi = c.addSource (vpi, railPhaseInverterNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (biasRail, -48.0);

        // Phase inverter: 12AX7 long-tailed pair. Cathodes -> 470 -> junction (grid leaks return here) -> 10k.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), bn = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = bn;
        c.addCapacitor (ch.wTone, g1, 0.02e-6);
        c.addResistor (g1, bn, 1.0e6);
        c.addResistor (g2, bn, 1.0e6);
        c.addTriode (pa, g1, k, triode12AX7());
        c.addTriode (pb, g2, k, triode12AX7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, bn, 470.0);
        c.addResistor (bn, gnd, 10.0e3);
        c.setInitialGuess (pa, 236.0);
        c.setInitialGuess (pb, 230.0);
        c.setInitialGuess (k, 34.0);
        c.setInitialGuess (bn, 32.5);
        c.setInitialGuess (g1, 32.5);
        c.setInitialGuess (g2, 32.5);

        // Power amplifier: coupling, grid leaks to the fixed bias, two beam tetrodes, output transformer.
        const auto g3 = c.addNode(), g4 = c.addNode(), nb = c.addNode();
        ch.wPowerGridA = g3;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.1e-6);
        c.addCapacitor (pb, g4, 0.1e-6);
        c.addResistor (g3, nb, 220.0e3);
        c.addResistor (g4, nb, 220.0e3);
        c.addResistor (nb, biasRail, 11.83e3);   // 15k || 56k of the bias supply
        c.addCapacitor (nb, gnd, 8.0e-6);
        ch.penA = c.addPentode (pp1, g3, gnd, pentode5881(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4, gnd, pentode5881(), railScreensNominal);

        // Distributed capacitance of the primary (and of the wiring to the tubes), plate to plate: what limits an
        // inductive kick and sets the transformer's top-end resonance (~130 kHz alone with the 4k load; with the speaker's voice-coil inductance it resonates at 15-20 kHz).
        c.addCapacitor (pp1, pp2, 250.0e-12);
        // Core and copper losses of the transformer (a resistance across the primary) and each plate's capacitance to
        // ground (tube, wiring, winding). They damp the resonance between the speaker's voice-coil inductance and the
        // transformer: without them the closed loop with a 16 ohm speaker breaks into oscillation after a few notes
        // (found in a 10 s soak with plucked notes), and the margin is thin at 8 ohm with Presence up.
        c.addResistor (pp1, pp2, 47.0e3);
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        // Windings (a -> b): half 1 (centre tap -> plate 1), half 2, secondary. Both halves start at the centre tap,
        // so they are wound against each other: the DC plate currents cancel in the core.
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        c.addResistor (sw, ch.wOut, secondaryResistance);
        // The speaker: Re + Le in series with the cone resonance (Rp || Lp || Cp). Values for the 8 ohm speaker here;
        // applySpeaker() changes them for 4 and 16.
        {
            const auto sm = speakerModel (8.0);
            const auto na = c.addNode(), nbb = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na, nbb } }, { sm.le });
            ch.rSpkRp = c.addResistor (nbb, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb, gnd, sm.cp);
        }

        // Negative feedback: 27k from the speaker terminal into the presence network, coupled into the second grid.
        const auto fp = c.addNode(), w = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, w, 2.5e3);
        ch.rPresBottom = c.addResistor (w, gnd, 2.5e3);
        c.addCapacitor (w, gnd, 0.1e-6);
        // Stray capacitance in the feedback path (~25 kHz with the 4-5k the presence network leaves at this node).
        // Without a pole here the loop's gain does not fall before this model's Nyquist, and an inductive speaker
        // load makes the closed loop unstable at 48 kHz (it is stable at 96 kHz): the analogue amp is protected by
        // the transformer and tube capacitances that this discrete-time model cannot represent above 24 kHz.
        c.addCapacitor (fp, gnd, 1.5e-9);
        c.addCapacitor (fp, g2, 0.1e-6);

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (g3, -48.0);
        c.setInitialGuess (g4, -48.0);
        c.setInitialGuess (nb, -48.0);
    }
}

void BassmanStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (reducedOrder)
        return; // no physical speaker RLC network exists in this mode (see the header's "reduced-order power stage" note)
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 2, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, { 1.0 / sm.le });
    ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 / sm.lp });
}

void BassmanStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Volume: 1M audio taper (pots::audio -- 15% at half rotation, what the real part does). The wiper divides the pot.
    const double vols[2] = { k.volNormal, k.volBright };
    // Treble 250k linear, Bass 1M audio (used as a rheostat: more resistance = more bass), Middle 25k linear
    // (rheostat to ground: more resistance = more mids), Presence 5k linear with a 0.1 uF cap on the wiper.
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midR = juce::jmax (1.0, 25.0e3 * k.middle);
    const double presBottom = juce::jmax (1.0, 5.0e3 * k.presence);
    const double presTop = juce::jmax (1.0, 5.0e3 - presBottom);

    // Bias: -38 V (hot) .. -58 V (cold), noon = the schematic's -48 V.
    const double biasVolts = -38.0 - 20.0 * k.bias;
    // Tube Feel: the supply's series resistance (sag) and the feedback fraction. 1 = the real amp; 0 = a stiff supply
    // (5% of the sag) and 2.5x the feedback.
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        for (int i = 0; i < 2; ++i)
        {
            const double bottom = juce::jmax (1.0, 1.0e6 * pots::audio (vols[i]));
            ch.pre.setResistance (ch.rVolBot[i], bottom);
            ch.pre.setResistance (ch.rVolTop[i], juce::jmax (1.0, 1.0e6 - bottom));
        }
        ch.power.setResistance (ch.rTrebleTop, trebleTop);
        ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.power.setResistance (ch.rBass, bassR);
        ch.power.setResistance (ch.rMid, midR);
        // reducedOrder: Presence/feedback/bias don't exist in the tone-stack-only power circuit (see the header's
        // "reduced-order power stage" note) -- none of Presence, Bias or Tube Feel are reproduced yet (a documented gap).
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresTop, presTop);
            ch.power.setResistance (ch.rPresBottom, presBottom);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setSource (ch.wSrcBias, biasVolts);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    // A lighter load gives more volts and a heavier one fewer; this compensates so 4 / 8 / 16 ohm changes the sound, not
    // the loudness (measured full-drive volts ~ z^0.83) on the FULL reference netlist. reducedOrder has no physical
    // speaker impedance left to compensate for (see SuperLeadStyleAmplifierProcessor's own fix for this exact bug,
    // 2026-09-27) -- applying this same factor there would make 4 ohm louder than 16 instead of matching them.
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 8.0, -0.8);
}

void BassmanStyleAmplifierProcessor::recover (Channel& ch) const
{
    // A hard-driven or oscillating output stage can push the operating point somewhere Newton cannot come back from
    // (the state is then frozen and every sample retries and fails). Go back to the settled state and re-apply the
    // knobs. The output stays continuous across it (Channel::declick): a stuck output and a pegged CPU are worse than a
    // short thump.
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void BassmanStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    // The supply sees what two output tubes can physically draw (a 5881 peaks around 0.4 A): a solver excursion in the
    // power block must not be able to turn into an absurd current that wrecks the rails of every other stage.
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.8, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.15, ch.sumScreen / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    // ... and a rectified supply can neither reverse nor exceed its open-circuit voltage.
    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    // reducedOrder: wSrcCt/wSrcPi don't exist in the tone-stack-only power circuit (see the header's "reduced-order power
    // stage" note) -- behavioralPowerStage() has its own, separate sag lookup, fitted directly from real measured data.
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 520.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 470.0));
    }
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sD, 400.0));
    ch.vScreen = rail (ch.sB, 520.0);
}

double BassmanStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (toneVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, toneVoltage);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double BassmanStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::gainStagePlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::mixNode: return ch.pre.voltage (ch.pMix);
        case Probe::brightPlate: return ch.pre.voltage (ch.pBrightPlate);
        case Probe::followerOut: return ch.pre.voltage (ch.pFollower);
        case Probe::toneStackOut: return ch.power.voltage (ch.wTone);
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return ch.power.voltage (ch.wTail);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::powerGridA: return ch.power.voltage (ch.wPowerGridA);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double BassmanStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void BassmanStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    if (reducedOrder)
        return; // no physical speaker network to compare against a resistor in this mode
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setInductorInverse (ch.grpSpkLe, { 1.0e6 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 });
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2; // never equal to a knob position, but no re-apply either: see updatePots()
    resistiveLoadForced = true;
}

void BassmanStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return; // no feedback resistor node exists in this mode (see the header's "reduced-order power stage" note)
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double BassmanStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0; // no pentode devices exist in this mode
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double BassmanStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void BassmanStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedVolNormal.reset (newSampleRate, 0.02);
    smoothedVolNormal.setCurrentAndTargetValue (volNormalParam->get());
    smoothedVolBright.reset (newSampleRate, 0.02);
    smoothedVolBright.setCurrentAndTargetValue (volBrightParam->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (trebleParam->get());
    smoothedMiddle.reset (newSampleRate, 0.02);
    smoothedMiddle.setCurrentAndTargetValue (middleParam->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bassParam->get());
    smoothedPresence.reset (newSampleRate, 0.02);
    smoothedPresence.setCurrentAndTargetValue (presenceParam->get());
    smoothedOutput.reset (newSampleRate, 0.02);
    smoothedOutput.setCurrentAndTargetValue (outputParam->get());
    smoothedPower.reset (newSampleRate, 0.02);
    smoothedPower.setCurrentAndTargetValue (powerParam->get());
    smoothedBias.reset (newSampleRate, 0.05);
    smoothedBias.setCurrentAndTargetValue (biasParam->get());
    smoothedFeel.reset (newSampleRate, 0.05);
    smoothedFeel.setCurrentAndTargetValue (tubeFeelParam->get());

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + phaseInverterNodeCurrent + preampNodeCurrent;
    appliedSpeaker = 1; // the circuits are built with the 8 ohm speaker
    updatePots ({ volNormalParam->get(), volBrightParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(),
                  presenceParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        // The supply first (idle currents), then the amplifier on its rails; then the supply again with the
        // amplifier's own idle currents so the whole thing starts settled.
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        ch.followerDc = ch.pre.voltage (ch.pFollower);

        ch.power.setSource (ch.wSrcCf, ch.followerDc);
        // The follower's DC level reaches only the two nodes on this side of the tone stack's capacitors; starting them
        // there keeps the DC relaxation from charging the capacitors through a violent step.
        ch.power.setInitialGuess (ch.wToneIn, ch.followerDc);
        ch.power.setInitialGuess (ch.wSlope, ch.followerDc);
        ch.vScreen = ch.supply.voltage (ch.sB);
        // reducedOrder: none of the PI/pentode handles below exist in the tone-stack-only power circuit (see the header's
        // "reduced-order power stage" note); ipA/ipB/isA/isB stay 0, decaying the supply's assumed power-tube draw toward
        // 0 -- a reasonable idle point for the PREAMP's own rails, which is all this supply model needs here.
        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
        }
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.power.solveSample();

        if (! reducedOrder)
        {
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
        }
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB));
        ch.supply.setCurrentSource (ch.iB, -(isA + isB));
        // Open-circuit voltage such that the rails sit at the schematic's values with THIS model's idle currents.
        idleSupplyCurrent = ipA + ipB + isA + isB + phaseInverterNodeCurrent + preampNodeCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        ch.screenDropA = screenResistor * isA;
        ch.screenDropB = screenResistor * isB;
        ch.vScreen = ch.supply.voltage (ch.sB);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        }
        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true;
    channel1Stale = false;
    identicalRun = 0;
}

void BassmanStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    // Dual-mono shortcut, exactly as in the other circuit processors (see HM2StyleDistortionProcessor::process).
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

    smoothedVolNormal.setTargetValue (volNormalParam->get());
    smoothedVolBright.setTargetValue (volBrightParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get()); // 0 Normal, 1 Jumped, 2 Bright
    const bool inputConnectsNormal = inputChoice != 2;
    const bool inputConnectsBright = inputChoice != 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vn = smoothedVolNormal.getNextValue();
        const float vb = smoothedVolBright.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vn, vb, tr, mi, ba, pr, pw, bi, fe, speakerChoice });
        }

        // Power Drive: a master volume between the preamp and the tone stack / phase inverter (amplitude = knob^2, audio
        // taper). The excursion of the cathode follower scales; its DC and the tone stack's impedances do not.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

        // Output control: -30 dB .. 0 dB at noon .. +12 dB.
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            // Input selector (Normal / Jumped / Bright): the same guitar cable, patched into one or both channel jacks
            // -- exactly the real amp's "jumper" trick (a patch cable Bright-1 -> Normal-2 drives both channels from
            // one guitar). Feeding only one channel's grid means the other channel's Volume knob does nothing, as on
            // the real amp with nothing plugged into its jack.
            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcInNormal, inputConnectsNormal ? x : 0.0);
            ch.pre.setSource (ch.pSrcInBright, inputConnectsBright ? x : 0.0);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            ch.power.setSource (ch.wSrcCf, ch.followerDc + masterGain * cathodeFollowerGain * (ch.pre.voltage (ch.pFollower) - ch.followerDc));
            if (! reducedOrder)
            {
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
            }
            const bool ok2 = ch.power.solveSample(); // reducedOrder: a pure linear circuit (the tone stack only) -- always converges
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            // reducedOrder: no pentode devices exist, so there is no current to read back into the shared supply model --
            // its rails just settle toward the (small) preamp-only draw; behavioralPowerStage() below has its own,
            // separate sag lookup fitted directly from the full model's real behaviour.
            if (! reducedOrder)
            {
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.screenDropA += 0.3 * (screenResistor * isA - ch.screenDropA);
                ch.screenDropB += 0.3 * (screenResistor * isB - ch.screenDropB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            // A converged solve can still land on a state no amplifier reaches (a speaker terminal at hundreds of volts, a
            // plate below ground): the output stage has run away between two samples. Such a sample is a failure, and
            // the output holds its last value instead of printing the excursion. reducedOrder's behavioralPowerStage() is
            // a bounded saturating function -- there is no Newton solve left to have a bad day.
            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone)) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));
            if (chIdx == 0 && ! sane && okPre && ok2)
            {
                ++sanityRejects; // the solver converged; only this check rejected the sample
                if (std::isfinite (speakerVolts))
                    worstRejectedVolts = juce::jmax (worstRejectedVolts, std::abs (speakerVolts));
            }

            if (ok)
            {
                ch.failStreak = 0;
                // Keep the recovery snapshot recent: a recovery mid-note should land back near what the amp was actually
                // doing, not back at prepare()'s silent idle point (see the Channel struct's comment).
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre.saveDynamicState (ch.preRest);
                    ch.power.saveDynamicState (ch.powerRest);
                    ch.supply.saveDynamicState (ch.supplyRest);
                }
            }
            // 4 samples (was 48, then 8): a failing sample already holds the output (below), so a long streak is frozen
            // sound AND, at up to ~300 Newton iterations each before NodalCircuit's own fallback cap kicks in, the thing
            // that made one block cost 13 ms and drop out (see docs/circuits/Bassman5F6A.md, "A hot pedal into the amp").
            // Lowered again 2026-09-22 chasing the same failure mode at more extreme drive (Volume Normal/Bright and
            // Power Drive all maxed, "roda o hm2 tudo no maximo"): worst block 4.46 -> 2.91 ms with Input = Normal (the
            // one channel a player normally uses). The restore is de-clicked, so recovering earlier costs nothing audible.
            else if (++ch.failStreak >= 4)
            {
                recover (ch); // memory only: the knobs (and so the reduced model) stay as they are
                ch.restRefreshCounter = 0; // don't immediately overwrite the just-restored snapshot with the same data
            }

            double out = ch.lastEmitted;
            if (sane)
            {
                out = speakerVolts * outputScale * outGain * speakerGain;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - out; // continuity with the last sample that went out
                    ch.alignOutput = false;
                }
                out += ch.declick;
                ch.declick *= declickDecay;
            }
            ch.lastEmitted = out;
            data[i] = (float) out;

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

void BassmanStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
