#include "BognerUberschallStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/BognerUberschall.md). 4 x EL34, ~120 W at ~460V plates.
    // Rails: A=460V plates, B=454V screens, C=430V (PI), D=410V (V2/V3), E=400V (V1).
    // Chain: Rect -> A (220uF, 150K bleeder) -> CHOKE -> B (220uF) -> 2.7K -> C (30uF) -> 22K -> D (30uF) -> 15K -> E (30uF). ----
    constexpr double railPlatesNominal = 460.0;
    constexpr double railScreensNominal = 454.0;
    constexpr double rectifierResistance = 55.0;
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 150.0e3;
    constexpr double screenResistor = 500.0;  // 1K per tube / 2 per pair

    // ---- reduced-order power stage sag table (Twin Reverb's 6L6GC data scaled 497V -> 470V) ----
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

    // EL34 Koren fit used for the Super Lead in this codebase (mu 8.11, Ex 1.50, kg1 1201, kg2 3720,
    // kp 100, kvb 24, single tube). Pair: kg1/kg2 halve, grid Gg doubles, arcResistance halves.
    KorenPentode::Parameters pentodeEL34()
    {
        KorenPentode::Parameters p;
        p.mu = 8.11;
        p.ex = 1.5;
        p.kg1 = 1201.0;
        p.kg2 = 3720.0;
        p.kp = 100.0;
        p.kvb = 24.0;
        return p;
    }

    KorenPentode::Parameters pentodeEL34Pair()
    {
        auto p = pentodeEL34();
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    KorenPentode::Parameters pentodeEL34PairPower()
    {
        auto p = pentodeEL34Pair();
        p.kg1 *= 40.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer: 4x6L6GC, ~100 W. Same class as the SLO-100 / Dual Rectifier.
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    // NFB off the 16-ohm tap to the PI (estimated for the EL34 quad).
    constexpr double feedbackResistor = 56.0e3;

    constexpr double biasSupplyVolts = -45.0;  // EL34 fixed bias

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2;
    constexpr double powerTheta = 0.9;

    // ---- preamp helper ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV3 = 0, srcV2 = 0, srcE = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node plateV1a = 0, plateV1b = 0, plateV2b = 0, plateV3a = 0, follower = 0;
        NodalCircuit::Node nodeV3 = 0, nodeV2 = 0, nodeE = 0;
    };

    /** The Uberschall OD-channel preamp (factory preamp schematic, educational tracing): V1A (180K
        plate, 1.8K + 4.7uF) -> 100nF coupling -> GAIN (475K pot + 300pF bright cap + 330pF shunt, the
        aggressive voiced control) -> 22nF -> V1B (100K plate, 2.4K + 470nF partial bypass) -> V2B ->
        V3A -> V3B cathode follower -> tone stack. The small 22nF couplings are the amp's tight low end. */
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

        // V1A: 180K plate (E rail) -- R5; 68K stopper R2, 100K leak R1, 1.8K + 4.7uF cathode (R6/C3).
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plateV1a = c.addNode();
        c.addResistor (in, g1, 68.0e3);                  // R2
        c.addResistor (g1, gnd, 100.0e3);                // R1
        c.addTriode (b.plateV1a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateV1a, cgp);
        c.addResistor (vccE, b.plateV1a, 180.0e3);       // R5
        c.addResistor (k1, gnd, 1.8e3);                  // R6
        c.addCapacitor (k1, gnd, 4.7e-6);                // C3
        c.setInitialGuess (b.plateV1a, 210.0);
        c.setInitialGuess (k1, 1.6);

        // GAIN: V1A plate -> 100nF -> P1 475K pot; C202 300pF bright cap across the top leg, K4 330pF
        // shunt to ground at the top (the voiced network that keeps the attack bright at low gain).
        const auto gainIn = c.addNode(), gainWiper = c.addNode();
        c.addCapacitor (b.plateV1a, gainIn, 100.0e-9);   // CX2
        c.addResistor (gainIn, gnd, 470.0e3);
        c.addCapacitor (gainIn, gnd, 330.0e-12);         // K4
        b.rGainTop = c.addResistor (gainIn, gainWiper, 475.0e3);
        b.rGainBot = c.addResistor (gainWiper, gnd, 475.0e3);
        c.addCapacitor (gainIn, gainWiper, 300.0e-12);   // C202 bright

        // V1B: 100K plate R7 (D rail), 68K stopper R8 + 68K leak R9, 2.4K + 470nF cathode (R10/K3).
        const auto g2 = c.addNode(), k2 = c.addNode(), coup2 = c.addNode();
        b.plateV1b = c.addNode();
        c.addCapacitor (gainWiper, coup2, 22.0e-9);      // KX2 -- small coupling, tight low end
        c.addResistor (coup2, g2, 68.0e3);               // R8
        c.addResistor (g2, gnd, 68.0e3);                 // R9
        c.addTriode (b.plateV1b, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV1b, cgp);
        c.addResistor (vccD, b.plateV1b, 100.0e3);       // R7
        c.addResistor (k2, gnd, 2.4e3);                  // R10
        c.addCapacitor (k2, gnd, 470.0e-9);              // K3 partial bypass
        c.setInitialGuess (b.plateV1b, 240.0);
        c.setInitialGuess (k2, 2.2);

        // V2B: 100K plate (C rail), 1.8K + 4.7uF cathode; 22nF coupling.
        const auto g3 = c.addNode(), k3 = c.addNode(), coup3 = c.addNode();
        b.plateV2b = c.addNode();
        c.addCapacitor (b.plateV1b, coup3, 22.0e-9);
        c.addResistor (coup3, g3, 39.0e3);
        c.addResistor (g3, gnd, 470.0e3);
        c.addTriode (b.plateV2b, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateV2b, cgp);
        c.addResistor (vccC, b.plateV2b, 100.0e3);
        c.addResistor (k3, gnd, 1.8e3);
        c.addCapacitor (k3, gnd, 4.7e-6);
        c.setInitialGuess (b.plateV2b, 230.0);
        c.setInitialGuess (k3, 1.6);

        // V3A: 100K plate (C rail), 1.8K + 4.7uF; the relay-switched extra stage (always in chain).
        const auto g4 = c.addNode(), k4 = c.addNode(), coup4 = c.addNode();
        b.plateV3a = c.addNode();
        c.addCapacitor (b.plateV2b, coup4, 22.0e-9);
        c.addResistor (coup4, g4, 39.0e3);
        c.addResistor (g4, gnd, 470.0e3);
        c.addTriode (b.plateV3a, g4, k4, triode12AX7());
        c.addCapacitor (g4, b.plateV3a, cgp);
        c.addResistor (vccC, b.plateV3a, 100.0e3);
        c.addResistor (k4, gnd, 1.8e3);
        c.addCapacitor (k4, gnd, 4.7e-6);
        c.setInitialGuess (b.plateV3a, 230.0);
        c.setInitialGuess (k4, 1.6);

        // V3B: cathode follower -- drives the tone stack.
        b.follower = c.addNode();
        c.addFollower (b.plateV3a, b.follower, followerDrop);
        return b;
    }
}

BognerUberschallStyleAmplifierProcessor::BognerUberschallStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("uber_gain", "Gain", 0.5f);
    auto treble = make ("uber_treble", "Treble", 0.5f);
    auto mid = make ("uber_mid", "Middle", 0.5f);
    auto bass = make ("uber_bass", "Bass", 0.5f);
    auto presence = make ("uber_presence", "Presence", 0.3f);
    auto master = make ("uber_master", "Master", 0.5f);
    auto output = make ("uber_output", "Output", 0.5f);
    auto power = make ("uber_power", "Power Drive", 0.5f);
    auto bias = make ("uber_bias", "Bias", 0.5f);
    auto feel = make ("uber_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "uber_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    gainParam = volume.get();
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
        "bogner_uber", "Bogner Uberschall-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (master));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("uber_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void BognerUberschallStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~470V)
        const auto nl = c.addNode();
        ch.sB = c.addNode();                           // screens rail (~464V)
        ch.sC = c.addNode();                           // PI supply (~435V)
        ch.sD = c.addNode();                           // V2 (~415V)
        ch.sE = c.addNode();                           // V1 (~405V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 220.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 220.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);
        c.addResistor (ch.sB, ch.sC, 2.7e3);
        c.addCapacitor (ch.sC, gnd, 30.0e-6);
        c.addResistor (ch.sC, ch.sD, 22.0e3);
        c.addCapacitor (ch.sD, gnd, 30.0e-6);
        c.addResistor (ch.sD, ch.sE, 15.0e3);
        c.addCapacitor (ch.sE, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);
        ch.iB = c.addCurrentSource (ch.sB, -0.012);
        ch.iC = c.addCurrentSource (ch.sC, -0.004);
        ch.iD = c.addCurrentSource (ch.sD, -0.002);
        ch.iE = c.addCurrentSource (ch.sE, -0.001);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 430.0);
        c.setInitialGuess (ch.sD, 410.0);
        c.setInitialGuess (ch.sE, 400.0);
    }

    // ================================================================ preamp (two-pass for cathode follower DC offset)
    {
        auto probe = buildPreamp (ch.pre, 0.0, 400.0, 410.0, 430.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plateV3a);
        const double drop = plate - cathodeFollowerDc (430.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop, 400.0, 410.0, 430.0);
        ch.pSrcE = b.srcE;
        ch.pSrcV2 = b.srcV2;
        ch.pSrcV3 = b.srcV3;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV1b = b.plateV1b;
        ch.pPlateV2b = b.plateV2b;
        ch.pPlateV3a = b.plateV3a;
        ch.pFollower = b.follower;
    }

    // ================================================================ tone stack (Uberschall-style TMB, always built and solved)
    // 150pF treble cap, 56K slope, .022 bass/mid caps, 240K treble, 720K bass, 25K mid; Master 1M after.
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
        c.addCapacitor (ti, top, 150.0e-12);               // treble coupling cap
        c.addResistor (ti, nB, 56.0e3);                    // slope resistor
        ch.rTrebleTop = c.addResistor (top, masterNode, 120.0e3);    // TREBLE 240K, split
        ch.rTrebleBottom = c.addResistor (masterNode, nT, 120.0e3);
        c.addResistor (nT, nB, 33.0e3);                    // series to bass leg
        c.addCapacitor (nB, nT, 0.022e-6);                 // bass cap
        ch.rBass = c.addResistor (nT, nM, 360.0e3);        // BASS 720K, rheostat
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);      // MID 25K, split
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        c.addCapacitor (nB, nMw, 0.022e-6);                // mid cap
        ch.rMaster = c.addResistor (masterNode, ch.wTone, 500.0e3);
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 430.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter: 12AX7 long-tailed pair, 82K/100K plates, .047uF couplings.
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
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, nm, 1.0e3);
        c.addResistor (nm, gnd, 10.0e3);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 4 x EL34 as two push-pull pairs. .047uF couplings, 1.5K grid stoppers,
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
        c.addResistor (g3, nb, 220.0e3);
        c.addResistor (g4, nb, 220.0e3);
        c.addResistor (g3, g3s, 1.5e3);
        c.addResistor (g4, g4s, 1.5e3);
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentodeEL34PairPower(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentodeEL34PairPower(), railScreensNominal);

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
        c.addCapacitor (fp, wp, 0.1e-6);
        c.addCapacitor (fp, gnd, 1.5e-9);

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -45.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void BognerUberschallStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 240.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 240.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 720.0e3 * pots::audio (k.bass));
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
        const double gainBottom = juce::jmax (1.0, 475.0e3 * pots::audio (k.gain));
        ch.pre.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 475.0e3 - gainBottom));
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

void BognerUberschallStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void BognerUberschallStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void BognerUberschallStyleAmplifierProcessor::recover (Channel& ch) const
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
    ch.vCt = ch.supply.voltage (ch.sA);
}

void BognerUberschallStyleAmplifierProcessor::updateSupply (Channel& ch) const
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
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 570.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 530.0));
    }
    ch.pre.setSource (ch.pSrcV3, rail (ch.sC, 490.0));
    ch.pre.setSource (ch.pSrcV2, rail (ch.sD, 490.0));
    ch.pre.setSource (ch.pSrcE, rail (ch.sE, 490.0));
    ch.vScreen = rail (ch.sB, 570.0);
    ch.vCt = rail (ch.sA, 570.0);
}

double BognerUberschallStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double BognerUberschallStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
        case Probe::v1bPlate: return ch.pre.voltage (ch.pPlateV1b);
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

double BognerUberschallStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int BognerUberschallStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void BognerUberschallStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double BognerUberschallStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double BognerUberschallStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void BognerUberschallStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
        double iPiRun = 0.004, iV3Run = 0.002, iV2Run = 0.002, iV1Run = 0.001, ipRun = 0.16, isRun = 0.012;
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
            ch.vCt = ch.supply.voltage (ch.sA);
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
            const double vV3 = ch.supply.voltage (ch.sC);
            const double iV3 = (vV3 - ch.pre.voltage (ch.pPlateV2b)) / 100.0e3 + (vV3 - ch.pre.voltage (ch.pPlateV3a)) / 100.0e3
                             + ch.followerDc / 100.0e3;
            const double vV2 = ch.supply.voltage (ch.sD);
            const double iV2 = (vV2 - ch.pre.voltage (ch.pPlateV1b)) / 100.0e3;
            const double vV1 = ch.supply.voltage (ch.sE);
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateV1a)) / 180.0e3;
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
        ch.vCt = ch.supply.voltage (ch.sA);
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
        ch.piCoupling.prepare (newSampleRate, 0.022e-6, 1.0e6, ch.power.voltage (ch.wTone));
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void BognerUberschallStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.piCoupling.process (ch.power.voltage (ch.wTone))) : ch.power.voltage (ch.wOut);
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

void BognerUberschallStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
