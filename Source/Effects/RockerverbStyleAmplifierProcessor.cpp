#include "RockerverbStyleAmplifierProcessor.h"
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

    // ---- supply (ORA-CD204 schematic, 27 Feb 2004).
    // 290Vac rectified -> ~410V nominal.
    // Chain: Rect -> A (47µF) -> R1=4K7 -> B (47µF) -> R33=4K7 -> C (22µF) -> R34=4K7 -> D (22µF) -> R41=10K -> E (22µF)
    // Rails: A=plates, B=screens/PI, C=V8 preamp rail, D/E=V9 preamp rails.
    constexpr double railPlatesNominal = 410.0;
    constexpr double rectifierResistance = 60.0;
    constexpr double bleeder = 100.0e3;
    constexpr double screenResistor = 470.0; // 470Ω screen resistors (same pattern as Powerball)

    // ---- reduced-order power stage sag table (scaled from Deluxe Reverb's 6V6 data, 415V -> 410V) ----
    constexpr int bmSagPoints = 17;
    constexpr double bmSagDrive[bmSagPoints] = { 0.000425, 0.002122, 0.006367, 0.014859, 0.025478, 0.042476, 0.063733,
                                                  0.106259, 0.170016, 0.255145, 0.383284, 0.597788, 0.855871, 1.157292,
                                                  1.387650, 1.472226, 1.487870 };
    constexpr double bmSagRail[bmSagPoints] = { 410.0, 410.0, 409.99, 409.94, 409.82, 409.50, 408.88,
                                                 406.98, 403.11, 399.49, 396.99, 395.63, 395.04, 394.68,
                                                 394.42, 394.25, 394.16 };

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

    /** 6V6 pair: merge two tubes (kg1*0.5, kg2*0.5, Gg*2.0, arcResistance*0.5) with kg1*40 stability.
        Published Koren 6V6GT: kg1=1400 — same pattern as EVH5150's 6L6GC pair.
        Full-model output is low (same as all kg1*40 amps); the behavioral model handles real output. */
    KorenPentode::Parameters pentode6V6Pair()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 1400.0; // published Koren 6V6GT
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        // merge pair
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        // stability
        p.kg1 *= 40.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer: 4x6V6 ~50W. Same turns ratio as Deluxe Reverb (similar tube type).
    constexpr double primaryHalfInductance = 5.0;
    constexpr double halfToSecondaryTurns = 11.18;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.997;
    constexpr double primaryHalfResistance = 55.0;
    constexpr double secondaryResistance = 0.15;
    // NFB: R10=4K7 from OT secondary to PI grid B through 150K.
    constexpr double feedbackResistor = 4.7e3 + 150.0e3; // combined feedback path

    constexpr double biasSupplyVolts = -40.0; // 6V6 fixed bias

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // 8 ohm matched
    constexpr double powerTheta = 0.9;

    // ---- preamp helper ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcE = 0, srcD = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node plateV9a = 0, plateV9b = 0, plateV8a = 0, plateV8b = 0;
        NodalCircuit::Node nodeE = 0, nodeD = 0;
    };

    /** Orange Rockerverb 50 MK1 Dirty channel preamp: FOUR cascaded 12AX7 gain stages (V9-A -> V9-B -> V8-A -> V8-B).
        V9-A/V9-B from rail F (~E), V8-A/V8-B from rail E (~D). */
    PreampBuild buildPreamp (NodalCircuit& c, double eGuess, double dGuess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vccE = c.addNode(), vccD = c.addNode(), in = c.addNode();
        b.nodeE = vccE;
        b.nodeD = vccD;
        b.srcE = c.addSource (vccE, eGuess);
        b.srcD = c.addSource (vccD, dGuess);
        b.srcIn = c.addSource (in, 0.0);

        // V9-A: R32=100K plate load (rail F/E), R47=68K grid stopper, 1M grid leak, R46=1K5 cathode bypassed with C27=10µF.
        // Input coupling C24=220nF from input.
        const auto g1 = c.addNode(), k1 = c.addNode(), coup1 = c.addNode();
        b.plateV9a = c.addNode();
        c.addCapacitor (in, coup1, 220.0e-9);               // C24: 220nF input coupling
        c.addResistor (coup1, g1, 68.0e3);                  // R47 grid stopper
        c.addResistor (g1, gnd, 1.0e6);                     // 1M grid leak
        c.addTriode (b.plateV9a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateV9a, cgp);
        c.addResistor (vccE, b.plateV9a, 100.0e3);          // R32: 100K plate load
        c.addResistor (k1, gnd, 1.5e3);                     // R46: 1K5 cathode
        c.addCapacitor (k1, gnd, 10.0e-6);                  // C27: 10µF bypass
        c.setInitialGuess (b.plateV9a, 200.0);
        c.setInitialGuess (k1, 1.5);

        // V9-B: R38=100K plate load (rail E), R53=220K grid leak, 1K5 cathode bypassed with 10µF.
        // Coupling from V9-A plate through C31=1nF.
        const auto g2 = c.addNode(), k2 = c.addNode(), coup2 = c.addNode();
        b.plateV9b = c.addNode();
        c.addCapacitor (b.plateV9a, coup2, 1.0e-9);         // C31: 1nF coupling
        c.addResistor (coup2, g2, 10.0e3);                  // series stopper (estimated)
        c.addResistor (g2, gnd, 220.0e3);                   // R53: 220K grid leak
        c.addTriode (b.plateV9b, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV9b, cgp);
        c.addResistor (vccD, b.plateV9b, 100.0e3);          // R38: 100K plate load (rail E)
        c.addResistor (k2, gnd, 1.5e3);                     // 1K5 cathode
        c.addCapacitor (k2, gnd, 10.0e-6);                  // 10µF bypass
        c.setInitialGuess (b.plateV9b, 250.0);
        c.setInitialGuess (k2, 1.5);

        // GAIN pot: RV4=1MA between V9-B and V8-A, through coupling cap (22nF estimated).
        const auto gainIn = c.addNode(), gainWiper = c.addNode();
        c.addCapacitor (b.plateV9b, gainIn, 22.0e-9);       // coupling cap (estimated 22nF)
        c.addResistor (gainIn, gnd, 470.0e3);                // bias reference
        b.rGainTop = c.addResistor (gainIn, gainWiper, 1.0e6);
        b.rGainBot = c.addResistor (gainWiper, gnd, 1.0e6);

        // V8-A: 100K plate load (rail E/D), 220K grid leak, 1K5 cathode bypassed with 10µF.
        // 100pF bright cap on coupling (estimated).
        const auto g3 = c.addNode(), k3 = c.addNode();
        b.plateV8a = c.addNode();
        c.addResistor (gainWiper, g3, 10.0e3);              // series stopper
        c.addResistor (g3, gnd, 220.0e3);                   // 220K grid leak
        c.addTriode (b.plateV8a, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateV8a, cgp);
        c.addResistor (vccD, b.plateV8a, 100.0e3);          // 100K plate load
        c.addResistor (k3, gnd, 1.5e3);                     // 1K5 cathode
        c.addCapacitor (k3, gnd, 10.0e-6);                  // 10µF bypass
        c.setInitialGuess (b.plateV8a, 250.0);
        c.setInitialGuess (k3, 1.5);

        // V8-B: 100K plate load (rail E/D), 220K grid leak, 1K5 cathode bypassed with 10µF.
        const auto g4 = c.addNode(), k4 = c.addNode(), coup4 = c.addNode();
        b.plateV8b = c.addNode();
        c.addCapacitor (b.plateV8a, coup4, 22.0e-9);        // coupling cap
        c.addResistor (coup4, g4, 10.0e3);                  // series stopper
        c.addResistor (g4, gnd, 220.0e3);                   // 220K grid leak
        c.addTriode (b.plateV8b, g4, k4, triode12AX7());
        c.addCapacitor (g4, b.plateV8b, cgp);
        c.addResistor (vccD, b.plateV8b, 100.0e3);          // 100K plate load
        c.addResistor (k4, gnd, 1.5e3);                     // 1K5 cathode
        c.addCapacitor (k4, gnd, 10.0e-6);                  // 10µF bypass
        c.setInitialGuess (b.plateV8b, 250.0);
        c.setInitialGuess (k4, 1.5);

        return b;
    }
}

RockerverbStyleAmplifierProcessor::RockerverbStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto gain = make ("rv_gain", "Gain", 0.5f);
    auto treble = make ("rv_treble", "Treble", 0.5f);
    auto mid = make ("rv_mid", "Mid", 0.5f);
    auto bass = make ("rv_bass", "Bass", 0.5f);
    auto presence = make ("rv_presence", "Presence", 0.3f);
    auto post = make ("rv_post", "Master", 0.5f);
    auto output = make ("rv_output", "Output", 0.5f);
    auto power = make ("rv_power", "Power Drive", 0.5f);
    auto bias = make ("rv_bias", "Bias", 0.5f);
    auto feel = make ("rv_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "rv_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    postParam = post.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "rockerverb", "Rockerverb-Style Amplifier", "|", std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (post));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("rv_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void RockerverbStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // 290Vac rectified -> ~410V. RC chain:
    // Rect -> A (47µF) -> 4K7 -> B (47µF) -> 4K7 -> C (22µF) -> 4K7 -> D (22µF) -> 10K -> E (22µF)
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~410V)
        ch.sB = c.addNode();                           // screens/PI rail
        ch.sC = c.addNode();                           // V8 preamp rail
        ch.sD = c.addNode();                           // V9 preamp rail (E on schematic)
        ch.sE = c.addNode();                           // V9 preamp rail (F on schematic)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 47.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);
        c.addResistor (ch.sA, ch.sB, 4.7e3);
        c.addCapacitor (ch.sB, gnd, 47.0e-6);
        c.addResistor (ch.sB, ch.sC, 4.7e3);
        c.addCapacitor (ch.sC, gnd, 22.0e-6);
        c.addResistor (ch.sC, ch.sD, 4.7e3);
        c.addCapacitor (ch.sD, gnd, 22.0e-6);
        c.addResistor (ch.sD, ch.sE, 10.0e3);
        c.addCapacitor (ch.sE, gnd, 22.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.10);        // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.008);       // power tubes screen current + PI
        ch.iC = c.addCurrentSource (ch.sC, -0.003);       // V8 preamp current
        ch.iD = c.addCurrentSource (ch.sD, -0.003);       // V9 preamp current (E rail)
        ch.iE = c.addCurrentSource (ch.sE, -0.003);       // V9 preamp current (F rail)
        for (auto n : { ch.sA, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 390.0);
        c.setInitialGuess (ch.sD, 380.0);
        c.setInitialGuess (ch.sE, 370.0);
    }

    // ================================================================ preamp
    {
        auto b = buildPreamp (ch.pre, 370.0, 390.0);
        ch.pSrcE = b.srcE;
        ch.pSrcD = b.srcD;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.pPlateV9a = b.plateV9a;
        ch.pPlateV9b = b.plateV9b;
        ch.pPlateV8a = b.plateV8a;
        ch.pPlateV8b = b.plateV8b;
    }

    // ================================================================ tone stack + master + PI + power amp
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);

        // Pre->Power coupling: AC-coupled from V8-B plate.
        const auto tsIn = c.addNode();
        ch.wSrcTs = c.addSource (tsIn, 0.0);

        // FMV tone stack: 560pF treble cap, 39K slope, 22n bass-mid, 22n mid,
        // 250KB treble, 1MA bass, 25KB mid.
        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        const auto postNode = c.addNode();
        ch.wTone = c.addNode();
        c.addResistor (tsIn, ti, 38.0e3);                  // source impedance (100K||62.5K ≈ 38K)
        c.addCapacitor (ti, top, 560.0e-12);                // 560pF treble coupling cap
        c.addResistor (ti, nB, 39.0e3);                     // 39K slope resistor
        ch.rTrebleTop = c.addResistor (top, postNode, 125.0e3);    // 250KB treble, split
        ch.rTrebleBottom = c.addResistor (postNode, nT, 125.0e3);
        c.addResistor (nT, nB, 22.0e3);                    // series between treble and bass
        c.addCapacitor (nB, nT, 22.0e-9);                   // 22nF bass cap
        ch.rBass = c.addResistor (nT, nM, 500.0e3);        // 1MA bass, rheostat
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);      // 25KB mid, split
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        c.addCapacitor (nB, nMw, 22.0e-9);                  // 22nF mid cap

        // Master (500KA rheostat): 100nF coupling from tone stack out, 1M to ground
        const auto masterCoup = c.addNode();
        c.addCapacitor (postNode, masterCoup, 100.0e-9);    // 100nF coupling from tone out
        c.addResistor (masterCoup, gnd, 1.0e6);             // 1M to ground
        ch.rPost = c.addResistor (masterCoup, ch.wTone, 250.0e3); // 500KA master (half nominal for rheostat)
    }

    // ================================================================ phase inverter + power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 400.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter: ECC83 (12AX7) long-tailed pair.
        // 82K/100K plate loads, 10K tail + 47K to ground, 100nF coupling from master to grid A.
        // Cross-coupling: 47nF from PI plate B to grid B.
        // NFB: from OT secondary through 150K + 4K7 to grid B.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (ch.wTone, g1, 100.0e-9);             // 100nF coupling from master
        c.addResistor (g1, gnd, 1.0e6);                      // R9: 1M grid leak
        c.addResistor (g2, fp, 1.0e6);                       // grid leak for grid B
        // Cross-coupling: 47nF from plate B to grid B
        c.addCapacitor (pb, g2, 47.0e-9);
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);                     // plate load A
        c.addResistor (vpi, pb, 100.0e3);                    // plate load B
        c.addResistor (k, nm, 10.0e3);                       // cathode to tail
        c.addResistor (nm, gnd, 47.0e3);                     // tail to ground
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 4 × 6V6 as two push-pull pairs.
        // 47nF coupling caps to power tube grids.
        // 1K0 grid stoppers, 220K grid leaks to bias.
        // 1K5 cathode resistors (individual, unbypassed — merged as 750Ω per pair).
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 47.0e-9);                    // coupling cap
        c.addCapacitor (pb, g4, 47.0e-9);                    // coupling cap
        c.addResistor (g3, nb, 220.0e3);                     // grid leak
        c.addResistor (g4, nb, 220.0e3);                     // grid leak
        c.addResistor (g3, g3s, 1.0e3);                      // grid stopper (1K0 per pair)
        c.addResistor (g4, g4s, 1.0e3);                      // grid stopper
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3);
        // Fixed bias, cathodes to ground (same proven pattern as 5150/Powerball)
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6V6Pair(), railPlatesNominal - 24.0);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6V6Pair(), railPlatesNominal - 24.0);

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

        // NFB: from speaker terminal through feedbackResistor to PI grid 2, with Presence 250K shunt.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 125.0e3);      // Presence 250K, split
        ch.rPresBottom = c.addResistor (wp, gnd, 125.0e3);
        c.addCapacitor (fp, wp, 0.01e-6);                    // HF feedback cap
        c.addCapacitor (fp, gnd, 1.5e-9);                    // stray for HF stability

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -40.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void RockerverbStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 250.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 250.0e3 - presBottom);
    const double postR = juce::jmax (1.0, 500.0e3 * pots::audio (k.post));
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
        ch.power.setResistance (ch.rPost, postR);
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

void RockerverbStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void RockerverbStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void RockerverbStyleAmplifierProcessor::recover (Channel& ch) const
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

void RockerverbStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! supplyCurrentFrozen)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.0, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.2, ch.sumScreen / n));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 500.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sB, 480.0));
    }
    ch.pre.setSource (ch.pSrcD, rail (ch.sC, 460.0));    // V8 from C
    ch.pre.setSource (ch.pSrcE, rail (ch.sE, 460.0));    // V9 from E
    ch.vScreen = rail (ch.sB, 500.0);
}

double RockerverbStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double RockerverbStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v9aPlate: return ch.pre.voltage (ch.pPlateV9a);
        case Probe::v9bPlate: return ch.pre.voltage (ch.pPlateV9b);
        case Probe::v8aPlate: return ch.pre.voltage (ch.pPlateV8a);
        case Probe::v8bPlate: return ch.pre.voltage (ch.pPlateV8b);
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

double RockerverbStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int RockerverbStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void RockerverbStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double RockerverbStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double RockerverbStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void RockerverbStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedPost, postParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.12;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), postParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iCRun = 0.003, iDRun = 0.003, iERun = 0.003, ipRun = 0.10, isRun = 0.008;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcD, ch.supply.voltage (ch.sC));
            ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.plateDcV8b = ch.pre.voltage (ch.pPlateV8b);

            ch.power.setSource (ch.wSrcTs, 0.0); // DC coupling = 0 (AC only)
            ch.vScreen = ch.supply.voltage (ch.sB);
            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0, iPi = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB));
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
                const double vPi = ch.supply.voltage (ch.sB);
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
            }
            const double vC = ch.supply.voltage (ch.sC);
            const double iC = (vC - ch.pre.voltage (ch.pPlateV8a)) / 100.0e3
                             + (vC - ch.pre.voltage (ch.pPlateV8b)) / 100.0e3;
            const double vE = ch.supply.voltage (ch.sE);
            const double iE = (vE - ch.pre.voltage (ch.pPlateV9a)) / 100.0e3;
            const double vD = ch.supply.voltage (ch.sD);
            const double iD = (vD - ch.pre.voltage (ch.pPlateV9b)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iCRun += 0.5 * (iC - iCRun);
            iDRun += 0.5 * (iD - iDRun);
            iERun += 0.5 * (iE - iERun);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + iPi));
            ch.supply.setCurrentSource (ch.iC, -iCRun);
            ch.supply.setCurrentSource (ch.iD, -iDRun);
            ch.supply.setCurrentSource (ch.iE, -iERun);
            idleSupplyCurrent = ipRun + isRun + iPi + iCRun + iDRun + iERun + (ch.vScreen / bleeder);
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB));
        }
        ch.pre.setSource (ch.pSrcD, ch.supply.voltage (ch.sC));
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

void RockerverbStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
    smoothedPost.setTargetValue (postParam->get());
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
        const float ps = smoothedPost.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, ps, pw, bi, fe, speakerChoice });
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

            // Pre->Power AC coupling: subtract V8-B plate DC
            const double preOut = masterGain * (ch.pre.voltage (ch.pPlateV8b) - ch.plateDcV8b);
            ch.power.setSource (ch.wSrcTs, preOut);
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

void RockerverbStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
