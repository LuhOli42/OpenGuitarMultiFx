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

    // Published Koren/Duncan 12AT7 (ECC81) set, for V7-A/V7-B (outside any feedback loop, so no stability softening).
    KorenTriode::Parameters triode12AT7()
    {
        KorenTriode::Parameters p;
        p.mu = 60.0;
        p.kg1 = 272.0;
        p.kp = 225.0;
        p.kvb = 54.7;
        p.ex = 1.5;
        return p;
    }

    // The tone outputs are loaded by R4 220k -> C4 -> V7-A's 1M bias resistor when RL1 selects them.
    constexpr double relayLoad = 220.0e3 + 1.0e6;
    constexpr double relayOpen = 100.0e6;

    // ---- preamp block (ORA-CD204 sheet 1/2): both channels and both tone stacks, as drawn ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcF = 0, srcE = 0, srcIn = 0;
        int rGainBTop = 0, rGainBBot = 0, rGainATop = 0, rGainABot = 0;
        int rDTrebleTop = 0, rDTrebleBot = 0, rDBass = 0, rDMidTop = 0, rDMidBot = 0, rDVolTop = 0, rDVolBot = 0;
        int rCVolTop = 0, rCVolBot = 0, rCTrebleTop = 0, rCTrebleBot = 0, rCBassTop = 0, rCBassBot = 0;
        int rLoadDirty = 0, rLoadClean = 0;
        NodalCircuit::Node plateV9a = 0, plateV9b = 0, plateV8a = 0, plateV8b = 0, plateV10a = 0, plateV10b = 0;
        NodalCircuit::Node dirtyOut = 0, cleanOut = 0;
    };

    /** A 12AX7 gain stage: plate load from `vcc`, cathode resistor (+ bypass cap when > 0). Returns the plate node. */
    NodalCircuit::Node triodeStage (NodalCircuit& c, NodalCircuit::Node grid, NodalCircuit::Node vcc, double rPlate,
                                    double rCathode, double cBypass, double plateGuess)
    {
        const auto gnd = NodalCircuit::ground;
        const auto plate = c.addNode(), k = c.addNode();
        c.addTriode (plate, grid, k, KorenTriode::Parameters {});
        c.addCapacitor (grid, plate, cgp);
        c.addResistor (vcc, plate, rPlate);
        c.addResistor (k, gnd, rCathode);
        if (cBypass > 0.0)
            c.addCapacitor (k, gnd, cBypass);
        c.setInitialGuess (plate, plateGuess);
        c.setInitialGuess (k, 1.5);
        return plate;
    }

    PreampBuild buildPreamp (NodalCircuit& c, double fGuess, double eGuess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vccF = c.addNode(), vccE = c.addNode(), in = c.addNode();
        b.srcF = c.addSource (vccF, fGuess);
        b.srcE = c.addSource (vccE, eGuess);
        b.srcIn = c.addSource (in, 0.0);

        // Input: C24 220n -> R42 1M0 to ground (shared grid leak) -> R47 68K to V9-A, R44 68K to V10-A.
        const auto inNode = c.addNode();
        c.addCapacitor (in, inNode, 220.0e-9);
        c.addResistor (inNode, gnd, 1.0e6);

        // ---------------- Dirty channel ----------------
        const auto g9a = c.addNode();
        c.addResistor (inNode, g9a, 68.0e3);                          // R47
        b.plateV9a = triodeStage (c, g9a, vccF, 100.0e3, 1.5e3, 10.0e-6, 200.0);   // R37, R46, C27 (C22 not fitted)

        // C31 1n0 -> R53 220K -> X (R60 220K, C42 470p to ground) -> Gain RV4-B (1MA) -> V9-B; bright C36 100p X -> grid.
        const auto c31 = c.addNode(), x = c.addNode(), g9b = c.addNode();
        c.addCapacitor (b.plateV9a, c31, 1.0e-9);
        c.addResistor (c31, x, 220.0e3);
        c.addResistor (x, gnd, 220.0e3);
        c.addCapacitor (x, gnd, 470.0e-12);
        b.rGainBTop = c.addResistor (x, g9b, 0.5e6);
        b.rGainBBot = c.addResistor (g9b, gnd, 0.5e6);
        c.addCapacitor (x, g9b, 100.0e-12);
        b.plateV9b = triodeStage (c, g9b, vccF, 100.0e3, 1.0e3, 10.0e-6, 250.0);  // R38, R48, C28
        c.addCapacitor (b.plateV9b, gnd, 100.0e-12);                  // C18 (plate to the AC-grounded rail)

        // C23 2n2 -> R54 220K -> Y (R61 470K) -> Gain RV4-A (1MA, second gang) -> V8-A.
        const auto c23 = c.addNode(), y = c.addNode(), g8a = c.addNode();
        c.addCapacitor (b.plateV9b, c23, 2.2e-9);
        c.addResistor (c23, y, 220.0e3);
        c.addResistor (y, gnd, 470.0e3);
        b.rGainATop = c.addResistor (y, g8a, 0.5e6);
        b.rGainABot = c.addResistor (g8a, gnd, 0.5e6);
        b.plateV8a = triodeStage (c, g8a, vccE, 100.0e3, 2.2e3, 10.0e-6, 250.0);  // R39, R49, C29
        c.addCapacitor (b.plateV8a, gnd, 100.0e-12);                  // C19

        // C32 4n7 -> R51 470K -> R52 220K -> V8-B, cathode R50 1K5 UNBYPASSED.
        const auto c32 = c.addNode(), g8b = c.addNode();
        c.addCapacitor (b.plateV8a, c32, 4.7e-9);
        c.addResistor (c32, g8b, 470.0e3);
        c.addResistor (g8b, gnd, 220.0e3);
        b.plateV8b = triodeStage (c, g8b, vccE, 100.0e3, 1.5e3, 0.0, 250.0);      // R40, R50

        // Dirty tone stack straight off V8-B's plate: C37 560p -> Treble RV7 250KB; R62 39K slope; C40 22n -> treble
        // bottom / bass top; Bass RV5 500KA (rheostat) -> Middle RV6 25KB to ground, C41 22n slope -> RV6's wiper.
        // Treble wiper -> Volume RV8 500KA -> relay.
        {
            const auto t = c.addNode(), w = c.addNode(), bt = c.addNode(), sl = c.addNode(), m = c.addNode(), mw = c.addNode();
            c.addCapacitor (b.plateV8b, t, 560.0e-12);
            b.rDTrebleTop = c.addResistor (t, w, 125.0e3);
            b.rDTrebleBot = c.addResistor (w, bt, 125.0e3);
            c.addResistor (b.plateV8b, sl, 39.0e3);
            c.addCapacitor (sl, bt, 22.0e-9);
            b.rDBass = c.addResistor (bt, m, 250.0e3);
            b.rDMidTop = c.addResistor (m, mw, 12.5e3);
            b.rDMidBot = c.addResistor (mw, gnd, 12.5e3);
            c.addCapacitor (sl, mw, 22.0e-9);
            b.dirtyOut = c.addNode();
            b.rDVolTop = c.addResistor (w, b.dirtyOut, 250.0e3);
            b.rDVolBot = c.addResistor (b.dirtyOut, gnd, 250.0e3);
            b.rLoadDirty = c.addResistor (b.dirtyOut, gnd, relayLoad);
        }

        // ---------------- Clean channel ----------------
        const auto g10a = c.addNode();
        c.addResistor (inNode, g10a, 68.0e3);                         // R44
        b.plateV10a = triodeStage (c, g10a, vccF, 100.0e3, 1.5e3, 22.0e-6, 200.0); // R35, R43, C25

        // C30 1n0 -> R56 220K -> W (R55 220K) -> Volume RV1 500KA -> V10-B; bright C34 150p W -> grid.
        const auto c30 = c.addNode(), wv = c.addNode(), g10b = c.addNode();
        c.addCapacitor (b.plateV10a, c30, 1.0e-9);
        c.addResistor (c30, wv, 220.0e3);
        c.addResistor (wv, gnd, 220.0e3);
        b.rCVolTop = c.addResistor (wv, g10b, 250.0e3);
        b.rCVolBot = c.addResistor (g10b, gnd, 250.0e3);
        c.addCapacitor (wv, g10b, 150.0e-12);
        b.plateV10b = triodeStage (c, g10b, vccF, 100.0e3, 1.5e3, 22.0e-6, 250.0); // R36, R45, C26

        // Clean tone stack: C35 56p -> Treble RV3 250KB; R58 100K slope; C38 22n -> treble bottom / Bass RV2 250KA top;
        // C39 22n slope -> RV2's wiper, which has R57 6K8 to ground (the fixed "middle"); RV2's far end to ground.
        {
            const auto t = c.addNode(), bt = c.addNode(), sl = c.addNode(), bw = c.addNode();
            c.addCapacitor (b.plateV10b, t, 56.0e-12);
            b.cleanOut = c.addNode();
            b.rCTrebleTop = c.addResistor (t, b.cleanOut, 125.0e3);
            b.rCTrebleBot = c.addResistor (b.cleanOut, bt, 125.0e3);
            c.addResistor (b.plateV10b, sl, 100.0e3);
            c.addCapacitor (sl, bt, 22.0e-9);
            c.addCapacitor (sl, bw, 22.0e-9);
            b.rCBassTop = c.addResistor (bt, bw, 125.0e3);
            b.rCBassBot = c.addResistor (bw, gnd, 125.0e3);
            c.addResistor (bw, gnd, 6.8e3);
            b.rLoadClean = c.addResistor (b.cleanOut, gnd, relayOpen);
        }
        return b;
    }
}

RockerverbStyleAmplifierProcessor::RockerverbStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        "rv_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Clean" : "Dirty");
        }));
    // The real panel, left to right. The Dirty knobs keep the original parameter ids (presets stay valid).
    auto cVolume = make ("rv_c_volume", "Clean Volume", 0.5f);
    auto cTreble = make ("rv_c_treble", "Clean Treble", 0.5f);
    auto cBass = make ("rv_c_bass", "Clean Bass", 0.5f);
    auto gain = make ("rv_gain", "Dirty Gain", 0.5f);
    auto treble = make ("rv_treble", "Dirty Treble", 0.5f);
    auto mid = make ("rv_mid", "Dirty Middle", 0.5f);
    auto bass = make ("rv_bass", "Dirty Bass", 0.5f);
    auto post = make ("rv_post", "Dirty Volume", 0.5f);
    auto output = make ("rv_output", "Output", 0.5f);
    auto power = make ("rv_power", "Power Drive", 1.0f);
    auto bias = make ("rv_bias", "Bias", 0.5f);
    auto feel = make ("rv_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "rv_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    channelParam = channel.get();
    cVolumeParam = cVolume.get();
    cTrebleParam = cTreble.get();
    cBassParam = cBass.get();
    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    postParam = post.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "rockerverb", "Rockerverb-Style Amplifier", "|", std::move (channel), std::move (cVolume));
    group->addChild (std::move (cTreble));
    group->addChild (std::move (cBass));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (post));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("rv_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
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

    // ================================================================ preamp: both channels + their tone stacks
    {
        auto b = buildPreamp (ch.pre, 370.0, 380.0);
        ch.pSrcF = b.srcF;
        ch.pSrcE = b.srcE;
        ch.pSrcIn = b.srcIn;
        ch.rGainBTop = b.rGainBTop;  ch.rGainBBot = b.rGainBBot;
        ch.rGainATop = b.rGainATop;  ch.rGainABot = b.rGainABot;
        ch.rDTrebleTop = b.rDTrebleTop;  ch.rDTrebleBot = b.rDTrebleBot;  ch.rDBass = b.rDBass;
        ch.rDMidTop = b.rDMidTop;  ch.rDMidBot = b.rDMidBot;  ch.rDVolTop = b.rDVolTop;  ch.rDVolBot = b.rDVolBot;
        ch.rCVolTop = b.rCVolTop;  ch.rCVolBot = b.rCVolBot;  ch.rCTrebleTop = b.rCTrebleTop;  ch.rCTrebleBot = b.rCTrebleBot;
        ch.rCBassTop = b.rCBassTop;  ch.rCBassBot = b.rCBassBot;
        ch.rLoadDirty = b.rLoadDirty;  ch.rLoadClean = b.rLoadClean;
        ch.pPlateV9a = b.plateV9a;  ch.pPlateV9b = b.plateV9b;  ch.pPlateV8a = b.plateV8a;  ch.pPlateV8b = b.plateV8b;
        ch.pPlateV10a = b.plateV10a;  ch.pPlateV10b = b.plateV10b;
        ch.pDirtyOut = b.dirtyOut;  ch.pCleanOut = b.cleanOut;
    }

    // ================================================================ post (always built): after relay RL1 the path is
    // shared -- R4 220K -> C4 220n -> V7-A (12AT7) cathode follower, grid biased from rail D by R17 220K / R14 33K (C11
    // makes that an AC ground) through R16 1M, cathode R15 22K -> C5 220n -> R6 150K / R13 68K -> loop (normalled) ->
    // C1 220n, R20 1M, R7 68K -> V7-B (12AT7), plate R18 56K, cathode R19 1K5 unbypassed -> C6 220n -> R9 1M -> the
    // reverb mixer (Reverb RV9 at minimum: R64 + the whole 250K pot to ground, R63 1M) -> C3 47n -> PI grid (1M).
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto src = c.addNode(), vd = c.addNode(), vb = c.addNode();
        ch.wSrcPost = c.addSource (src, 0.0);
        ch.wSrcD = c.addSource (vd, 360.0);
        ch.wSrcV7Bias = c.addSource (vb, 360.0 * 33.0 / 253.0);

        const auto r4 = c.addNode(), g7a = c.addNode();
        c.addResistor (src, r4, 220.0e3);                       // R4
        c.addCapacitor (r4, g7a, 220.0e-9);                     // C4
        c.addResistor (g7a, vb, 1.0e6);                         // R16
        ch.wPlateV7a = vd;
        ch.wCathodeV7a = c.addNode();
        c.addTriode (vd, g7a, ch.wCathodeV7a, triode12AT7());
        c.addResistor (ch.wCathodeV7a, gnd, 22.0e3);            // R15
        c.setInitialGuess (g7a, 47.0);
        c.setInitialGuess (ch.wCathodeV7a, 49.0);

        const auto c5 = c.addNode(), loop = c.addNode(), c1 = c.addNode(), g7b = c.addNode(), k7b = c.addNode();
        c.addCapacitor (ch.wCathodeV7a, c5, 220.0e-9);          // C5
        c.addResistor (c5, loop, 150.0e3);                      // R6
        c.addResistor (loop, gnd, 68.0e3);                      // R13
        c.addCapacitor (loop, c1, 220.0e-9);                    // C1
        c.addResistor (c1, gnd, 1.0e6);                         // R20
        c.addResistor (c1, g7b, 68.0e3);                        // R7
        ch.wPlateV7b = c.addNode();
        c.addTriode (ch.wPlateV7b, g7b, k7b, triode12AT7());
        c.addCapacitor (g7b, ch.wPlateV7b, cgp);
        c.addResistor (vd, ch.wPlateV7b, 56.0e3);               // R18
        c.addResistor (k7b, gnd, 1.5e3);                        // R19
        c.setInitialGuess (ch.wPlateV7b, 220.0);
        c.setInitialGuess (k7b, 2.5);

        const auto c6 = c.addNode();
        ch.wMix = c.addNode();
        c.addCapacitor (ch.wPlateV7b, c6, 220.0e-9);            // C6
        c.addResistor (c6, ch.wMix, 1.0e6);                     // R9
        c.addResistor (ch.wMix, gnd, 220.0e3 + 250.0e3);        // R64 + RV9 (reverb at minimum)
        c.addResistor (ch.wMix, gnd, 1.0e6);                    // R63
        ch.wTone = c.addNode();                                 // the PI's grid A
        c.addCapacitor (ch.wMix, ch.wTone, 47.0e-9);            // C3
        c.addResistor (ch.wTone, gnd, 1.0e6);                   // R26
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
        const auto g1 = ch.wTone, g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
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

        // NFB: from the speaker terminal through feedbackResistor to PI grid 2 (no Presence control on this amp).
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
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
    const auto split = [] (double total, double bottomFraction, double& top, double& bottom)
    {
        bottom = juce::jmax (1.0, total * bottomFraction);
        top = juce::jmax (1.0, total - bottom);
    };
    double gTop, gBot, dtTop, dtBot, dmTop, dmBot, dvTop, dvBot, cvTop, cvBot, ctTop, ctBot, cbTop, cbBot;
    split (1.0e6, pots::audio (k.gain), gTop, gBot);            // RV4 A1M, both gangs
    split (250.0e3, k.treble, dtTop, dtBot);                    // RV7 250KB
    split (25.0e3, k.mid, dmTop, dmBot);                        // RV6 25KB
    split (500.0e3, pots::audio (k.post), dvTop, dvBot);        // RV8 500KA
    split (500.0e3, pots::audio (k.cVolume), cvTop, cvBot);     // RV1 500KA
    split (250.0e3, k.cTreble, ctTop, ctBot);                   // RV3 250KB
    split (250.0e3, 1.0 - pots::audio (k.cBass), cbTop, cbBot); // RV2 250KA: more Bass = more resistance above the wiper
    const double dBass = juce::jmax (1.0, 500.0e3 * pots::audio (k.bass)); // RV5 500KA rheostat
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        auto& p = ch.pre;
        p.setResistance (ch.rGainBTop, gTop);  p.setResistance (ch.rGainBBot, gBot);
        p.setResistance (ch.rGainATop, gTop);  p.setResistance (ch.rGainABot, gBot);
        p.setResistance (ch.rDTrebleTop, dtTop);  p.setResistance (ch.rDTrebleBot, dtBot);
        p.setResistance (ch.rDBass, dBass);
        p.setResistance (ch.rDMidTop, dmTop);  p.setResistance (ch.rDMidBot, dmBot);
        p.setResistance (ch.rDVolTop, dvTop);  p.setResistance (ch.rDVolBot, dvBot);
        p.setResistance (ch.rCVolTop, cvTop);  p.setResistance (ch.rCVolBot, cvBot);
        p.setResistance (ch.rCTrebleTop, ctTop);  p.setResistance (ch.rCTrebleBot, ctBot);
        p.setResistance (ch.rCBassTop, cbTop);  p.setResistance (ch.rCBassBot, cbBot);
        // RL1: only the selected channel's output is connected to (and loaded by) the shared path.
        p.setResistance (ch.rLoadDirty, k.channel == 1 ? relayLoad : relayOpen);
        p.setResistance (ch.rLoadClean, k.channel == 0 ? relayLoad : relayOpen);
        if (! reducedOrder)
        {
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
    ch.pre.setSource (ch.pSrcF, rail (ch.sE, 460.0));    // F: V9 / V10
    ch.pre.setSource (ch.pSrcE, rail (ch.sD, 460.0));    // E: V8
    ch.power.setSource (ch.wSrcD, rail (ch.sC, 460.0));  // D: V7
    ch.power.setSource (ch.wSrcV7Bias, rail (ch.sC, 460.0) * 33.0 / 253.0);
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
        case Probe::v10aPlate: return ch.pre.voltage (ch.pPlateV10a);
        case Probe::v10bPlate: return ch.pre.voltage (ch.pPlateV10b);
        case Probe::v7aCathode: return ch.power.voltage (ch.wCathodeV7a);
        case Probe::v7bPlate: return ch.power.voltage (ch.wPlateV7b);
        case Probe::toneStackOut: return ch.pre.voltage (lastKnobs.channel == 0 ? ch.pCleanOut : ch.pDirtyOut);
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
    setup (smoothedCVolume, cVolumeParam, 0.02);
    setup (smoothedCTreble, cTrebleParam, 0.02);
    setup (smoothedCBass, cBassParam, 0.02);
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMid, midParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPost, postParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.12;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ cVolumeParam->get(), cTrebleParam->get(), cBassParam->get(), gainParam->get(), trebleParam->get(),
                  midParam->get(), bassParam->get(), postParam->get(), powerParam->get(), biasParam->get(),
                  tubeFeelParam->get(), juce::roundToInt (speakerParam->get()), juce::roundToInt (channelParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iCRun = 0.005, iDRun = 0.003, iERun = 0.006, ipRun = 0.10, isRun = 0.008;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcF, ch.supply.voltage (ch.sE));
            ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sD));
            passOk = ch.pre.prepare (newSampleRate) && passOk;

            ch.power.setSource (ch.wSrcPost, 0.0);
            ch.power.setSource (ch.wSrcD, ch.supply.voltage (ch.sC));
            ch.power.setSource (ch.wSrcV7Bias, ch.supply.voltage (ch.sC) * 33.0 / 253.0);
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
            const double vF = ch.supply.voltage (ch.sE), vE = ch.supply.voltage (ch.sD), vD = ch.supply.voltage (ch.sC);
            const double iF = (vF - ch.pre.voltage (ch.pPlateV9a)) / 100.0e3 + (vF - ch.pre.voltage (ch.pPlateV9b)) / 100.0e3
                            + (vF - ch.pre.voltage (ch.pPlateV10a)) / 100.0e3 + (vF - ch.pre.voltage (ch.pPlateV10b)) / 100.0e3;
            const double iE = (vE - ch.pre.voltage (ch.pPlateV8a)) / 100.0e3 + (vE - ch.pre.voltage (ch.pPlateV8b)) / 100.0e3;
            const double iD = ch.power.voltage (ch.wCathodeV7a) / 22.0e3 + (vD - ch.power.voltage (ch.wPlateV7b)) / 56.0e3
                            + vD / 253.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iCRun += 0.5 * (iD - iCRun);
            iDRun += 0.5 * (iE - iDRun);
            iERun += 0.5 * (iF - iERun);
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
        ch.pre.setSource (ch.pSrcF, ch.supply.voltage (ch.sE));
        ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sD));
        ch.power.setSource (ch.wSrcD, ch.supply.voltage (ch.sC));
        ch.power.setSource (ch.wSrcV7Bias, ch.supply.voltage (ch.sC) * 33.0 / 253.0);
        ch.pre.solveSample();
        ch.power.solveSample();
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

    smoothedCVolume.setTargetValue (cVolumeParam->get());
    smoothedCTreble.setTargetValue (cTrebleParam->get());
    smoothedCBass.setTargetValue (cBassParam->get());
    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMid.setTargetValue (midParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPost.setTargetValue (postParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int channelSel = juce::roundToInt (channelParam->get()) >= 1 ? 1 : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        Knobs knobs { smoothedCVolume.getNextValue(), smoothedCTreble.getNextValue(), smoothedCBass.getNextValue(),
                      smoothedGain.getNextValue(), smoothedTreble.getNextValue(), smoothedMid.getNextValue(),
                      smoothedBass.getNextValue(), smoothedPost.getNextValue(), smoothedPower.getNextValue(),
                      smoothedBias.getNextValue(), smoothedFeel.getNextValue(), speakerChoice, channelSel };
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (knobs);
        }

        const double masterGain = juce::jmax (0.002, pots::audio (knobs.powerDrive));

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

            // Both tone stacks block DC (their only DC paths end on caps), so the relay's output is pure AC.
            const double selected = ch.pre.voltage (channelSel == 0 ? ch.pCleanOut : ch.pDirtyOut);
            ch.power.setSource (ch.wSrcPost, masterGain * selected);
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
