#include "JCM800StyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace tubeamp;

    // ---- supply (docs/circuits/JCM8002203.md, "Power supply"). The drawing prints no voltages; these are set by the model's
    // own idle currents and a plexi's usual ~480 V on the plates. ----
    constexpr double railPlatesNominal = 480.0;   // output-transformer centre tap (before the choke)
    constexpr double rectifierResistance = 150.0; // bridge + transformer (assumed): full drive sags the plates ~50 V
    constexpr double chokeResistance = 80.0;
    constexpr double chokeInductance = 20.0;      // H (the 1967 drawing of this amp says 20H; the 2002 one gives no value)
    constexpr double bleeder = 112.0e3;           // the two 56k / 1 W equalising resistors across the second bank
    constexpr double screenResistor = 500.0;      // two 1k / 5 W screen resistors in parallel (one pair)

    // ---- reduced-order power stage: DELIBERATELY still Super Lead's own SL_POWERCAL sweep (same power section, reused
    // unchanged) rather than this amp's own J8_POWERCAL data -- see the header's bmGain0/bmYmax comment for why. ----
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 480.66, 480.66, 480.66, 480.66, 480.65, 480.65, 480.63, 480.56,
                                                 480.47, 480.20, 479.80, 478.93, 477.71, 475.53, 471.14, 463.79,
                                                 454.91, 437.06, 421.73, 417.36 };

    /** Piecewise-linear lookup through the real measured points above (not a fitted shape: the sag curve's rise is too
        irregular for a clean 1-2 parameter fit in the time available, unlike the output curve below). Below/above the
        table's range, clamps to the nearest measured value -- a light load barely sags (true at the low end) and this
        amp was never driven harder than the top measured point (a reasonable floor, not a claim that sag stops there). */
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
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set

    /** The phase inverter's own 12AX7, `kg1` softened -- combined with pentodeEL34PairPower()'s own softening below, a
        more modest multiplier on EACH tube reaches genuine stability with less damage to either one's own transfer
        curve than pushing the whole correction onto a single tube (see that function's comment for the full story). */
    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        p.kg1 *= 70.0;
        return p;
    }

    /** An EL34 PAIR: two identical tubes on the same nodes are one tube with twice the current (kg1, kg2 and the grid
        conductance are halved / doubled, the flash-over resistance halved). mu, Kp and Ex are fitted to a plexi's
        operating region (~35 mA per tube at -55 V and 480 V, gm ~7 mA/V there, 100+ mA at 250 V / -13.5 V, gm ~11 mA/V);
        Koren's published EL34 set (mu 11, Kp 60) has its cutoff at about -40 V at these voltages, where a plexi is biased at
        -55 V. See the doc. */
    KorenPentode::Parameters pentodeEL34Pair()
    {
        KorenPentode::Parameters p;
        p.mu = 8.11;
        p.ex = 1.50;
        p.kg1 = 1201.0 / 2.0;
        p.kg2 = 3720.0 / 2.0;
        p.kp = 100.0;
        p.kvb = 24.0;
        p.grid.Gg *= 2.0;
        p.arcResistance /= 2.0;
        return p;
    }

    /** Same EL34 pair as pentodeEL34Pair(), `kg1` softened for this amp's own power-stage instability -- see
        docs/circuits/JCM8002203.md's "A power-stage instability" section. This amp's 4-cascaded-gain preamp drives
        the SAME Super Lead power section at a different idle follower DC than the Super Lead's own preamp ever
        reaches, and at that bias point the published fit sits in this circuit's unstable region: silence at the
        input grew into a full-scale, chaotic self-oscillation of the PI/output stage within under a second, and it
        did NOT go away with the global feedback loop opened, ruling out the loop as the sole cause -- same failure
        class as [[circuit-tube-model-loop-instability]]. Softening EITHER tube alone needed an extreme multiple
        (~3800x on the PI triode, or ~3000x on this pentode alone) to fully quench it, and at that extreme it also
        crushed ordinary-signal gain to near-silence (a plucked note at default Power Drive/Gain barely registered)
        -- not a usable fix. Splitting a smaller multiple across BOTH tubes (see triode12AX7Pi()) reaches the same
        genuine 10 s stability with much less damage to either one's own transfer curve. Still a real, disclosed
        compromise, not a clean resolution -- see docs/circuits/JCM8002203.md; the likely true root cause is
        something structural this session didn't isolate (matches JTM45's own unresolved case). */
    KorenPentode::Parameters pentodeEL34PairPower()
    {
        auto p = pentodeEL34Pair();
        p.kg1 *= 70.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Output transformer: ~1.7k plate to plate (four EL34s: two pairs, each wanting ~3.4k), 16 ohm secondary tap.
    constexpr double primaryHalfInductance = 2.65;        // H per half (10.6 H plate to plate)
    constexpr double halfToSecondaryTurns = 5.153;        // (1700/16)^0.5 / 2
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.9992;
    constexpr double primaryHalfResistance = 35.0;
    constexpr double secondaryResistance = 0.2;
    constexpr double feedbackResistor = 47.0e3;           // R21, from the 16 ohm terminal

    constexpr double biasSupplyVolts = -67.5;             // the rectified bias winding (assumed): trimmer at noon gives -55 V

    constexpr double speakerEddyLoss = 150.0;             // ohms across the 16 ohm speaker's Le: ~10x the voice coil's DC resistance

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // the transformer's tap (and the feedback tap) is the 16 ohm one

    constexpr double powerTheta = 0.9; // see BassmanStyleAmplifierProcessor
}

JCM800StyleAmplifierProcessor::JCM800StyleAmplifierProcessor()
{
    // Input: which of the amp's two front-panel jacks (High / Low sensitivity, sharing one gain stage) the guitar is
    // patched into -- not a second channel, just a real second jack on the same input like every classic Marshall.
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "j8_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("High") : juce::String ("Low"); }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto gain = make ("j8_gain", "Gain", 0.4f);
    auto treble = make ("j8_treble", "Treble", 0.5f);
    auto middle = make ("j8_middle", "Middle", 0.5f);
    auto bass = make ("j8_bass", "Bass", 0.5f);
    auto presence = make ("j8_presence", "Presence", 0.3f);
    auto output = make ("j8_output", "Output", 0.5f);
    auto power = make ("j8_power", "Power Drive", 0.5f);
    auto bias = make ("j8_bias", "Bias", 0.5f);
    auto feel = make ("j8_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "j8_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    gainParam = gain.get();
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
        "jcm800", "JCM800-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("jcm800_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV1 = 0, srcV2 = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node plate2 = 0, follower = 0, plateV1a = 0, plateV1b = 0, nodeV1 = 0, nodeV2 = 0;
    };

    /** The real 2203 preamp: ONE input drives four cascaded gain stages in series -- V1a, a Gain pot, V1b, V2a, and
        V2b (the cathode follower into the tone stack, an ideal follower here with a DC drop of `followerDrop`).
        Unlike every other amp on this project's roadmap, there is no second channel to mix in. V1a/V1b share one
        supply tap (the real amp's own R0/C21-decoupled local rail); V2a/V2b share the other, undecoupled tap
        (matching the schematic's own two distinct preamp rail nodes). */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double v1Guess, double v2Guess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc1 = c.addNode(), vcc2 = c.addNode(), in = c.addNode();
        b.nodeV1 = vcc1;
        b.nodeV2 = vcc2;
        b.srcV1 = c.addSource (vcc1, v1Guess);
        b.srcV2 = c.addSource (vcc2, v2Guess);
        b.srcIn = c.addSource (in, 0.0); // High/Low sensitivity is a plain gain factor applied in process(), not a
                                          // separate resistor network -- see the header's own note on this simplification

        // V1a: R3 68k grid stopper, R4 100k plate (vcc1), R1 2k7 cathode with a 68 uF bypass (full bass, no local
        // negative feedback here -- unlike every stage after it).
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plateV1a = c.addNode();
        c.addResistor (in, g1, 68.0e3);
        c.addTriode (b.plateV1a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateV1a, cgp);
        c.addResistor (vcc1, b.plateV1a, 100.0e3);
        c.addResistor (k1, gnd, 2.7e3);
        c.addCapacitor (k1, gnd, 68.0e-6);
        c.setInitialGuess (b.plateV1a, 230.0);
        c.setInitialGuess (k1, 1.0);

        // Gain pot (VR1, 1M log): C 0.022 uF couples V1a's plate into the pot's top lug; R5 470k / C4 470 pF / C5 1 nF
        // form a bright-compensation network across the pot (a standard trick for a log-taper gain control in a
        // cascaded-gain preamp, read directly off the schematic though its exact internal wiring at scan resolution
        // was not fully legible -- modelled here as a parallel RC bridging the pot's own top-to-wiper segment).
        const auto gainIn = c.addNode();
        c.addCapacitor (b.plateV1a, gainIn, 0.022e-6);
        const auto gainWiper = c.addNode();
        b.rGainTop = c.addResistor (gainIn, gainWiper, 1.0e6);
        b.rGainBot = c.addResistor (gainWiper, gnd, 1.0e6);
        c.addResistor (gainIn, gainWiper, 470.0e3); // R5 (fixed, across the pot's top-to-wiper segment)
        c.addCapacitor (gainIn, gainWiper, 470.0e-12 + 1.0e-9); // C4 + C5, both bridging the same segment

        // V1b: R7 100k plate (vcc1), R6 10k cathode -- UNBYPASSED (no cap), a real local negative-feedback stage that
        // gives this preamp its harder, more compressed character than the earlier bypassed-cathode amps.
        const auto g2 = c.addNode(), k2 = c.addNode();
        b.plateV1b = c.addNode();
        c.addResistor (gainWiper, g2, 1.0e6); // the pot's own load into V1b's grid
        c.addTriode (b.plateV1b, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV1b, cgp);
        c.addResistor (vcc1, b.plateV1b, 100.0e3);
        c.addResistor (k2, gnd, 10.0e3);
        c.setInitialGuess (b.plateV1b, 200.0);
        c.setInitialGuess (k2, 1.4);

        // V2a: C7 0.022 uF coupling, R11 470k grid leak, R12 100k plate
        // (vcc2), R9 820 ohm cathode -- UNBYPASSED again.
        const auto g3 = c.addNode(), k3 = c.addNode();
        b.plate2 = c.addNode();
        c.addCapacitor (b.plateV1b, g3, 0.022e-6);
        c.addResistor (g3, gnd, 470.0e3);
        c.addTriode (b.plate2, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plate2, cgp);
        c.addResistor (vcc2, b.plate2, 100.0e3);
        c.addResistor (k3, gnd, 820.0);
        c.setInitialGuess (b.plate2, 190.0);
        c.setInitialGuess (k3, 1.0);

        // V2b: a cathode follower, DC-coupled directly from V2a's plate (no capacitor between them, per the
        // schematic), driving the tone stack.
        b.follower = c.addNode();
        c.addFollower (b.plate2, b.follower, followerDrop);
        return b;
    }
}

void JCM800StyleAmplifierProcessor::buildChannel (Channel& ch)
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
        ch.sE = c.addNode();

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 50.0e-6);           // two 100 uF banks in series
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 50.0e-6);
        c.addResistor (ch.sB, gnd, bleeder);
        c.addResistor (ch.sB, ch.sC, 20.0e3);           // R26 + R27
        c.addCapacitor (ch.sC, gnd, 100.0e-6);
        c.addResistor (ch.sC, ch.sD, 10.0e3);           // R15
        c.addCapacitor (ch.sD, gnd, 50.0e-6);
        c.addResistor (ch.sD, ch.sE, 10.0e3);           // R14
        c.addCapacitor (ch.sE, gnd, 50.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.14);
        ch.iB = c.addCurrentSource (ch.sB, -0.012);
        ch.iC = c.addCurrentSource (ch.sC, -0.0012);
        ch.iD = c.addCurrentSource (ch.sD, -0.0025);
        ch.iE = c.addCurrentSource (ch.sE, -0.0015);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 440.0);
        c.setInitialGuess (ch.sD, 400.0);
        c.setInitialGuess (ch.sE, 380.0);
    }

    // ================================================================ preamp
    // The follower's DC drop needs the gain stage's DC plate voltage, which does not depend on the follower: build once
    // with a placeholder, read the plate, solve the follower, build again.
    {
        auto probe = buildPreamp (ch.pre, 0.0, 380.0, 400.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plate2);
        const double drop = plate - cathodeFollowerDc (400.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop, 380.0, 400.0);
        ch.pSrcV1 = b.srcV1;
        ch.pSrcV2 = b.srcV2;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.pPlate2 = b.plate2;
        ch.pFollower = b.follower;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV1b = b.plateV1b;
    }

    // ================================================================ tone stack (ALWAYS built and solved: a real linear
    // circuit either way, so it costs nothing extra in reducedOrder mode -- see the header's "reduced-order power stage" note)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        // Tone stack (the Marshall "TMB" stack), this amp's own printed values: C10 470 pF from the follower into the
        // Treble pot's (220k, linear) top, R15 33k from the follower into the stack's own node B, C11 0.022 uF from B
        // to the Treble pot's bottom / the Bass wiper, the Bass pot (1M log, a rheostat) into the Middle pot (22k,
        // linear) to ground, C12 0.022 uF from B to the Middle wiper. The Treble wiper is the output.
        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 470.0e-12);
        c.addResistor (ti, nB, 33.0e3);
        ch.rTrebleTop = c.addResistor (top, ch.wTone, 110.0e3);
        ch.rTrebleBottom = c.addResistor (ch.wTone, nT, 110.0e3);
        c.addCapacitor (nB, nT, 0.022e-6);
        ch.rBass = c.addResistor (nT, nM, 500.0e3);
        ch.rMidTop = c.addResistor (nM, nMw, 11.0e3);
        ch.rMidBottom = c.addResistor (nMw, gnd, 11.0e3);
        c.addCapacitor (nB, nMw, 22.0e-9);
    }

    // ================================================================ phase inverter, power amp -- the FULL reference
    // netlist only (reducedOrder replaces all of this with behavioralPowerStage(), fitted to this same circuit's own
    // measured output -- docs/circuits/JCM8002203.md)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 440.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter V3: C11 22 nF into grid 1; 1M grid leaks meet at M; R17 470 from the joined cathodes to M; R20 10k from M into the
        // feedback node F (which the Presence network holds near ground); C13 100 nF from F into grid 2. Plate loads R22 82k / R25 100k.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (ch.wTone, g1, 22.0e-9);
        c.addResistor (g1, nm, 1.0e6);
        c.addResistor (g2, nm, 1.0e6);
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, nm, 470.0);
        c.addResistor (nm, fp, 10.0e3);
        c.addCapacitor (fp, g2, 100.0e-9);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 22 nF couplings into each pair's grid group (each grid stopper 1k5 / 2 for a pair), 220k grid leaks to the bias
        // node, two EL34 pairs, output transformer.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 22.0e-9);
        c.addCapacitor (pb, g4, 22.0e-9);
        c.addResistor (g3, nb, 220.0e3);
        c.addResistor (g4, nb, 220.0e3);
        c.addResistor (g3, g3s, 750.0);
        c.addResistor (g4, g4s, 750.0);
        // Bias supply: the rectified winding (an ideal source here, behind R29 15k), C17 10 uF, and R28 56k + the trimmer to ground
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 56.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 10.0e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentodeEL34PairPower(), railPlatesNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentodeEL34PairPower(), railPlatesNominal);

        // Distributed capacitance of the primary and each plate's capacitance to ground, plus the transformer's core and copper losses
        // (see BassmanStyleAmplifierProcessor: they damp the resonance with the speaker's voice-coil inductance).
        c.addCapacitor (pp1, pp2, 400.0e-12);
        c.addResistor (pp1, pp2, 20.0e3);
        {
            // The winding's dielectric and eddy-current losses at the top of the band: a series R-C across the primary (nothing at 1 kHz,
            // where the 2 nF is 80k ohm against 1.7k; a few kilohms at 15-20 kHz, where the speaker's inductance resonates with the
            // winding capacitance).
            const auto sn = c.addNode();
            c.addResistor (pp1, sn, 2.0e3);
            c.addCapacitor (sn, pp2, 3.0e-9);
        }
        c.addCapacitor (pp1, gnd, 400.0e-12);
        c.addCapacitor (pp2, gnd, 400.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        c.addResistor (sw, ch.wOut, secondaryResistance);
        {
            const auto sm = speakerModel (speakerNominal[matchedSpeaker]);
            const auto na = c.addNode(), nbb = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na, nbb } }, { sm.le });
            // Eddy-current loss in the voice coil, as a resistance across Le: a real coil's impedance rises as roughly f^0.6, not f^1
            // (Leach's "semi-inductance"), because the pole piece and former dissipate at HF. A pure inductor here has an unphysically
            // high Q where it resonates with the transformer's winding capacitance (about 20 kHz, next to this model's Nyquist).
            ch.rSpkEddy = c.addResistor (na, nbb, speakerEddyLoss);
            ch.rSpkRp = c.addResistor (nbb, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb, gnd, sm.cp);
        }

        // Negative feedback: R21 47k from the 16 ohm terminal into F; the Presence pot (5k) from F to ground with C12 100 nF from F to its wiper.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 2.5e3);
        ch.rPresBottom = c.addResistor (wp, gnd, 2.5e3);
        c.addCapacitor (fp, wp, 100.0e-9);
        // Stray capacitance in the feedback path (see BassmanStyleAmplifierProcessor: without a pole here the loop's gain does not fall
        // before this model's Nyquist and an inductive speaker load makes the closed loop unstable at 48 kHz).
        c.addCapacitor (fp, gnd, 1.5e-9);

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -55.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void JCM800StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Gain (VR1): 1M log taper (pots::audio: 15% at half rotation).
    // Treble 220k linear (wiper toward the 470 pF end = more treble), Bass 1M log (a rheostat: more resistance = more bass),
    // Middle 22k linear with a real wiper tap, Presence 5k linear (the wiper-to-ground segment shrinks as Presence rises).
    const double trebleBottom = juce::jmax (1.0, 220.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 220.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 22.0e3 * k.middle);
    const double midTop = juce::jmax (1.0, 22.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 5.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 5.0e3 - presBottom);
    // Bias trimmer (VR1, 20k, in series with the 56k that loads the bias supply): a bigger resistance loads it less, so the bias is
    // more negative (colder). Knob 0 = hot, 1 = cold.
    const double trim = juce::jmax (1.0, 20.0e3 * k.bias);
    // Tube Feel: the supply's series resistance (sag) and the feedback fraction. 1 = the real amp; 0 = a stiff supply (5% of the
    // sag) and 2.5x the feedback.
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
        ch.pre.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBottom));
        ch.power.setResistance (ch.rTrebleTop, trebleTop);
        ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.power.setResistance (ch.rBass, bassR);
        ch.power.setResistance (ch.rMidTop, midTop);
        ch.power.setResistance (ch.rMidBottom, midBottom);
        // reducedOrder: Presence/feedback/bias-trim resistors don't exist in the tone-stack-only power circuit (see the
        // header's "reduced-order power stage" note) -- Presence isn't reproduced by behavioralPowerStage() yet (a
        // documented gap), and Bias/Tube Feel only ever moved these same removed nodes.
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresTop, presTop);
            ch.power.setResistance (ch.rPresBottom, presBottom);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setResistance (ch.rBiasTrim, trim);
        }
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    // A lighter load gives more volts and a heavier one fewer; this compensates so 4 / 8 / 16 ohm changes the sound, not the
    // loudness (the Bassman's law, measured there as full-drive volts ~ z^0.83) -- verified on the FULL reference netlist
    // (48.3 / 51.7 / 49.1 V rms full-drive, within ~1 dB). reducedOrder has no physical speaker impedance left to
    // compensate for (behavioralPowerStage()'s calibration doesn't vary with speaker choice) -- applying this same z^-0.8
    // factor there was a real bug found by the user (2026-09-27): with nothing left to cancel, it just made 4 ohm ~9.6 dB
    // louder than 16 ohm instead of matching them. reducedOrder keeps the three settings equally loud until the speaker's
    // own tonal difference is modelled behaviourally too (a documented gap, see the header's note).
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void JCM800StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    // reducedOrder: no physical speaker RLC network exists (see the header note); level still follows speakerGain
    // (computed unconditionally in updatePots()), but the speaker's own resonance/impedance isn't reproduced -- a
    // documented gap in behavioralPowerStage().
    if (reducedOrder)
        return;
    const double nominal = speakerNominal[juce::jlimit (0, 2, index)];
    const auto sm = speakerModel (nominal);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setResistance (ch.rSpkEddy, speakerEddyLoss * nominal / speakerNominal[matchedSpeaker]);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, { 1.0 / sm.le });
    ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 / sm.lp });
}

void JCM800StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    if (reducedOrder)
        return; // no physical speaker network to compare against a resistor in this mode
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setResistance (ch.rSpkEddy, 1.0e9);
        ch.power.setInductorInverse (ch.grpSpkLe, { 1.0e6 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 });
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2;
    resistiveLoadForced = true;
}

void JCM800StyleAmplifierProcessor::recover (Channel& ch) const
{
    // See BassmanStyleAmplifierProcessor::recover: back to the settled state, output kept continuous by the de-click offset.
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

void JCM800StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    // The supply sees what four output tubes can physically draw (an EL34 peaks around 0.5 A): a solver excursion in the power block
    // must not be able to turn into an absurd current that wrecks the rails of every other stage.
    if (! supplyCurrentFrozen)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    // ... and a rectified supply can neither reverse nor exceed its open-circuit voltage.
    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    // reducedOrder: wSrcCt/wSrcPi don't exist in the tone-stack-only power circuit (their default-0 handles would silently
    // clobber whatever real source/resistor got index 0 there instead) -- see the header's "reduced-order power stage" note.
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 520.0));
    }
    ch.pre.setSource (ch.pSrcV2, rail (ch.sD, 480.0));
    ch.pre.setSource (ch.pSrcV1, rail (ch.sE, 480.0));
    ch.vScreen = rail (ch.sB, 560.0);
}

double JCM800StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
{
    // Envelope follower driving the sag lookup: fast attack (a rail sags quickly under a sudden hard hit), slower release
    // (a real capacitor bank recharges through the rectifier's resistance over tens of ms) -- matches the amp's documented
    // ~13 Hz supply ring (~12 ms) better than a single symmetric time constant.
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (toneVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    // Saturating curve fitted to the real closed-loop transfer (docs/circuits/JCM8002203.md): small-signal gain bmGain0,
    // peak output bmYmax of the (sagged) rail at full saturation, knee sharpness bmKneeN. K is the knee's location in
    // absolute volts AT THE CURRENT RAIL (a lighter-sagged rail clips later, in volts, exactly like the real amp).
    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, toneVoltage);

    // High-shelf cut for the frequency response the removed stages used to provide (see the header's own comment):
    // ch.bmToneState is a one-pole lowpass of the curve's raw output; blending it back in at bmShelfHfGain leaves DC/LF
    // at unity and cuts everything above the shelf frequency.
    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double JCM800StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::gainStagePlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
        case Probe::v1bPlate: return ch.pre.voltage (ch.pPlateV1b);
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
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
        case Probe::feedbackNode: return ch.power.voltage (ch.wFeedback);
    }
    return 0.0;
}

double JCM800StyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int JCM800StyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void JCM800StyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return; // no feedback resistor node exists in this mode (see the header's "reduced-order power stage" note)
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double JCM800StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0; // no pentode devices exist in this mode (see the header's "reduced-order power stage" note)
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double JCM800StyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void JCM800StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double seconds)
    {
        s.reset (newSampleRate, seconds);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMiddle, middleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.16;
    appliedSpeaker = matchedSpeaker; // the circuits are built with the 16 ohm speaker
    updatePots ({ gainParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(),
                  presenceParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        // The supply, then the amplifier on its rails, then the currents the amplifier draws back into the supply: three passes so
        // the whole thing starts settled (the drawing prints no voltages, so the rails are whatever the tubes' own currents give).
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        // The currents depend on the rails they set (a higher rail draws more, which lowers the rail): a plain fixed-point iteration
        // overshoots and oscillates, so each pass moves only half way to what the tubes drew.
        double iPiRun = 0.0035, iV2Run = 0.0028, iV1Run = 0.0018, ipRun = 0.14, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
            ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sE));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.followerDc = ch.pre.voltage (ch.pFollower);

            ch.power.setSource (ch.wSrcCf, ch.followerDc);
            ch.power.setInitialGuess (ch.wToneIn, ch.followerDc);
            ch.vScreen = ch.supply.voltage (ch.sB);
            // reducedOrder: none of the PI/pentode handles below exist in the tone-stack-only power circuit (see the header's
            // "reduced-order power stage" note); ipA/ipB/isA/isB/iPi stay 0, which decays the supply's assumed power-tube draw
            // toward 0 over these passes -- a reasonable idle point for the PREAMP's own rails, which is all this supply
            // model still needs to get right here (behavioralPowerStage() has its own, separate sag model at runtime).
            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0, iPi = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
            }
            passOk = ch.power.prepare (newSampleRate) && passOk;
            ch.power.solveSample();

            if (! reducedOrder)
            {
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                const double vPi = ch.supply.voltage (ch.sC);
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
            }
            const double vV2 = ch.supply.voltage (ch.sD), vV1 = ch.supply.voltage (ch.sE);
            const double iV2 = (vV2 - ch.pre.voltage (ch.pPlate2)) / 100.0e3 + ch.followerDc / 100.0e3;
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateV1a)) / 100.0e3 + (vV1 - ch.pre.voltage (ch.pPlateV1b)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iV2Run);
            ch.supply.setCurrentSource (ch.iE, -iV1Run);
            // Open-circuit voltage such that the plate rail sits at its nominal value with THIS model's idle currents.
            idleSupplyCurrent = ipRun + isRun + iPiRun + iV2Run + iV1Run + (ch.vScreen / bleeder);
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        }
        ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
        ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sE));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void JCM800StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get()); // 0 High, 1 Low sensitivity jack
    // The two jacks share one gain stage on the real amp -- modelled as a plain input attenuation
    // rather than a second resistor network, since both jacks feed the exact same grid.
    const double inputGain = inputChoice == 0 ? 1.0 : 0.25;

    for (int i = 0; i < numSamples; ++i)
    {
        const float gn = smoothedGain.getNextValue();
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
            updatePots ({ gn, tr, mi, ba, pr, pw, bi, fe, speakerChoice });
        }

        // Power Drive: a master volume between the preamp and the tone stack / phase inverter (amplitude = audio taper).
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

        // Output control: -30 dB .. 0 dB at noon .. +12 dB.
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            // Input selector: High or Low sensitivity jack, both wired into the same V1a grid on the real amp.
            const double x = std::isfinite (data[i]) ? inputLimit (inputGain * (double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
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
            // its rails just settle toward the (small) preamp-only draw, since behavioralPowerStage() below has its own,
            // separate sag lookup fitted directly from the full model's real behaviour (see the header's note).
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

            // A converged solve can still land on a state no amplifier reaches (a speaker terminal at hundreds of volts, a plate below
            // ground): such a sample is a failure, and the output holds its last value instead of printing the excursion. reducedOrder's
            // behavioralPowerStage() is a bounded saturating function (mathematically can't exceed ~bmYmax * bmRail), so this can only
            // ever reject a NaN there, never a real excursion -- there is no Newton solve left to have a bad day.
            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone)) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));
            if (chIdx == 0 && ! sane && okPre && ok2)
                ++sanityRejects;

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
            else if (++ch.failStreak >= 4) // see BassmanStyleAmplifierProcessor::process for why 4
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
                lastSampleOk = ok;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void JCM800StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
