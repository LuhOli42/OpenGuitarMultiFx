#include "DualRectifierStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/DualRectifier.md). Factory Mesa Boogie Dual Rectifier (2-channel, 6-93 GEO. M.).
    // Solid-state rectification only (the tube/diode select is not modelled).
    // Rails: A=460V plates, B=454V screens, C=422V (PI), D=406V (V3A), E=402V (V1A/V2).
    // Chain: Rect → A (220µF, 150K bleeder) → CHOKE → B (220µF) → 2.7K → C (30µF) → 22K → D (30µF) → 15K → E (30µF). ----
    constexpr double railPlatesNominal = 460.0;
    constexpr double railScreensNominal = 454.0;
    constexpr double rectifierResistance = 60.0;
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 150.0e3;
    constexpr double screenResistor = 500.0;  // 1K per tube / 2 per pair

    // ---- reduced-order power stage sag table (Twin Reverb's data scaled from 497V to 460V) ----
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 460.0, 460.0, 460.0, 460.0, 459.99, 459.98, 459.96, 459.91,
                                                 459.83, 459.58, 459.21, 458.40, 457.26, 455.18, 451.32, 444.82,
                                                 436.84, 420.88, 406.09, 402.21 };

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
    KorenTriode::Parameters triode12AX7() { return {}; }

    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        p.kg1 *= 40.0;
        return p;
    }

    KorenPentode::Parameters pentode6L6Pair()
    {
        KorenPentode::Parameters p;
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    KorenPentode::Parameters pentode6L6PairPower()
    {
        auto p = pentode6L6Pair();
        p.kg1 *= 40.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer: 4x6L6GC, ~100 W. Same class as the SLO-100 / Mark IIC+.
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    // NFB: from schematic R276=47K from OT, but since the model taps from 16 ohm secondary (not 4 ohm),
    // scale: 47K * (16/4) + 10K = ~198K effective (R353=10K PI side).
    constexpr double feedbackResistor = 198.0e3;

    constexpr double biasSupplyVolts = -51.0;  // 6L6GC bias

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2;
    constexpr double powerTheta = 0.9;

    // ---- preamp helper ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV2 = 0, srcV3 = 0, srcE = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node plateV1a = 0, plateV2a = 0, plateV2b = 0, plateV3a = 0, follower = 0;
        NodalCircuit::Node nodeV2 = 0, nodeV3 = 0, nodeE = 0;
    };

    /** The Dual Rectifier RED channel preamp: FOUR cascaded 12AX7 gain stages (V1A -> V2A -> V2B -> V3A) plus
        a V3D cathode follower driving the tone stack.

        Key voicing elements:
        - V1A: 220K plate (E rail), 1.8K/bypassed cathode — the input gain stage
        - RED GAIN: 1M pot (audio taper) between V1A and V2A — the high-gain drive control
        - V2A: plate from D rail, 1.8K/bypassed cathode — second gain stage
        - V2B: 100K plate (D rail), unbypassed cathode (~27K estimated from Vp/Ip) — compression stage
        - V3A: 220K plate (C rail), 1.8K/bypassed cathode — final gain stage

        Supply taps: E (~402V) for V1A, D (~406V) for V2A/V2B, C (~422V) for V3A/V3D. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double eGuess, double dGuess, double cGuess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vccE = c.addNode(), vccD = c.addNode(), vccC = c.addNode(), in = c.addNode();
        b.nodeE = vccE;
        b.nodeV2 = vccD;
        b.nodeV3 = vccC;
        b.srcE = c.addSource (vccE, eGuess);
        b.srcV2 = c.addSource (vccD, dGuess);
        b.srcV3 = c.addSource (vccC, cGuess);
        b.srcIn = c.addSource (in, 0.0);

        // V1A: 220K plate (E rail), 68K grid stopper, 1M grid leak, 1.8K cathode bypassed with 22µF.
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plateV1a = c.addNode();
        c.addResistor (in, g1, 68.0e3);                  // grid stopper
        c.addResistor (g1, gnd, 1.0e6);                  // grid leak
        c.addTriode (b.plateV1a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateV1a, cgp);
        c.addResistor (vccE, b.plateV1a, 220.0e3);       // R221
        c.addResistor (k1, gnd, 1.8e3);                  // R291
        c.addCapacitor (k1, gnd, 22.0e-6);               // bypassed
        c.setInitialGuess (b.plateV1a, 200.0);
        c.setInitialGuess (k1, 1.6);

        // RED GAIN: V1A plate → coupling cap (22nF) → 470K grid leak → Gain pot (1MA).
        const auto gainIn = c.addNode(), gainWiper = c.addNode();
        c.addCapacitor (b.plateV1a, gainIn, 22.0e-9);    // coupling cap to gain pot
        c.addResistor (gainIn, gnd, 470.0e3);             // grid leak / bias reference
        b.rGainTop = c.addResistor (gainIn, gainWiper, 1.0e6);
        b.rGainBot = c.addResistor (gainWiper, gnd, 1.0e6);

        // V2A: plate from D rail, 470K grid leak, 1.8K cathode bypassed with 22µF.
        // The plate load is assumed ~220K (same as V1A; not fully legible on the factory schematic,
        // but consistent with the D rail voltage drop to the ~280V plate reading).
        const auto g2 = c.addNode(), k2 = c.addNode();
        b.plateV2a = c.addNode();
        c.addResistor (gainWiper, g2, 39.0e3);           // series from gain wiper (estimated from schematic)
        c.addResistor (g2, gnd, 470.0e3);                // grid leak
        c.addTriode (b.plateV2a, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV2a, cgp);
        c.addResistor (vccD, b.plateV2a, 220.0e3);       // plate load
        c.addResistor (k2, gnd, 1.8e3);                  // R292
        c.addCapacitor (k2, gnd, 22.0e-6);               // bypassed
        c.setInitialGuess (b.plateV2a, 280.0);
        c.setInitialGuess (k2, 1.6);

        // V2B: 100K plate (D rail), 220K grid leak. Unbypassed cathode (~27K estimated from 384V plate and
        // ~6V cathode → Ip ≈ 22µA → Rk = 6/0.000022 ≈ 27K). This is the compression/clipping shaping stage.
        const auto g3 = c.addNode(), k3 = c.addNode(), coup3 = c.addNode();
        b.plateV2b = c.addNode();
        c.addCapacitor (b.plateV2a, coup3, 22.0e-9);     // coupling from V2A
        c.addResistor (coup3, g3, 470.0e3);              // series into V2B grid
        c.addResistor (g3, gnd, 220.0e3);                // R225, grid leak
        c.addTriode (b.plateV2b, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateV2b, cgp);
        c.addResistor (vccD, b.plateV2b, 100.0e3);       // R302
        c.addResistor (k3, gnd, 27.0e3);                 // unbypassed — compression stage
        c.setInitialGuess (b.plateV2b, 384.0);
        c.setInitialGuess (k3, 6.0);

        // V3A: 220K plate (C rail), 220K grid leak, 1.8K cathode bypassed with 22µF.
        // Coupling from V2B through 22nF → 39K series (R103 grid stopper).
        const auto g4 = c.addNode(), k4 = c.addNode(), coup4 = c.addNode();
        b.plateV3a = c.addNode();
        c.addCapacitor (b.plateV2b, coup4, 22.0e-9);     // coupling from V2B
        c.addResistor (coup4, g4, 39.0e3);               // R103 grid stopper
        c.addResistor (g4, gnd, 220.0e3);                // R225 grid leak
        c.addTriode (b.plateV3a, g4, k4, triode12AX7());
        c.addCapacitor (g4, b.plateV3a, cgp);
        c.addResistor (vccC, b.plateV3a, 220.0e3);       // R224
        c.addResistor (k4, gnd, 1.8e3);                  // R293
        c.addCapacitor (k4, gnd, 22.0e-6);               // bypassed
        c.setInitialGuess (b.plateV3a, 213.0);
        c.setInitialGuess (k4, 1.6);

        // V3D: cathode follower — drives the tone stack through a low-impedance output.
        b.follower = c.addNode();
        c.addFollower (b.plateV3a, b.follower, followerDrop);
        return b;
    }
}

DualRectifierStyleAmplifierProcessor::DualRectifierStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto gain = make ("drec_gain", "Gain", 0.5f);
    auto treble = make ("drec_treble", "Treble", 0.5f);
    auto mid = make ("drec_mid", "Mid", 0.5f);
    auto bass = make ("drec_bass", "Bass", 0.5f);
    auto presence = make ("drec_presence", "Presence", 0.3f);
    auto master = make ("drec_master", "Master", 0.5f);
    auto output = make ("drec_output", "Output", 0.5f);
    auto power = make ("drec_power", "Power Drive", 0.5f);
    auto bias = make ("drec_bias", "Bias", 0.5f);
    auto feel = make ("drec_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "drec_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    masterParam = master.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "dualrec", "Dual Rectifier-Style Amplifier", "|", std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (master));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("drec_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void DualRectifierStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // Solid-state rectification → choke-filtered RC chain.
    // A (220µF, 150K bleeder) → CHOKE → B (220µF) → 2.7K → C (30µF) → 22K → D (30µF) → 15K → E (30µF)
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~460V)
        const auto nl = c.addNode();
        ch.sB = c.addNode();                           // screens rail (~454V)
        ch.sC = c.addNode();                           // PI supply (~422V)
        ch.sD = c.addNode();                           // V2/V3 (~406V)
        ch.sE = c.addNode();                           // V1A (~402V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 220.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 220.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);              // 150K bleeder across main bank
        c.addResistor (ch.sB, ch.sC, 2.7e3);              // R502
        c.addCapacitor (ch.sC, gnd, 30.0e-6);
        c.addResistor (ch.sC, ch.sD, 22.0e3);             // R252
        c.addCapacitor (ch.sD, gnd, 30.0e-6);
        c.addResistor (ch.sD, ch.sE, 15.0e3);             // R261
        c.addCapacitor (ch.sE, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);        // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.012);       // power tubes screen current
        ch.iC = c.addCurrentSource (ch.sC, -0.003);       // PI current draw
        ch.iD = c.addCurrentSource (ch.sD, -0.003);       // V2A/V2B/V3A preamp current
        ch.iE = c.addCurrentSource (ch.sE, -0.001);       // V1A preamp current
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 422.0);
        c.setInitialGuess (ch.sD, 406.0);
        c.setInitialGuess (ch.sE, 402.0);
    }

    // ================================================================ preamp (two-pass for cathode follower DC offset)
    {
        auto probe = buildPreamp (ch.pre, 0.0, 402.0, 406.0, 422.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plateV3a);
        const double drop = plate - cathodeFollowerDc (422.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop, 402.0, 406.0, 422.0);
        ch.pSrcE = b.srcE;
        ch.pSrcV2 = b.srcV2;
        ch.pSrcV3 = b.srcV3;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV2a = b.plateV2a;
        ch.pPlateV2b = b.plateV2b;
        ch.pPlateV3a = b.plateV3a;
        ch.pFollower = b.follower;
    }

    // ================================================================ tone stack (Fender TMB, always built and solved)
    // Dual Rectifier RED tone stack: 47K slope, 680pF treble, 250K treble pot, 22K series (R254),
    // 1M bass, .02µF bass/mid caps, 25K mid. Master (1M) after tone stack.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        const auto masterNode = c.addNode();
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 680.0e-12);               // C4: 680pF treble coupling cap
        c.addResistor (ti, nB, 47.0e3);                    // R273: 47K slope
        ch.rTrebleTop = c.addResistor (top, masterNode, 125.0e3);    // TRBL 250K, split
        ch.rTrebleBottom = c.addResistor (masterNode, nT, 125.0e3);
        c.addResistor (nT, nB, 22.0e3);                    // R254
        c.addCapacitor (nB, nT, 0.02e-6);                  // C23: .02µF bass cap
        ch.rBass = c.addResistor (nT, nM, 500.0e3);        // BASS 1M, rheostat
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);      // MID 25K, split
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        c.addCapacitor (nB, nMw, 0.02e-6);                 // C26: .02µF mid cap
        // Master (MSTR 1M) as a rheostat after the tone stack wiper
        ch.rMaster = c.addResistor (masterNode, ch.wTone, 500.0e3);
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 422.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter: 12AX7 long-tailed pair.
        // V5B: 82K plate (C rail), V5A: 90K plate (C rail), 1K tail on each.
        // 75pF compensation cap between plates. .047µF coupling to power tubes.
        // 220K grid leaks (R223, R222) to power tubes.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (ch.wTone, g1, 0.022e-6);
        c.addResistor (g1, nm, 1.0e6);
        c.addResistor (g2, fp, 1.0e6);
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);                    // R281
        c.addResistor (vpi, pb, 90.0e3);                    // R104
        c.addCapacitor (pa, pb, 75.0e-12);                   // C3
        c.addResistor (k, nm, 1.0e3);                        // tail R (R214/R213 combined)
        c.addResistor (nm, gnd, 10.0e3);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 4 × 6L6GC as two push-pull pairs. .047µF couplings, 1.5K grid stoppers (3K/pair),
        // 220K grid leaks to the fixed bias rail.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.047e-6);
        c.addCapacitor (pb, g4, 0.047e-6);
        c.addResistor (g3, nb, 220.0e3);                    // R223
        c.addResistor (g4, nb, 220.0e3);                    // R222
        c.addResistor (g3, g3s, 1.5e3);                     // grid stoppers (3K/pair = 1.5K each)
        c.addResistor (g4, g4s, 1.5e3);
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6L6PairPower(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6L6PairPower(), railScreensNominal);

        c.addCapacitor (pp1, pp2, 400.0e-12);
        c.addResistor (pp1, pp2, 20.0e3);
        {
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
            ch.rSpkEddy = c.addResistor (na, nbb, speakerEddyLoss);
            ch.rSpkRp = c.addResistor (nbb, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb, gnd, sm.cp);
        }

        // NFB: from speaker terminal through feedbackResistor to PI grid 2, with Presence 25K shunt and stray cap.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 12.5e3);
        ch.rPresBottom = c.addResistor (wp, gnd, 12.5e3);
        c.addCapacitor (fp, wp, 0.1e-6);                    // C52
        c.addCapacitor (fp, gnd, 1.5e-9);                   // stray for HF stability

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -50.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void DualRectifierStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 25.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 25.0e3 - presBottom);
    const double masterR = juce::jmax (1.0, 1.0e6 * pots::audio (k.master));
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
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
        ch.power.setResistance (ch.rMaster, masterR);
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
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void DualRectifierStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
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

void DualRectifierStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    if (reducedOrder)
        return;
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

void DualRectifierStyleAmplifierProcessor::recover (Channel& ch) const
{
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

void DualRectifierStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! supplyCurrentFrozen)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 520.0));
    }
    ch.pre.setSource (ch.pSrcV3, rail (ch.sC, 480.0));   // V3A from PI rail (C)
    ch.pre.setSource (ch.pSrcV2, rail (ch.sD, 480.0));   // V2A/V2B from D
    ch.pre.setSource (ch.pSrcE, rail (ch.sE, 480.0));    // V1A from E
    ch.vScreen = rail (ch.sB, 560.0);
}

double DualRectifierStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double DualRectifierStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
        case Probe::v2aPlate: return ch.pre.voltage (ch.pPlateV2a);
        case Probe::v2bPlate: return ch.pre.voltage (ch.pPlateV2b);
        case Probe::v3aPlate: return ch.pre.voltage (ch.pPlateV3a);
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

double DualRectifierStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int DualRectifierStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void DualRectifierStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double DualRectifierStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double DualRectifierStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void DualRectifierStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedMid, midParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedMaster, masterParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), masterParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iPiRun = 0.003, iV3Run = 0.002, iV2Run = 0.003, iV1Run = 0.001, ipRun = 0.16, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sC));
            ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
            ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.followerDc = ch.pre.voltage (ch.pFollower);

            ch.power.setSource (ch.wSrcCf, ch.followerDc);
            ch.power.setInitialGuess (ch.wToneIn, ch.followerDc);
            ch.vScreen = ch.supply.voltage (ch.sB);
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
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 90.0e3;
            }
            const double vV3 = ch.supply.voltage (ch.sC);
            const double iV3 = (vV3 - ch.pre.voltage (ch.pPlateV3a)) / 220.0e3 + ch.followerDc / 100.0e3;
            const double vV2 = ch.supply.voltage (ch.sD);
            const double iV2 = (vV2 - ch.pre.voltage (ch.pPlateV2a)) / 220.0e3
                             + (vV2 - ch.pre.voltage (ch.pPlateV2b)) / 100.0e3;
            const double vV1 = ch.supply.voltage (ch.sE);
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateV1a)) / 220.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV3Run += 0.5 * (iV3 - iV3Run);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -(iV3Run + iV2Run));
            ch.supply.setCurrentSource (ch.iE, -iV1Run);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iV3Run + iV2Run + iV1Run + (ch.vScreen / bleeder);
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
        ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sC));
        ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
        ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
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

void DualRectifierStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMid.setTargetValue (midParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMid.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float ms = smoothedMaster.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, ms, pw, bi, fe, speakerChoice });
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            ch.power.setSource (ch.wSrcCf, ch.followerDc + masterGain * cathodeFollowerGain * (ch.pre.voltage (ch.pFollower) - ch.followerDc));
            if (! reducedOrder)
            {
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
            }
            const bool ok2 = ch.power.solveSample();
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

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
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre.saveDynamicState (ch.preRest);
                    ch.power.saveDynamicState (ch.powerRest);
                    ch.supply.saveDynamicState (ch.supplyRest);
                }
            }
            else if (++ch.failStreak >= 4)
            {
                recover (ch);
                ch.restRefreshCounter = 0;
            }

            double out = ch.lastEmitted;
            if (sane)
            {
                out = speakerVolts * outputScale * outGain * speakerGain;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - out;
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

void DualRectifierStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
