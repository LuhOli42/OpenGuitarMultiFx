#include "DumbleODSStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/DumbleODS.md): the 70s trace prints A 460 V (OT centre tap), B 454 V (screens,
    // after the 80-ohm choke), C 436 V (PI), D 306 V (Overdrive stages), E 303 V (V1). Rectifier -> 2 x 100uF/350 in
    // series with 2 x 220K -> standby -> A -> choke -> B (20uF) -> 2.2K -> C (20uF) -> 22K -> D (20uF) -> 1K -> E (20uF).
    constexpr double railPlatesNominal = 460.0;
    constexpr double rectifierResistance = 40.0;
    constexpr double chokeResistance = 80.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 440.0e3;
    constexpr double screenResistor = 500.0;

    // ---- reduced-order power stage sag table (the Steel String's 6L6GC table, scaled 470 V -> 460 V) ----
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 460.0, 460.0, 460.0, 460.0, 459.99, 459.98, 459.96, 459.91,
                                                 459.83, 459.59, 459.23, 458.44, 457.32, 455.29, 451.51, 445.15,
                                                 437.34, 421.72, 407.24, 403.45 };

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

    KorenTriode::Parameters triode12AX7() { return {}; }
    KorenPentode::Parameters pentode6L6GC() { return {}; }

    constexpr double cgp = 1.7e-12;

    // Output transformer: not on the trace. Two 6L6GC at ~460 V want ~4K plate to plate; taken with an 8 ohm
    // secondary, the same iron as the Bassman model.
    constexpr double primaryHalfInductance = 6.25;
    constexpr double halfToSecondaryTurns = 11.18;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.9992;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.24;

    constexpr double feedbackResistor = 3.9e3;  // 3.9K-1W from the speaker into the bottom of the PI's tail
    constexpr double biasSupplyVolts = -66.0;   // the F rail: not printed, set for ~35 mA per tube at noon
    constexpr double biasPot = 25.0e3;          // the bias pot over the 27K: its value is not printed

    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1;
    constexpr double powerTheta = 0.9;

    constexpr double openLink = 1.0e9, closedLink = 1.0;

    struct PreampBuild
    {
        NodalCircuit& c;
        int srcE = 0, srcD = 0, srcIn = 0;
        int rVolTop = 0, rVolBot = 0, rTrebleTop = 0, rTrebleBottom = 0, rBassTop = 0, rBassBottom = 0, rMid = 0;
        int capBright = 0, rMidBoost = 0, rRock = 0, rJazzCap = 0, rJazzMerge = 0, rDeepA = 0, rDeepB = 0;
        int rNormal = 0, rOdIn = 0, rOdOut = 0;
        int rTrimTop = 0, rTrimBot = 0, rOdTop = 0, rOdBot = 0, rRatioTop = 0, rRatioBot = 0, rMasterTop = 0, rMasterBot = 0;
        NodalCircuit::Node plateV1a = 0, plateV1b = 0, plateV2a = 0, plateV2b = 0, relay = 0, masterWiper = 0, piGrid = 0;
    };

    /** A gain stage: plate resistor to `rail`, cathode resistor bypassed by a cap, the Miller cap, and (when given) a
        plate-to-cathode cap -- the 500pF snubbers on the Overdrive stages. */
    NodalCircuit::Node gainStage (NodalCircuit& c, NodalCircuit::Node grid, NodalCircuit::Node rail, double plateGuess,
                                  double plateCathodeCap = 0.0)
    {
        const auto gnd = NodalCircuit::ground;
        const auto plate = c.addNode(), k = c.addNode();
        c.addTriode (plate, grid, k, triode12AX7());
        c.addCapacitor (grid, plate, cgp);
        c.addResistor (rail, plate, 100.0e3);
        c.addResistor (k, gnd, 1.5e3);
        c.addCapacitor (k, gnd, 5.0e-6);
        if (plateCathodeCap > 0.0)
            c.addCapacitor (plate, k, plateCathodeCap);
        c.setInitialGuess (plate, plateGuess);
        c.setInitialGuess (k, 1.6);
        return plate;
    }

    /** The whole preamp, one block (the 70s trace's preamp and "Distorto" sheets):
        input (1M, 22K) -> V1A -> tone stack -> Volume 1M-A (Bright 500pF) -> 220K -> V1B with 22M + .05uF from its
        plate back to its grid -> .01uF -> the Normal / Overdrive relay. Overdrive: the input trim (100K-L) -> 68K ->
        V2A -> .02uF + 100K -> Overdrive (100K-L) -> 180K -> V2B -> .01uF -> Ratio (100K-L), back to the relay. After the
        relay: 10K -> Master 1M-A, and a copy of the PI's input (.01uF into 1M) as its load. */
    PreampBuild buildPreamp (NodalCircuit& c, double eGuess, double dGuess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };
        const auto vccE = c.addNode(), vccD = c.addNode(), in = c.addNode();
        b.srcE = c.addSource (vccE, eGuess);
        b.srcD = c.addSource (vccD, dGuess);
        b.srcIn = c.addSource (in, 0.0);

        const auto g1 = c.addNode();
        c.addResistor (in, g1, 22.0e3);
        c.addResistor (in, gnd, 1.0e6);
        b.plateV1a = gainStage (c, g1, vccE, 200.0);

        // Tone stack. Treble: 2.2nF in series with 390pF (331pF; Mid Boost shorts the 390pF) into the 250K-A
        // Treble pot. Slope 100K -> .1uF into the 250K-A Bass pot (bottom on 1.8K) and .05uF into the 100K Middle
        // rheostat. JAZZ / PAB / ROCK: Rock ties the treble pot's bottom to the bass wiper; Jazz puts it on .005uF to
        // ground and merges the bass wiper into the treble wiper through 100K; PAB lifts the bass wiper through 2 x 22M.
        const auto nMb = c.addNode(), nTt = c.addNode(), nW = c.addNode(), nTb = c.addNode(), nS = c.addNode(),
                   nBt = c.addNode(), nBw = c.addNode(), nBb = c.addNode(), nM = c.addNode(), nJc = c.addNode(),
                   nJm = c.addNode();
        c.addCapacitor (b.plateV1a, nMb, 2.2e-9);
        c.addCapacitor (nMb, nTt, 390.0e-12);
        b.rMidBoost = c.addResistor (nMb, nTt, openLink);
        b.rTrebleTop = c.addResistor (nTt, nW, 125.0e3);
        b.rTrebleBottom = c.addResistor (nW, nTb, 125.0e3);
        c.addResistor (b.plateV1a, nS, 100.0e3);
        c.addCapacitor (nS, nBt, 0.1e-6);
        c.addCapacitor (nS, nM, 0.05e-6);
        b.rMid = c.addResistor (nM, gnd, 50.0e3);
        b.rBassTop = c.addResistor (nBt, nBw, 125.0e3);
        b.rBassBottom = c.addResistor (nBw, nBb, 125.0e3);
        c.addResistor (nBb, gnd, 1.8e3);
        b.rRock = c.addResistor (nTb, nBw, closedLink);
        b.rJazzCap = c.addResistor (nTb, nJc, openLink);
        c.addCapacitor (nJc, gnd, 0.005e-6);
        b.rJazzMerge = c.addResistor (nBw, nJm, openLink);
        c.addResistor (nJm, nW, 100.0e3);

        // Volume 1M-A off the treble wiper; Bright puts 500pF from its top to its wiper.
        const auto nV = c.addNode();
        b.rVolTop = c.addResistor (nW, nV, 500.0e3);
        b.rVolBot = c.addResistor (nV, gnd, 500.0e3);
        b.capBright = c.addCapacitor (nW, nV, 1.0e-15);

        // DEEP: bass wiper -> 270K -> 1M -> J (10K to ground) <- .01uF <- 270K <- the Volume's wiper.
        const auto nDa = c.addNode(), nDb = c.addNode(), nDa2 = c.addNode(), nDb2 = c.addNode(), nJ = c.addNode();
        c.addResistor (nBw, nDa, 270.0e3);
        b.rDeepA = c.addResistor (nDa, nDa2, openLink);
        c.addResistor (nDa2, nJ, 1.0e6);
        c.addResistor (nJ, gnd, 10.0e3);
        c.addCapacitor (nJ, nDb2, 0.01e-6);
        b.rDeepB = c.addResistor (nDb2, nDb, openLink);
        c.addResistor (nDb, nV, 270.0e3);

        // V1B: 220K series; 22M from the grid to a .05uF off its own plate (shunt feedback above ~150 Hz).
        const auto g2 = c.addNode(), nFb = c.addNode();
        c.addResistor (nV, g2, 220.0e3);
        b.plateV1b = gainStage (c, g2, vccE, 200.0);
        c.addCapacitor (b.plateV1b, nFb, 0.05e-6);
        c.addResistor (nFb, g2, 22.0e6);

        // Relay: Normal passes V1B's .01uF straight on; Overdrive sends it through the two stages.
        const auto nO = c.addNode(), nA = c.addNode(), nB = c.addNode();
        b.relay = c.addNode();
        c.addCapacitor (b.plateV1b, nO, 0.01e-6);
        b.rNormal = c.addResistor (nO, b.relay, closedLink);
        b.rOdIn = c.addResistor (nO, nA, openLink);
        b.rOdOut = c.addResistor (nB, b.relay, openLink);
        c.addResistor (nO, gnd, 10.0e6); // keeps the cap's far side defined while the relay is open

        const auto nTw = c.addNode(), g3 = c.addNode();
        b.rTrimTop = c.addResistor (nA, nTw, 50.0e3);
        b.rTrimBot = c.addResistor (nTw, gnd, 50.0e3);
        c.addResistor (nTw, g3, 68.0e3);
        b.plateV2a = gainStage (c, g3, vccD, 200.0, 500.0e-12);

        const auto nC = c.addNode(), nOt = c.addNode(), nOw = c.addNode(), g4 = c.addNode();
        c.addCapacitor (b.plateV2a, nC, 0.02e-6);
        c.addResistor (nC, nOt, 100.0e3);
        b.rOdTop = c.addResistor (nOt, nOw, 50.0e3);
        b.rOdBot = c.addResistor (nOw, gnd, 50.0e3);
        c.addResistor (nOw, g4, 180.0e3);
        b.plateV2b = gainStage (c, g4, vccD, 200.0, 500.0e-12);

        const auto nRt = c.addNode();
        c.addCapacitor (b.plateV2b, nRt, 0.01e-6);
        b.rRatioTop = c.addResistor (nRt, nB, 50.0e3);
        b.rRatioBot = c.addResistor (nB, gnd, 50.0e3);

        // Master: 10K -> 1M-A; the PI's .01uF into its 1M as the load.
        const auto nMt = c.addNode(), nMw = c.addNode();
        b.piGrid = c.addNode();
        c.addResistor (b.relay, nMt, 10.0e3);
        b.rMasterTop = c.addResistor (nMt, nMw, 500.0e3);
        b.rMasterBot = c.addResistor (nMw, gnd, 500.0e3);
        b.masterWiper = nMw;
        c.addCapacitor (nMw, b.piGrid, 0.01e-6);
        c.addResistor (b.piGrid, gnd, 1.0e6);
        return b;
    }

    // Alternates this amp's page-2 Cab selector offers -- the option ORDER must match between makeCabParam() (names)
    // and the tails' setCabChoices() (profiles).
    const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::v30_412, &tubeamp::cabs::greenback412, &tubeamp::cabs::jensen212 };
}

DumbleODSStyleAmplifierProcessor::DumbleODSStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto toggle = [] (const char* id, const char* name, const char* off, const char* on)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [off, on] (float v, int) { return juce::String (v > 0.5f ? on : off); }));
    };

    auto channel = toggle ("ods_channel", "Channel", "Normal", "Overdrive");
    auto volume = make ("ods_volume", "Volume", 0.5f);
    auto bright = toggle ("ods_bright", "Bright", "Off", "On");
    auto deep = toggle ("ods_deep", "Deep", "Off", "On");
    auto midBoost = toggle ("ods_mid_boost", "Mid Boost", "Off", "On");
    auto voicing = std::make_unique<juce::AudioParameterFloat> (
        "ods_voicing", "Jazz / PAB / Rock", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 2.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("Jazz"); case 1: return juce::String ("PAB"); default: return juce::String ("Rock"); }
        }));
    auto treble = make ("ods_treble", "Treble", 0.5f);
    auto mid = make ("ods_mid", "Middle", 0.5f);
    auto bass = make ("ods_bass", "Bass", 0.5f);
    auto overdrive = make ("ods_overdrive", "Overdrive", 0.5f);
    // Ratio is the Overdrive channel's level. At 0.08 the two channels sit ~10 dB apart at noon with a 0.2 V note,
    // where a 0.5 would put Overdrive 23 dB over Normal (the stages saturate long before the pot).
    auto ratio = make ("ods_ratio", "Ratio", 0.08f);
    auto master = make ("ods_master", "Master", 0.5f);
    auto accent = toggle ("ods_accent", "Accent", "Off", "On");

    auto odTrim = make ("ods_od_trim", "OD Trim", 0.5f);
    auto power = make ("ods_power", "Power Drive", 1.0f);
    auto bias = make ("ods_bias", "Bias", 0.5f);
    auto feel = make ("ods_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "ods_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));
    auto output = make ("ods_output", "Output", 0.5f);
    auto cab = tubeamp::makeCabParam ("ods_cab", "1x12 EV", cabAlts);

    smoothedParams = { volume.get(), treble.get(), mid.get(), bass.get(), overdrive.get(), ratio.get(), master.get(),
                       odTrim.get(), power.get(), bias.get(), feel.get(), output.get() };
    channelParam = channel.get();
    brightParam = bright.get();
    deepParam = deep.get();
    midBoostParam = midBoost.get();
    voicingParam = voicing.get();
    accentParam = accent.get();
    speakerParam = speaker.get();
    cabParam = cab.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("dumble_ods", "Dumble ODS", "|", std::move (channel));
    group->addChild (std::move (volume));
    group->addChild (std::move (bright));
    group->addChild (std::move (deep));
    group->addChild (std::move (midBoost));
    group->addChild (std::move (voicing));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (overdrive));
    group->addChild (std::move (ratio));
    group->addChild (std::move (master));
    group->addChild (std::move (accent));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ods_page2", "Page 2", "|", std::move (odTrim));
    page2->addChild (std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void DumbleODSStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode(), nl = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();
        ch.sD = c.addNode();
        ch.sE = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 50.0e-6); // 2 x 100uF in series
        c.addResistor (ch.sA, gnd, bleeder);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 20.0e-6);
        c.addResistor (ch.sB, ch.sC, 2.2e3);
        c.addCapacitor (ch.sC, gnd, 20.0e-6);
        c.addResistor (ch.sC, ch.sD, 22.0e3);
        c.addCapacitor (ch.sD, gnd, 20.0e-6);
        c.addResistor (ch.sD, ch.sE, 1.0e3);
        c.addCapacitor (ch.sE, gnd, 20.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -0.07);
        ch.iB = c.addCurrentSource (ch.sB, -0.006);
        ch.iC = c.addCurrentSource (ch.sC, -0.0026);
        ch.iD = c.addCurrentSource (ch.sD, -0.003);
        ch.iE = c.addCurrentSource (ch.sE, -0.003);
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 436.0);
        c.setInitialGuess (ch.sD, 306.0);
        c.setInitialGuess (ch.sE, 303.0);
    }

    // ================================================================ preamp
    {
        auto b = buildPreamp (ch.pre, 303.0, 306.0);
        ch.pSrcE = b.srcE;
        ch.pSrcD = b.srcD;
        ch.pSrcIn = b.srcIn;
        ch.rVolTop = b.rVolTop;
        ch.rVolBot = b.rVolBot;
        ch.rTrebleTop = b.rTrebleTop;
        ch.rTrebleBottom = b.rTrebleBottom;
        ch.rBassTop = b.rBassTop;
        ch.rBassBottom = b.rBassBottom;
        ch.rMid = b.rMid;
        ch.capBright = b.capBright;
        ch.rMidBoost = b.rMidBoost;
        ch.rRock = b.rRock;
        ch.rJazzCap = b.rJazzCap;
        ch.rJazzMerge = b.rJazzMerge;
        ch.rDeepA = b.rDeepA;
        ch.rDeepB = b.rDeepB;
        ch.rNormal = b.rNormal;
        ch.rOdIn = b.rOdIn;
        ch.rOdOut = b.rOdOut;
        ch.rTrimTop = b.rTrimTop;
        ch.rTrimBot = b.rTrimBot;
        ch.rOdTop = b.rOdTop;
        ch.rOdBot = b.rOdBot;
        ch.rRatioTop = b.rRatioTop;
        ch.rRatioBot = b.rRatioBot;
        ch.rMasterTop = b.rMasterTop;
        ch.rMasterBot = b.rMasterBot;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV1b = b.plateV1b;
        ch.pPlateV2a = b.plateV2a;
        ch.pPlateV2b = b.plateV2b;
        ch.pRelay = b.relay;
        ch.pMasterWiper = b.masterWiper;
        ch.pPiGrid = b.piGrid;
    }

    if (reducedOrder)
        return;

    // ================================================================ phase inverter, power amp (full reference only)
    // The 70s trace's output sheet. PI: 12AX7 long-tailed pair, 100K plates to C, cathodes -> 820 -> X; X -> 24K -> N;
    // N -> 390 to ground (Accent puts 2.2uF across it) and 3.9K from the speaker. Both grids return through 1M to X; the
    // second grid is bypassed to N by .1uF. The preamp block carries a copy of the PI's .01uF and 1M as the Master's
    // load; here the Master wiper's voltage drives the real .01uF, so the 1M alone sets the grid's DC.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto src = c.addNode(), vpi = c.addNode(), ct = c.addNode(), vf = c.addNode();
        ch.wSrcIn = c.addSource (src, 0.0);
        ch.wSrcPi = c.addSource (vpi, 436.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vf, biasSupplyVolts);

        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(),
                   x = c.addNode(), n = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = x;
        ch.wFeedback = n;
        c.addCapacitor (src, g1, 0.01e-6);
        c.addResistor (g1, x, 1.0e6);
        c.addResistor (g2, x, 1.0e6);
        c.addCapacitor (g2, n, 0.1e-6);
        c.addTriode (pa, g1, k, triode12AX7());
        c.addTriode (pb, g2, k, triode12AX7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 100.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addResistor (k, x, 820.0);
        c.addResistor (x, n, 24.0e3);
        c.addResistor (n, gnd, 390.0);
        ch.capAccent = c.addCapacitor (n, gnd, 1.0e-12);
        c.setInitialGuess (pa, 305.0);
        c.setInitialGuess (pb, 305.0);
        c.setInitialGuess (k, 62.0);
        c.setInitialGuess (x, 60.0);
        c.setInitialGuess (g1, 60.0);
        c.setInitialGuess (g2, 60.0);
        c.setInitialGuess (n, 1.0);

        // 2 x 6L6GC: .1uF couplings, 200K leaks to the bias wiper, 3.3K stoppers, 500-ohm screen resistors (their drop
        // is applied to the screen supply each sample). Bias: the F rail across the bias pot over 27K.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.1e-6);
        c.addCapacitor (pb, g4, 0.1e-6);
        c.addResistor (g3, nb, 200.0e3);
        c.addResistor (g4, nb, 200.0e3);
        c.addResistor (g3, g3s, 3.3e3);
        c.addResistor (g4, g4s, 3.3e3);
        c.addResistor (vf, nb, 0.5 * biasPot);
        ch.rBiasTrim = c.addResistor (nb, gnd, 0.5 * biasPot + 27.0e3);
        c.addCapacitor (nb, gnd, 1.0e-6);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6L6GC(), 454.0);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6L6GC(), 454.0);
        c.addCapacitor (pp1, pp2, 400.0e-12);
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
        ch.rFeedback = c.addResistor (ch.wOut, n, feedbackResistor);

        for (auto node : { pp1, pp2, a1, a2 })
            c.setInitialGuess (node, railPlatesNominal);
        for (auto node : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (node, -47.0);
    }
}

DumbleODSStyleAmplifierProcessor::Knobs DumbleODSStyleAmplifierProcessor::knobsFromSmoothed (const float* v, int speaker) const
{
    Knobs k;
    k.volume = v[sVolume];
    k.treble = v[sTreble];
    k.mid = v[sMid];
    k.bass = v[sBass];
    k.overdrive = v[sOverdrive];
    k.ratio = v[sRatio];
    k.master = v[sMaster];
    k.odTrim = v[sOdTrim];
    k.powerDrive = v[sPower];
    k.bias = v[sBias];
    k.tubeFeel = v[sFeel];
    k.speaker = speaker;
    k.overdriveOn = channelParam->get() > 0.5f;
    k.bright = brightParam->get() > 0.5f;
    k.deep = deepParam->get() > 0.5f;
    k.midBoost = midBoostParam->get() > 0.5f;
    k.accent = accentParam->get() > 0.5f;
    k.voicing = juce::roundToInt (voicingParam->get());
    return k;
}

DumbleODSStyleAmplifierProcessor::Knobs DumbleODSStyleAmplifierProcessor::knobsFromParams() const
{
    float v[numSmoothed];
    for (int i = 0; i < numSmoothed; ++i)
        v[i] = smoothedParams[(size_t) i]->get();
    return knobsFromSmoothed (v, juce::roundToInt (speakerParam->get()));
}

void DumbleODSStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const auto split = [] (NodalCircuit& c, int top, int bottom, double total, double bottomFraction)
    {
        const double rb = juce::jlimit (1.0, total - 1.0, total * bottomFraction);
        c.setResistance (bottom, rb);
        c.setResistance (top, total - rb);
    };
    const bool jazz = k.voicing == 0, pab = k.voicing == 1;
    const double rockR = jazz ? openLink : (pab ? 44.0e6 : closedLink);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        auto& c = ch.pre;
        split (c, ch.rVolTop, ch.rVolBot, 1.0e6, pots::audio (k.volume));
        split (c, ch.rTrebleTop, ch.rTrebleBottom, 250.0e3, pots::audio (k.treble));
        split (c, ch.rBassTop, ch.rBassBottom, 250.0e3, pots::audio (k.bass));
        c.setResistance (ch.rMid, juce::jmax (1.0, 100.0e3 * k.mid));
        split (c, ch.rTrimTop, ch.rTrimBot, 100.0e3, k.odTrim);
        split (c, ch.rOdTop, ch.rOdBot, 100.0e3, k.overdrive);
        split (c, ch.rRatioTop, ch.rRatioBot, 100.0e3, k.ratio);
        split (c, ch.rMasterTop, ch.rMasterBot, 1.0e6, pots::audio (k.master));
        c.setCapacitance (ch.capBright, k.bright ? 500.0e-12 : 1.0e-15);
        c.setResistance (ch.rMidBoost, k.midBoost ? closedLink : openLink);
        c.setResistance (ch.rRock, rockR);
        c.setResistance (ch.rJazzCap, jazz ? closedLink : openLink);
        c.setResistance (ch.rJazzMerge, jazz ? closedLink : openLink);
        c.setResistance (ch.rDeepA, k.deep ? closedLink : openLink);
        c.setResistance (ch.rDeepB, k.deep ? closedLink : openLink);
        c.setResistance (ch.rNormal, k.overdriveOn ? openLink : closedLink);
        c.setResistance (ch.rOdIn, k.overdriveOn ? closedLink : openLink);
        c.setResistance (ch.rOdOut, k.overdriveOn ? closedLink : openLink);
        if (! reducedOrder)
        {
            ch.power.setCapacitance (ch.capAccent, k.accent ? 2.2e-6 : 1.0e-12);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setResistance (ch.rBiasTrim, (1.0 - k.bias) * biasPot + 27.0e3);
        }
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void DumbleODSStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (reducedOrder)
        return;
    const double nominal = speakerNominal[juce::jlimit (0, 2, index)];
    const auto sm = speakerModel (nominal);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setResistance (ch.rSpkEddy, speakerEddyLoss * nominal / speakerNominal[matchedSpeaker]);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void DumbleODSStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    if (reducedOrder)
        return;
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setResistance (ch.rSpkEddy, 1.0e9);
        ch.power.setInductorInverse (ch.grpSpkLe, 1.0e6);
        ch.power.setInductorInverse (ch.grpSpkLp, 1.0);
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2;
    resistiveLoadForced = true;
}

void DumbleODSStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

void DumbleODSStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    if (! reducedOrder)
        ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void DumbleODSStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! reducedOrder)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.8, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.15, ch.sumScreen / n));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 530.0));
    }
    ch.pre.setSource (ch.pSrcD, rail (ch.sD, 400.0));
    ch.pre.setSource (ch.pSrcE, rail (ch.sE, 400.0));
    ch.vScreen = rail (ch.sB, 560.0);
}

double DumbleODSStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double piGrid) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (piGrid);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    const double feel = juce::jlimit (0.0, 1.0, lastKnobs.tubeFeel);
    const double biasTrim = juce::jlimit (0.0, 1.0, lastKnobs.bias);
    ch.bmRail = bmSagRail[0] - feel * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

    const double asym = 0.3 * (biasTrim - 0.5) * bmYmax;
    const double drive = piGrid + asym;
    const double k = ch.bmRail * bmYmax / bmGain0;
    const auto knee = [&] (double x) { return bmYmax * x / std::pow (1.0 + std::pow (x, bmKneeN), 1.0 / bmKneeN); };
    const auto f = [&] (double x) { const double ux = std::abs (x) / juce::jmax (1.0e-9, k);
                                    return std::copysign (knee (ux), x); };
    const double raw = (f (drive) - f (asym)) * ch.bmRail;

    // Accent bypasses the 390-ohm NFB shunt: less feedback above ~185 Hz, so more top. Folded in as a shelf lift.
    const double hf = bmShelfHfGain * (lastKnobs.accent ? 1.6 : 1.0);
    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmTail.process (bmLevelTrim * (ch.bmToneState + hf * (raw - ch.bmToneState)));
    return ch.bmOutput;
}

double DumbleODSStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    const auto power = [&] (NodalCircuit::Node n) { return reducedOrder ? 0.0 : ch.power.voltage (n); };
    switch (p)
    {
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
        case Probe::v1bPlate: return ch.pre.voltage (ch.pPlateV1b);
        case Probe::v2aPlate: return ch.pre.voltage (ch.pPlateV2a);
        case Probe::v2bPlate: return ch.pre.voltage (ch.pPlateV2b);
        case Probe::preampOut: return ch.pre.voltage (ch.pPiGrid);
        case Probe::phaseInverterGrid: return power (ch.wGridA);
        case Probe::phaseInverterPlateA: return power (ch.wPlateA);
        case Probe::phaseInverterPlateB: return power (ch.wPlateB);
        case Probe::phaseInverterTail: return power (ch.wTail);
        case Probe::powerPlateA: return power (ch.wPP1);
        case Probe::powerPlateB: return power (ch.wPP2);
        case Probe::powerGridA: return power (ch.wPowerGridA);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return power (ch.wBias);
        case Probe::feedbackNode: return power (ch.wFeedback);
    }
    return 0.0;
}

double DumbleODSStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

void DumbleODSStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = voicings::fenderClean;
        bmVoicing.asym = 0.0;
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
    }

    for (size_t i = 0; i < smoothed.size(); ++i)
    {
        smoothed[i].reset (newSampleRate, i == sBias || i == sFeel ? 0.05 : 0.02);
        smoothed[i].setCurrentAndTargetValue (smoothedParams[i]->get());
    }

    idleSupplyCurrent = 0.085;
    appliedSpeaker = matchedSpeaker;
    updatePots (knobsFromParams());

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * lastKnobs.tubeFeel;
        bool passOk = true;
        double ipRun = 0.07, isRun = 0.006, iPiRun = 0.0026, iDRun = 0.003, iERun = 0.003;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);
            ch.pre.setSource (ch.pSrcD, ch.supply.voltage (ch.sD));
            ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.vScreen = ch.supply.voltage (ch.sB);

            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0, iPi = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, 0.0);
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                const double vPi = ch.supply.voltage (ch.sC);
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 100.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
                ch.screenDropA = screenResistor * isA;
                ch.screenDropB = screenResistor * isB;
            }
            const double vD = ch.supply.voltage (ch.sD), vE = ch.supply.voltage (ch.sE);
            const double iD = (vD - ch.pre.voltage (ch.pPlateV2a)) / 100.0e3 + (vD - ch.pre.voltage (ch.pPlateV2b)) / 100.0e3;
            const double iE = (vE - ch.pre.voltage (ch.pPlateV1a)) / 100.0e3 + (vE - ch.pre.voltage (ch.pPlateV1b)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iDRun += 0.5 * (iD - iDRun);
            iERun += 0.5 * (iE - iERun);
            if (! reducedOrder)
            {
                ch.supply.setCurrentSource (ch.iA, -ipRun);
                ch.supply.setCurrentSource (ch.iB, -isRun);
            }
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iDRun);
            ch.supply.setCurrentSource (ch.iE, -iERun);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iDRun + iERun + railPlatesNominal / bleeder;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        ch.pre.setSource (ch.pSrcD, ch.supply.voltage (ch.sD));
        ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
        ch.pre.saveDynamicState (ch.preRest);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            ch.power.saveDynamicState (ch.powerRest);
        }
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = ch.bmOutput = ch.bmToneState = 0.0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void DumbleODSStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);
    for (size_t p = 0; p < smoothed.size(); ++p)
        smoothed[p].setTargetValue (smoothedParams[p]->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        float v[numSmoothed];
        for (int p = 0; p < numSmoothed; ++p)
            v[p] = smoothed[(size_t) p].getNextValue();
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (knobsFromSmoothed (v, speakerChoice));
        }
        const double driveGain = juce::jmax (0.002, pots::audio ((double) v[sPower]));
        const double ou = v[sOutput];
        const double outGain = std::pow (10.0, (ou < 0.5 ? (ou - 0.5) * 60.0 : (ou - 0.5) * 24.0) / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            bool ok = ch.pre.solveSample();
            const double piDrive = driveGain * ch.pre.voltage (reducedOrder ? ch.pPiGrid : ch.pMasterWiper);

            double speakerVolts = 0.0;
            if (reducedOrder)
            {
                speakerVolts = behavioralPowerStage (ch, piDrive);
            }
            else
            {
                ch.power.setSource (ch.wSrcIn, piDrive);
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
                ok = ch.power.solveSample() && ok;
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.screenDropA += 0.3 * (screenResistor * isA - ch.screenDropA);
                ch.screenDropB += 0.3 * (screenResistor * isB - ch.screenDropB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
                speakerVolts = ch.power.voltage (ch.wOut);
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < 100.0;
            ok = ok && sane;
            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre.saveDynamicState (ch.preRest);
                    if (! reducedOrder)
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
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void DumbleODSStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
