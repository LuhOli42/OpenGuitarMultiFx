#include "SuperLeadStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/SuperLead1959.md, "Power supply"). The drawing prints no voltages; these are set by the model's
    // own idle currents and a plexi's usual ~480 V on the plates. ----
    constexpr double railPlatesNominal = 480.0;   // output-transformer centre tap (before the choke)
    constexpr double rectifierResistance = 150.0; // bridge + transformer (assumed): full drive sags the plates ~50 V
    constexpr double chokeResistance = 80.0;
    constexpr double chokeInductance = 20.0;      // H (the 1967 drawing of this amp says 20H; the 2002 one gives no value)
    constexpr double bleeder = 112.0e3;           // the two 56k / 1 W equalising resistors across the second bank
    constexpr double screenResistor = 500.0;      // two 1k / 5 W screen resistors in parallel (one pair)

    // ---- reduced-order power stage (2026-09-27): calibration data, SL_POWERCAL in the test file ----
    // Plate rail vs. the post-tone-stack drive PEAK, measured on the full reference model with a slow (settled) sine at each
    // level so the supply reaches its own quasi-equilibrium (Power Drive at max, Loudness I/II at 0.8, matched speaker). The
    // 6.238 V point's speaker RMS (57.9 V pk / 40.99 V rms) is EXACTLY the doc's published "105 W at 3.3% THD" figure
    // (40.99^2 / 16 = 105.0 W) -- the anchor that says this data is real, not guessed.
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

SuperLeadStyleAmplifierProcessor::SuperLeadStyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "sl_input", "Input", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("Normal"); case 2: return juce::String ("Bright"); default: return juce::String ("Jumped"); }
        }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto normal = make ("sl_loud2", "Loudness II", 0.4f);
    auto bright = make ("sl_loud1", "Loudness I", 0.4f);
    auto treble = make ("sl_treble", "Treble", 0.5f);
    auto middle = make ("sl_middle", "Middle", 0.5f);
    auto bass = make ("sl_bass", "Bass", 0.5f);
    auto presence = make ("sl_presence", "Presence", 0.3f);
    auto output = make ("sl_output", "Output", 0.5f);
    auto power = make ("sl_power", "Power Drive", 1.0f);
    auto bias = make ("sl_bias", "Bias", 0.5f);
    auto feel = make ("sl_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "sl_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
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
        "superlead", "Super Lead-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (normal));
    group->addChild (std::move (bright));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("superlead_page2", "Page 2", "|", std::move (power));
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
        int srcV1 = 0, srcV2 = 0, srcInNormal = 0, srcInBright = 0;
        int rVolTop[2] {}, rVolBot[2] {};
        NodalCircuit::Node mix = 0, plate1 = 0, plate2 = 0, follower = 0, plateBright = 0, plateNormal = 0, nodeV1 = 0, nodeV2 = 0, kFollowerCurrentNode = 0;
    };

    /** V1's two triodes, both Loudness pots (+ the bright channel's caps), the mixing resistors and V2A; V2B (the cathode
        follower) is an ideal follower with a DC drop of `followerDrop`. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double v1Guess, double v2Guess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc1 = c.addNode(), vcc2 = c.addNode(), inNormal = c.addNode(), inBright = c.addNode();
        b.nodeV1 = vcc1;
        b.nodeV2 = vcc2;
        b.srcV1 = c.addSource (vcc1, v1Guess);
        b.srcV2 = c.addSource (vcc2, v2Guess);
        b.srcInNormal = c.addSource (inNormal, 0.0);
        b.srcInBright = c.addSource (inBright, 0.0);

        // G: V2A's grid, where both channels' 470k mixing resistors meet
        b.mix = c.addNode();

        for (int ch = 0; ch < 2; ++ch) // 0 = Channel II (normal), 1 = Channel I (bright)
        {
            const auto g = c.addNode(), p = c.addNode(), k = c.addNode(), cp = c.addNode(), w = c.addNode();
            c.addResistor (ch == 0 ? inNormal : inBright, g, 68.0e3); // grid stopper: each channel's OWN jack
            c.addTriode (p, g, k, triode12AX7());
            c.addCapacitor (g, p, cgp);
            c.addResistor (vcc1, p, 100.0e3);
            c.addResistor (k, gnd, 820.0);
            c.addCapacitor (k, gnd, ch == 0 ? 330.0e-6 : 680.0e-9); // Channel II: full bass; Channel I: bass rolled off
            c.addCapacitor (p, cp, ch == 0 ? 22.0e-9 : 3.3e-9);
            b.rVolTop[ch] = c.addResistor (cp, w, 500.0e3);
            b.rVolBot[ch] = c.addResistor (w, gnd, 500.0e3);
            if (ch == 1)
            {
                c.addCapacitor (cp, w, 4.7e-9);        // the bright channel's treble bypass across its Loudness pot
                c.addCapacitor (w, b.mix, 470.0e-12);
            }
            c.addResistor (w, b.mix, 470.0e3);
            c.setInitialGuess (p, 230.0);
            c.setInitialGuess (k, 0.8);
            if (ch == 1)
                b.plateBright = p;
            else
                b.plateNormal = p;
        }

        const auto k2 = c.addNode();
        b.plate2 = c.addNode();
        c.addTriode (b.plate2, b.mix, k2, triode12AX7());
        c.addCapacitor (b.mix, b.plate2, cgp);
        c.addResistor (vcc2, b.plate2, 100.0e3);
        c.addResistor (k2, gnd, 820.0);
        c.addCapacitor (k2, gnd, 680.0e-9);
        c.setInitialGuess (b.plate2, 210.0);
        c.setInitialGuess (k2, 1.0);

        b.follower = c.addNode();
        c.addFollower (b.plate2, b.follower, followerDrop);
        return b;
    }
}

void SuperLeadStyleAmplifierProcessor::buildChannel (Channel& ch)
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
        ch.pPlateBright = b.plateBright;
        ch.pPlateNormal = b.plateNormal;
    }

    // ================================================================ tone stack (ALWAYS built and solved: a real linear
    // circuit either way, so it costs nothing extra in reducedOrder mode -- see the header's "reduced-order power stage" note)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        // Tone stack (the Marshall "TMB" stack): C8 470 pF from the follower into the Treble pot's top, R13 33k from the follower into the
        // stack's own node B, C9 22 nF from B to the Treble pot's bottom / the Bass wiper, the Bass pot (1M, a rheostat) into the Middle
        // pot (25k) to ground, C10 22 nF from B to the Middle wiper. The Treble wiper is the output.
        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 470.0e-12);
        c.addResistor (ti, nB, 33.0e3);
        ch.rTrebleTop = c.addResistor (top, ch.wTone, 125.0e3);
        ch.rTrebleBottom = c.addResistor (ch.wTone, nT, 125.0e3);
        c.addCapacitor (nB, nT, 22.0e-9);
        ch.rBass = c.addResistor (nT, nM, 500.0e3);
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        c.addCapacitor (nB, nMw, 22.0e-9);
    }

    // ================================================================ phase inverter, power amp -- the FULL reference
    // netlist only (reducedOrder replaces all of this with behavioralPowerStage(), fitted to this same circuit's own
    // measured output -- docs/circuits/SuperLead1959.md)
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
        c.addTriode (pa, g1, k, triode12AX7());
        c.addTriode (pb, g2, k, triode12AX7());
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
        ch.penA = c.addPentode (pp1, g3s, gnd, pentodeEL34Pair(), railPlatesNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentodeEL34Pair(), railPlatesNominal);

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

void SuperLeadStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Loudness I / II: 1M audio taper (pots::audio: 15% at half rotation).
    const double vols[2] = { k.volNormal, k.volBright };
    // Treble 250k linear (wiper toward the 470 pF end = more treble), Bass 1M audio (a rheostat: more resistance = more bass),
    // Middle 25k linear with a real wiper tap, Presence 5k linear (the wiper-to-ground segment shrinks as Presence rises).
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.middle);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
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
        for (int i = 0; i < 2; ++i)
        {
            const double bottom = juce::jmax (1.0, 1.0e6 * pots::audio (vols[i]));
            ch.pre.setResistance (ch.rVolBot[i], bottom);
            ch.pre.setResistance (ch.rVolTop[i], juce::jmax (1.0, 1.0e6 - bottom));
        }
        ch.power.setResistance (ch.rTrebleTop, trebleTop);
        ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.power.setResistance (ch.rBass, bassR);
        ch.power.setResistance (ch.rMidTop, midTop);
        ch.power.setResistance (ch.rMidBottom, midBottom);
        // reducedOrder: the Presence pot's physical resistors don't exist in the tone-stack-only power circuit, so
        // its effect is reproduced inside behavioralPowerStage() instead -- presenceMix is the measured closed-loop
        // gain of opening the feedback divider by the pot's cap-bypassed fraction (see the header's bmPres* note).
        ch.presenceMix = bmPresMixK * k.presence / juce::jmax (1.0e-3, 1.0 - bmPresMixR * k.presence);
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

void SuperLeadStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void SuperLeadStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void SuperLeadStyleAmplifierProcessor::recover (Channel& ch) const
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

void SuperLeadStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    // The supply sees what four output tubes can physically draw (an EL34 peaks around 0.5 A): a solver excursion in the power block
    // must not be able to turn into an absurd current that wrecks the rails of every other stage.
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
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

double SuperLeadStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
{
    // Envelope follower driving the sag lookup: fast attack (a rail sags quickly under a sudden hard hit), slower release
    // (a real capacitor bank recharges through the rectifier's resistance over tens of ms) -- matches the amp's documented
    // ~13 Hz supply ring (~12 ms) better than a single symmetric time constant.
    // Presence first: the 5k+100nF leg in the feedback path opens the loop progressively at HF -- modelled as the
    // resonant high-pass fitted to the reference's measured presence law, adding effective drive at the power
    // section's input. Putting it BEFORE the knee keeps the total bounded by the rail like the real amp (and below
    // the 150 V sanity limit -- a post-saturation add blew past it and tripped recoveries every sample).
    const double hpIn = bmHpB0 * toneVoltage + bmHpB1 * ch.bmHpX1 + bmHpB2 * ch.bmHpX2
                      - bmHpA1 * ch.bmHpY1 - bmHpA2 * ch.bmHpY2;
    ch.bmHpX2 = ch.bmHpX1; ch.bmHpX1 = toneVoltage;
    ch.bmHpY2 = ch.bmHpY1; ch.bmHpY1 = hpIn;
    const double drive = toneVoltage + ch.presenceMix * hpIn;
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (drive);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    // Saturating curve fitted to the real closed-loop transfer (docs/circuits/SuperLead1959.md): small-signal gain bmGain0,
    // peak output bmYmax of the (sagged) rail at full saturation, knee sharpness bmKneeN. K is the knee's location in
    // absolute volts AT THE CURRENT RAIL (a lighter-sagged rail clips later, in volts, exactly like the real amp).
    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, drive);

    // The frequency response the removed stages used to provide: a fixed biquad fitted to the reference netlist's own
    // measured transfer (mid dip + rising top, NOT the single falling shelf it replaces -- see the header).
    const double base = bmBaseB0 * raw + bmBaseB1 * ch.bmBaseX1 + bmBaseB2 * ch.bmBaseX2
                      - bmBaseA1 * ch.bmBaseY1 - bmBaseA2 * ch.bmBaseY2;
    ch.bmBaseX2 = ch.bmBaseX1; ch.bmBaseX1 = raw;
    ch.bmBaseY2 = ch.bmBaseY1; ch.bmBaseY1 = base;
    ch.bmOutput = base * bmLevelTrim;
    return ch.bmOutput;
}

void SuperLeadStyleAmplifierProcessor::designPowerFilters()
{
    // Bilinear transform (s = c(1-z^-1)/(1+z^-1), c = 2*fs) of an analog biquad n2 s^2 + n1 s + n0 over d2 s^2 + d1 s + d0.
    const auto bilinear = [] (double n2, double n1, double n0, double d2, double d1, double d0, double fs,
                              double& b0, double& b1, double& b2, double& a1, double& a2)
    {
        const double c = 2.0 * fs;
        const double A0 = d2 * c * c + d1 * c + d0;
        a1 = 2.0 * (d0 - d2 * c * c) / A0;
        a2 = (d2 * c * c - d1 * c + d0) / A0;
        b0 = (n2 * c * c + n1 * c + n0) / A0;
        b1 = 2.0 * (n0 - n2 * c * c) / A0;
        b2 = (n2 * c * c - n1 * c + n0) / A0;
    };
    const double z1 = 2.0 * juce::MathConstants<double>::pi * bmBaseZ1Hz, z2 = 2.0 * juce::MathConstants<double>::pi * bmBaseZ2Hz;
    const double p1 = 2.0 * juce::MathConstants<double>::pi * bmBaseP1Hz, p2 = 2.0 * juce::MathConstants<double>::pi * bmBaseP2Hz;
    bilinear (bmBaseDc / (z1 * z2), bmBaseDc * (1.0 / z1 + 1.0 / z2), bmBaseDc,
              1.0 / (p1 * p2), 1.0 / p1 + 1.0 / p2, 1.0,
              sampleRate, bmBaseB0, bmBaseB1, bmBaseB2, bmBaseA1, bmBaseA2);
    const double wc = 2.0 * juce::MathConstants<double>::pi * bmPresHz, wz = 2.0 * juce::MathConstants<double>::pi * bmPresZeroHz;
    bilinear (1.0, wz, 0.0,
              1.0, wc / bmPresQ, wc * wc,
              sampleRate, bmHpB0, bmHpB1, bmHpB2, bmHpA1, bmHpA2);
    // Normalize the presence section to unity at 6 kHz so presenceMix is the measured gain law directly.
    const double w6 = 2.0 * juce::MathConstants<double>::pi * 6000.0 / sampleRate;
    const double c1 = std::cos (w6), s1n = std::sin (w6), c2 = std::cos (2.0 * w6), s2 = std::sin (2.0 * w6);
    const double nr = bmHpB0 + bmHpB1 * c1 + bmHpB2 * c2, ni = -bmHpB1 * s1n - bmHpB2 * s2;
    const double dr = 1.0 + bmHpA1 * c1 + bmHpA2 * c2, di = -bmHpA1 * s1n - bmHpA2 * s2;
    const double mag = std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    bmHpB0 /= mag; bmHpB1 /= mag; bmHpB2 /= mag;
}

double SuperLeadStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::gainStagePlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::mixNode: return ch.pre.voltage (ch.pMix);
        case Probe::channelIPlate: return ch.pre.voltage (ch.pPlateBright);
        case Probe::channelIIPlate: return ch.pre.voltage (ch.pPlateNormal);
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

double SuperLeadStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int SuperLeadStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void SuperLeadStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return; // no feedback resistor node exists in this mode (see the header's "reduced-order power stage" note)
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double SuperLeadStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0; // no pentode devices exist in this mode (see the header's "reduced-order power stage" note)
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double SuperLeadStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void SuperLeadStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedVolNormal, volNormalParam, 0.02);
    setup (smoothedVolBright, volBrightParam, 0.02);
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
    updatePots ({ volNormalParam->get(), volBrightParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(),
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
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateNormal)) / 100.0e3 + (vV1 - ch.pre.voltage (ch.pPlateBright)) / 100.0e3;
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
        designPowerFilters();
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
        ch.bmBaseX1 = ch.bmBaseX2 = ch.bmBaseY1 = ch.bmBaseY2 = 0.0;
        ch.bmHpX1 = ch.bmHpX2 = ch.bmHpY1 = ch.bmHpY2 = 0.0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void SuperLeadStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

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

        // Power Drive: a master volume between the preamp and the tone stack / phase inverter (amplitude = audio taper).
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

        // Output control: -30 dB .. 0 dB at noon .. +12 dB.
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            // Input selector: the same guitar cable, patched into one or both channels' jacks (the plexi's jumper-cable trick).
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

void SuperLeadStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
