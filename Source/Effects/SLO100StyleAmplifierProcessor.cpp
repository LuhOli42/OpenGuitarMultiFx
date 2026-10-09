#include "SLO100StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/SLO100.md, "Power supply"). Sheet 1 prints all voltages: 497V plates, 495V screens,
    // 378V (B2), 350V (B3), 359V (B4). The SLO-100 uses solid-state rectification (diode bridge), not a tube rectifier. ----
    constexpr double railPlatesNominal = 497.0;
    constexpr double railScreensNominal = 495.0;
    constexpr double rectifierResistance = 60.0;     // solid-state rectifier + transformer: much less sag than a tube rectifier
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;          // H (assumed, not printed)
    constexpr double bleeder = 100.0e3;               // not printed; assumed bleeder across the main bank
    constexpr double screenResistor = 475.0;          // 475 ohm 1W screen resistors (printed on Sheet 2)

    // ---- reduced-order power stage sag table (measured from the full reference model, or a sibling if this model's own
    // reference is unreliable -- same pattern as the JCM800). ----
    constexpr int bmSagPoints = 20;
    // Placeholder sag table: Twin Reverb's own data (same tube type, same general power section) -- to be replaced
    // once this amp's own full-topology reference model is verified stable and a real SL_POWERCAL sweep is measured.
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 497.0, 497.0, 497.0, 497.0, 496.99, 496.98, 496.96, 496.90,
                                                 496.82, 496.56, 496.18, 495.35, 494.18, 492.06, 488.09, 481.42,
                                                 473.19, 456.48, 441.11, 436.92 };

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

    /** Phase inverter 12AX7, kg1 softened for potential loop stability -- same pattern as JCM800/AC30. Start at 1x
        (the default), raise only if the full reference model self-oscillates in the 10s silence test. */
    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        // kg1 softened 40x for closed-loop stability (split with the power pentode below).
        // Without softening the full reference model self-oscillates from silence (10s peak ~1.0).
        // 10x: oscillates (peak 0.18). 30x: marginally stable (peak 0.018). 70x: stable but gain collapsed.
        p.kg1 *= 40.0;
        return p;
    }

    /** A 6L6GC PAIR: two identical tubes on the same nodes, same recipe as TwinReverbStyleAmplifierProcessor's own
        pentode6L6Pair(). Koren's own published 6L6GC set (kg1=1460, kg2=4500 etc.) as the starting point, with the
        pair scaling (kg1/kg2 halved, grid conductance doubled). */
    KorenPentode::Parameters pentode6L6Pair()
    {
        KorenPentode::Parameters p; // Koren's 6L6GC defaults
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    /** Same 6L6GC pair as pentode6L6Pair(), with kg1 softened for stability if needed. */
    KorenPentode::Parameters pentode6L6PairPower()
    {
        auto p = pentode6L6Pair();
        // kg1 softened 40x for closed-loop stability (split with the PI triode above).
        p.kg1 *= 40.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Output transformer: 4x6L6GC, 100 W. Primary plate-to-plate ~2.5k ohm (matched for 6L6GC at 497V), taps at 4/8/16 ohm.
    // The SLO-100's feedback comes from the 4 ohm tap; in this model the feedback comes from the speaker terminal,
    // with the resistance scaled to give the equivalent feedback fraction (see docs/circuits/SLO100.md).
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;     // sqrt(2500 / (4*16)) for a 16 ohm output tap
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    // Feedback: the real amp takes NFB from the 4 ohm tap through 39K + 4.7K = 43.7K. Since this model taps from the
    // 16 ohm terminal (turns ratio 2x from the 4 ohm tap), the equivalent feedback resistor at the 16 ohm terminal is
    // 43.7K * (16/4) = ~175K to deliver the same current to the PI's grid.
    constexpr double feedbackResistor = 175.0e3;

    constexpr double biasSupplyVolts = -70.0;         // assumed: trimmer at noon gives ~-55V for 6L6GC

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // 16 ohm tap for the model
    constexpr double powerTheta = 0.9;
}

SLO100StyleAmplifierProcessor::SLO100StyleAmplifierProcessor()
{
    auto onOff = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return juce::roundToInt (v) == 0 ? juce::String ("Off") : juce::String ("On"); }));
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        "slo_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("Normal") : juce::String ("Overdrive"); }));
    auto input = onOff ("slo_input", "Bright");
    auto crunch = onOff ("slo_crunch", "Crunch");
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto nGain = make ("slo_n_gain", "Normal Preamp", 0.5f);
    auto gain = make ("slo_gain", "Overdrive Preamp", 0.5f);
    auto treble = make ("slo_treble", "Treble", 0.5f);
    auto mid = make ("slo_mid", "Middle", 0.5f);
    auto bass = make ("slo_bass", "Bass", 0.5f);
    auto nMaster = make ("slo_n_master", "Normal Master", 0.5f);
    auto power = make ("slo_power", "Overdrive Master", 0.5f);
    auto presence = make ("slo_presence", "Presence", 0.3f);
    auto output = make ("slo_output", "Output", 0.5f);
    auto bias = make ("slo_bias", "Bias", 0.5f);
    auto feel = make ("slo_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "slo_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    channelParam = channel.get();
    inputParam = input.get();
    crunchParam = crunch.get();
    nGainParam = nGain.get();
    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    nMasterParam = nMaster.get();
    powerParam = power.get();
    presenceParam = presence.get();
    outputParam = output.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "slo100", "SLO-100-Style Amplifier", "|", std::move (channel));
    group->addChild (std::move (input));
    group->addChild (std::move (crunch));
    group->addChild (std::move (nGain));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (nMaster));
    group->addChild (std::move (power));
    group->addChild (std::move (presence));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("slo100_page2", "Page 2", "|", std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    constexpr double ldrOn = 1.0e3, ldrOff = 100.0e6;

    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV1 = 0, srcV2 = 0, srcV3 = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0, rNGainTop = 0, rNGainBot = 0;
        int rCrunchSeries = 0, rCrunchShunt = 0, rBrightSeries = 0;
        NodalCircuit::Node plateV1b = 0, plateV1a = 0, plateV2a = 0, plateV2b = 0, plateV3b = 0, follower = 0;
    };

    NodalCircuit::Node gainStage (NodalCircuit& c, NodalCircuit::Node grid, NodalCircuit::Node vcc, double rPlate,
                                  double rCathode, double cBypass, double plateGuess, double cathodeGuess)
    {
        const auto gnd = NodalCircuit::ground;
        const auto plate = c.addNode(), k = c.addNode();
        c.addTriode (plate, grid, k, triode12AX7());
        c.addCapacitor (grid, plate, cgp);
        c.addResistor (vcc, plate, rPlate);
        c.addResistor (k, gnd, rCathode);
        if (cBypass > 0.0)
            c.addCapacitor (k, gnd, cBypass);
        c.setInitialGuess (plate, plateGuess);
        c.setInitialGuess (k, cathodeGuess);
        return plate;
    }

    /** The SLO-100 preamp, both channels, from Rob Robinette's annotated factory schematic. Rails: B+5 (359 V, srcV1)
        for V1B/V1A/V2A, B+4 (350 V, srcV2) for V2B, B+3 (378 V, srcV3) for V3B/V3A. */
    /** Two netlists, one per channel, each solved only while its channel is selected (measured: in Overdrive the Normal
        branch's 2.2M path into V3B changes the output by < 0.01 dB, and in Normal LDR1 grounds V2B's grid, so the
        inactive branch's tubes contribute nothing). Each still carries the OTHER branch's passive input network as a
        load on V1B. `normal` selects which branch's tubes are built. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double v1Guess, double v2Guess, double v3Guess, bool normal)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc1 = c.addNode(), vcc2 = c.addNode(), vcc3 = c.addNode(), in = c.addNode();
        b.srcV1 = c.addSource (vcc1, v1Guess);
        b.srcV2 = c.addSource (vcc2, v2Guess);
        b.srcV3 = c.addSource (vcc3, v3Guess);
        b.srcIn = c.addSource (in, 0.0);

        // Preamp 1 (V1B, both channels): 68K grid stop, 1M input leak, 220K plate, 1.8K + 1uF.
        const auto g1 = c.addNode();
        c.addResistor (in, g1, 68.0e3);
        c.addResistor (g1, gnd, 1.0e6);
        b.plateV1b = gainStage (c, g1, vcc1, 220.0e3, 1.8e3, 1.0e-6, 200.0, 1.5);
        const auto p1 = c.addNode();
        c.addCapacitor (b.plateV1b, p1, 0.02e-6);

        // ---- Overdrive: 470K attenuator || 0.002uF treble peaker -> OD Preamp 500KL (fixed 0.001uF bright) -> 470K grid
        // stop -> V2A (100K, 1.8K + 1uF) -> 0.02uF -> 470K / 1M attenuator -> V2B cold clipper (100K || 0.001uF, 39K).
        const auto q = c.addNode(), odWiper = c.addNode(), g2 = c.addNode();
        c.addResistor (p1, q, 470.0e3);
        c.addCapacitor (p1, q, 0.002e-6);
        b.rGainTop = c.addResistor (q, odWiper, 250.0e3);
        b.rGainBot = c.addResistor (odWiper, gnd, 250.0e3);
        c.addCapacitor (q, odWiper, 0.001e-6);
        c.addResistor (odWiper, g2, 470.0e3);
        const auto v3in = c.addNode();
        if (! normal)
        {
            b.plateV2a = gainStage (c, g2, vcc1, 100.0e3, 1.8e3, 1.0e-6, 190.0, 1.5);
            const auto a = c.addNode(), g3 = c.addNode();
            c.addCapacitor (b.plateV2a, a, 0.02e-6);
            c.addResistor (a, g3, 470.0e3);
            c.addResistor (g3, gnd, 1.0e6);
            b.plateV2b = gainStage (c, g3, vcc2, 100.0e3, 39.0e3, 0.0, 280.0, 30.0);
            c.addCapacitor (b.plateV2b, vcc2, 0.001e-6);         // plate load bypass (high-cut)
            const auto odOut = c.addNode();
            c.addCapacitor (b.plateV2b, odOut, 0.02e-6);
            c.addResistor (odOut, gnd, 2.2e6);                   // anti-pop
            c.addResistor (odOut, v3in, ldrOn);                  // LDR2 lit: Overdrive connected to V3B
        }

        // ---- Normal: 470K (bypassed by Crunch) -> 470K (|| 470pF with Bright) -> 39K (removed by Crunch) || Normal
        // Preamp 500KL -> V1A (100K, 2.2K unbypassed) -> 0.02uF -> 2.2M || 120pF -> V3B's input (330K leak).
        const auto t = c.addNode(), n = c.addNode(), nWiper = c.addNode(), brightMid = c.addNode();
        b.rCrunchSeries = c.addResistor (p1, t, 470.0e3);
        c.addResistor (t, n, 470.0e3);
        b.rBrightSeries = c.addResistor (t, brightMid, ldrOff);
        c.addCapacitor (brightMid, n, 470.0e-12);
        b.rCrunchShunt = c.addResistor (n, gnd, 39.0e3);
        b.rNGainTop = c.addResistor (n, nWiper, 250.0e3);
        b.rNGainBot = c.addResistor (nWiper, gnd, 250.0e3);
        if (normal)
        {
            b.plateV1a = gainStage (c, nWiper, vcc1, 100.0e3, 2.2e3, 0.0, 230.0, 1.5);
            const auto cOut = c.addNode();
            c.addCapacitor (b.plateV1a, cOut, 0.02e-6);
            c.addResistor (cOut, v3in, 2.2e6);
            c.addCapacitor (cOut, v3in, 120.0e-12);
        }
        c.addResistor (v3in, gnd, 330.0e3);

        // ---- OD Preamp 4 (V3B, both channels): 220K grid stop, 220K plate, 1.8K + 1uF; V3A cathode follower.
        const auto g4 = c.addNode();
        c.addResistor (v3in, g4, 220.0e3);
        b.plateV3b = gainStage (c, g4, vcc3, 220.0e3, 1.8e3, 1.0e-6, 220.0, 1.5);
        b.follower = c.addNode();
        c.addFollower (b.plateV3b, b.follower, followerDrop);
        return b;
    }
}

void SLO100StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // The SLO-100 uses solid-state rectification with a conventional RC filter chain:
    // Rectifier -> 200uF -> 200uF -> (plates 497V, screens 495V) -> 10K -> 40uF (B2, 378V)
    // -> 15K -> 10uF (B3, 350V) -> 15K -> 10uF (B4, 359V)
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode(); // plates rail
        const auto nl = c.addNode();
        ch.sB = c.addNode(); // screens rail
        ch.sC = c.addNode(); // B2 (PI supply, 378V)
        ch.sD = c.addNode(); // B3 (V2a/V2b supply, 350V)
        ch.sE = c.addNode(); // V2 preamp supply (= B3)
        ch.sF = c.addNode(); // B4 (V1b supply, 359V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 200.0e-6);           // first filter cap (200uF/300V)
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 200.0e-6);           // second filter cap (200uF/300V)
        c.addResistor (ch.sB, gnd, bleeder);
        c.addResistor (ch.sB, ch.sC, 10.0e3);            // 10K 2W decoupling to B2
        c.addCapacitor (ch.sC, gnd, 40.0e-6);            // 40uF/450V
        c.addResistor (ch.sC, ch.sD, 15.0e3);            // 15K decoupling to B3
        c.addCapacitor (ch.sD, gnd, 10.0e-6);            // 10uF/450V
        c.addResistor (ch.sD, ch.sE, 1.0);               // B3 = V2 supply (same node, tiny R for separation)
        c.addResistor (ch.sD, ch.sF, 15.0e3);            // 15K decoupling to B4
        c.addCapacitor (ch.sF, gnd, 10.0e-6);            // 10uF/450V

        ch.iA = c.addCurrentSource (ch.sA, -0.16);       // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.012);      // power tubes screen current
        ch.iC = c.addCurrentSource (ch.sC, -0.003);      // PI current draw
        ch.iD = c.addCurrentSource (ch.sD, -0.003);      // V2a/V2b preamp current
        ch.iE = c.addCurrentSource (ch.sE, -0.001);      // (shared with sD via tiny R)
        ch.iF = c.addCurrentSource (ch.sF, -0.001);      // V1b preamp current
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 378.0);
        c.setInitialGuess (ch.sD, 350.0);
        c.setInitialGuess (ch.sE, 350.0);
        c.setInitialGuess (ch.sF, 359.0);
    }

    // ================================================================ preamp: one netlist per channel
    for (int side = 0; side < 2; ++side)
    {
        auto& p = ch.pre[(size_t) side];
        const bool normal = side == 0;
        // The follower's DC drop needs V3B's own plate voltage: build once with no drop, read it, rebuild.
        auto probe = buildPreamp (p.net, 0.0, 359.0, 350.0, 378.0, normal);
        p.net.prepare (48000.0);
        const double plate = p.net.voltage (probe.plateV3b);
        const double drop = plate - cathodeFollowerDc (378.0, plate);
        p.net = NodalCircuit {};
        auto b = buildPreamp (p.net, drop, 359.0, 350.0, 378.0, normal);
        p.srcV1 = b.srcV1;  p.srcV2 = b.srcV2;  p.srcV3 = b.srcV3;  p.srcIn = b.srcIn;
        p.rGainTop = b.rGainTop;  p.rGainBot = b.rGainBot;  p.rNGainTop = b.rNGainTop;  p.rNGainBot = b.rNGainBot;
        p.rCrunchSeries = b.rCrunchSeries;  p.rCrunchShunt = b.rCrunchShunt;  p.rBrightSeries = b.rBrightSeries;
        p.plateV1b = b.plateV1b;  p.plateV1a = b.plateV1a;  p.plateV2a = b.plateV2a;  p.plateV2b = b.plateV2b;
        p.plateV3b = b.plateV3b;  p.follower = b.follower;
    }

    // ================================================================ tone stack (always built and solved)
    // The SLO-100's TMB tone stack (from Sheet 2): 47K slope resistor from the preamp output, 470pF treble coupling,
    // .02uF bass coupling, .02uF mid coupling. Treble 250K linear, Bass 1M log, Mid 25K linear.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 470.0e-12);             // treble coupling cap
        c.addResistor (ti, nB, 47.0e3);                  // slope resistor (47K, printed on Sheet 2)
        ch.rTrebleTop = c.addResistor (top, ch.wTone, 125.0e3);   // Treble 250K linear, split
        ch.rTrebleBottom = c.addResistor (ch.wTone, nT, 125.0e3);
        c.addCapacitor (nB, nT, 0.02e-6);                // bass coupling cap
        ch.rBass = c.addResistor (nT, nM, 500.0e3);      // Bass 1M log (rheostat)
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);    // Mid 25K linear, split
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        c.addCapacitor (nB, nMw, 0.02e-6);               // mid coupling cap
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 378.0);            // PI supply from B2
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter V5: 12AX7 long-tailed pair. .02uF from OD MV wiper into grid 1 (10K series resistor + 1M
        // grid leak); 470 ohm tail; 82K/81K plate loads (slightly asymmetric for balanced drive); 47pF between plates;
        // 1M grid leak on g2 from the NFB node.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        // Power Drive is applied as a gain factor to the signal entering the tone stack (see process()),
        // so the tone stack output at wTone already has the master volume applied. A .02uF couples the tone
        // stack output to the PI's driven grid (through a 10K series resistor, matching Sheet 2).
        c.addCapacitor (ch.wTone, g1, 0.02e-6);
        c.addResistor (g1, nm, 1.0e6);                   // grid 1 leak to tail node
        c.addResistor (g2, fp, 1.0e6);                   // grid 2 leak from feedback node
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 81.0e3);                 // slightly asymmetric (81K vs 82K)
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, nm, 470.0);                    // tail resistor
        c.addResistor (nm, gnd, 10.0e3);                 // tail to ground (the SLO's tail doesn't go to a feedback node)
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 4 x 6L6GC as two push-pull pairs. .047uF couplings, 2.2K grid stoppers (per pair = 1.1K),
        // 220K grid leaks to the fixed bias rail.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.047e-6);                // smaller coupling caps than Twin Reverb's .1uF
        c.addCapacitor (pb, g4, 0.047e-6);
        c.addResistor (g3, nb, 220.0e3);
        c.addResistor (g4, nb, 220.0e3);
        c.addResistor (g3, g3s, 1.1e3);                   // 2.2K per tube / 2 for a pair
        c.addResistor (g4, g4s, 1.1e3);
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);                 // bias network from schematic: 100K
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3); // 220K trimmer portion (adjustable)
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

        // Negative feedback: from the speaker terminal through the (equivalent) feedback resistor into the PI's tail
        // node. The Presence pot (25K, linear) shunts the feedback node to ground: more presence = less NFB at HF.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 12.5e3);
        ch.rPresBottom = c.addResistor (wp, gnd, 12.5e3);
        c.addCapacitor (fp, wp, 0.1e-6);                  // .1uF in the Presence network
        c.addCapacitor (fp, gnd, 1.5e-9);                 // stray capacitance for HF stability

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -55.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void SLO100StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 25.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 25.0e3 - presBottom);
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        // Both preamp pots are 500KL (linear) on the schematic.
        const double gainBottom = juce::jmax (1.0, 500.0e3 * k.gain);
        for (auto& p : ch.pre)
        {
            p.net.setResistance (p.rGainBot, gainBottom);
            p.net.setResistance (p.rGainTop, juce::jmax (1.0, 500.0e3 - gainBottom));
        }
        const double nGainBottom = juce::jmax (1.0, 500.0e3 * k.nGain);
        for (auto& p : ch.pre)
        {
            p.net.setResistance (p.rNGainBot, nGainBottom);
            p.net.setResistance (p.rNGainTop, juce::jmax (1.0, 500.0e3 - nGainBottom));
        }
        ch.power.setResistance (ch.rTrebleTop, trebleTop);
        ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.power.setResistance (ch.rBass, bassR);
        ch.power.setResistance (ch.rMidTop, midTop);
        ch.power.setResistance (ch.rMidBottom, midBottom);
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

void SLO100StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void SLO100StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void SLO100StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    for (auto& p : ch.pre)
        p.net.restoreDynamicState (p.rest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void SLO100StyleAmplifierProcessor::updateSupply (Channel& ch) const
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
    for (auto& p : ch.pre)
    {
        p.net.setSource (p.srcV3, rail (ch.sC, 480.0));  // V3b/V3a from B2
        p.net.setSource (p.srcV2, rail (ch.sD, 480.0));  // V2b from B3
        p.net.setSource (p.srcV1, rail (ch.sF, 480.0));  // V1b/V1a/V2a from B4
    }
    ch.vScreen = rail (ch.sB, 560.0);
}

double SLO100StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double SLO100StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1bPlate: return ch.pre[(size_t) ch.activePre].net.voltage (ch.pre[(size_t) ch.activePre].plateV1b);
        case Probe::v1aPlate: return ch.pre[0].net.voltage (ch.pre[0].plateV1a);
        case Probe::v2aPlate: return ch.pre[1].net.voltage (ch.pre[1].plateV2a);
        case Probe::v2bPlate: return ch.pre[1].net.voltage (ch.pre[1].plateV2b);
        case Probe::v3bPlate: return ch.pre[(size_t) ch.activePre].net.voltage (ch.pre[(size_t) ch.activePre].plateV3b);
        case Probe::followerOut: return ch.pre[(size_t) ch.activePre].net.voltage (ch.pre[(size_t) ch.activePre].follower);
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

double SLO100StyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre[(size_t) channels[0].activePre].net.averageIterations() : channels[0].power.averageIterations();
}

int SLO100StyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void SLO100StyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double SLO100StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double SLO100StyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void SLO100StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedNGain, nGainParam, 0.02);
    setup (smoothedNMaster, nMasterParam, 0.02);
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMid, midParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ nGainParam->get(), gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

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

            for (auto& p : ch.pre)
            {
                p.net.setSource (p.srcV3, ch.supply.voltage (ch.sC));
                p.net.setSource (p.srcV2, ch.supply.voltage (ch.sD));
                p.net.setSource (p.srcV1, ch.supply.voltage (ch.sF));
                passOk = p.net.prepare (newSampleRate) && passOk;
                p.followerDc = p.net.voltage (p.follower);
            }
            ch.followerDc = ch.pre[(size_t) ch.activePre].followerDc;

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
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 81.0e3;
            }
            const double vV3 = ch.supply.voltage (ch.sC);
            const auto& od = ch.pre[1];
            const auto& nm = ch.pre[0];
            const double iV3 = (vV3 - od.net.voltage (od.plateV3b)) / 220.0e3 + ch.followerDc / 100.0e3;
            const double vV2 = ch.supply.voltage (ch.sD);
            const double iV2 = (vV2 - od.net.voltage (od.plateV2b)) / 100.0e3;
            const double vV1 = ch.supply.voltage (ch.sF);
            const double iV1 = (vV1 - od.net.voltage (od.plateV1b)) / 220.0e3 + (vV1 - nm.net.voltage (nm.plateV1a)) / 100.0e3
                             + (vV1 - od.net.voltage (od.plateV2a)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV3Run += 0.5 * (iV3 - iV3Run);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iV2Run);
            // sE is tied to sD via 1 ohm, so its current source is negligible
            ch.supply.setCurrentSource (ch.iE, 0.0);
            ch.supply.setCurrentSource (ch.iF, -iV1Run);
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
        for (auto& p : ch.pre)
        {
            p.net.setSource (p.srcV3, ch.supply.voltage (ch.sC));
            p.net.setSource (p.srcV2, ch.supply.voltage (ch.sD));
            p.net.setSource (p.srcV1, ch.supply.voltage (ch.sF));
            p.net.saveDynamicState (p.rest);
        }
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

void SLO100StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedNGain.setTargetValue (nGainParam->get());
    smoothedNMaster.setTargetValue (nMasterParam->get());
    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMid.setTargetValue (midParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    // The panel's switches and the channel LDRs (an LDR is ~1k lit, effectively open dark).
    const bool brightOn = juce::roundToInt (inputParam->get()) == 1;
    const bool crunchOn = juce::roundToInt (crunchParam->get()) == 1;
    const int channelSel = juce::roundToInt (channelParam->get()) >= 1 ? 1 : 0;
    for (auto& ch : channels)
    {
        for (auto& p : ch.pre)
        {
            p.net.setResistance (p.rBrightSeries, brightOn ? 1.0 : ldrOff);
            p.net.setResistance (p.rCrunchSeries, crunchOn ? 1.0 : 470.0e3);
            p.net.setResistance (p.rCrunchShunt, crunchOn ? ldrOff : 39.0e3);
        }
        if (channelSel != ch.activePre)
        {
            // The channel coming in starts from its own settled operating point; the output is declicked.
            auto& next = ch.pre[(size_t) channelSel];
            next.net.restoreDynamicState (next.rest);
            ch.activePre = channelSel;
            ch.followerDc = next.followerDc;
            ch.alignOutput = true;
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float ng = smoothedNGain.getNextValue();
        const float nm = smoothedNMaster.getNextValue();
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMid.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ ng, gn, tr, mi, ba, pr, nm, pw, bi, fe, speakerChoice });
        }

        // The selected channel's master (1MA each), between the shared tone stack and the phase inverter.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) (channelSel == 0 ? nm : pw)));

        // Output control: -30 dB .. 0 dB at noon .. +12 dB.
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            auto& pre = ch.pre[(size_t) ch.activePre];
            pre.net.setSource (pre.srcIn, x);
            const bool okPre = pre.net.solveSample();
            bool ok = okPre;

            // Both channels reach the tone stack through V3B and the V3A follower.
            const double preAc = pre.net.voltage (pre.follower) - ch.followerDc;
            ch.power.setSource (ch.wSrcCf, ch.followerDc + masterGain * cathodeFollowerGain * preAc);
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
                    pre.net.saveDynamicState (pre.rest);
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

void SLO100StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
