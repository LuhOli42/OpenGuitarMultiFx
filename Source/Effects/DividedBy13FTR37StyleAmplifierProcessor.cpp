#include "DividedBy13FTR37StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/DividedBy13FTR37.md). No factory schematic is published; the manufacturer quotes the
    // tube complement (5879, 2x12AX7, 12AT7, 2x12AX7, 4x6V6GT), the GZ34 rectifier and a 25 mA-per-tube idle, so the
    // rails below are ESTIMATED for a ~37 W fixed-bias 6V6 quartet and the idle currents chosen to land there. ----
    constexpr double railPlatesNominal = 410.0;   // OT centre tap (estimated)
    constexpr double rectifierResistance = 140.0; // GZ34 + PT (estimated)
    constexpr double screenResistor = 1.0e3;      // plate node -> screen node dropper
    constexpr double idlePlateCurrent = 0.025;    // per output tube -- the manual's own 25 mA bias point
    constexpr double idleScreenCurrent = 0.003;
    constexpr double piAndPreampCurrent = 0.006;  // PI + preamp halves, drawn from the screen node (estimated)
    constexpr double piFraction = 0.74;           // PI rail / screen node
    constexpr double preFraction = 0.68;          // preamp rail / screen node
    constexpr double biasNominal = -32.0;         // fixed-bias grid volts at noon (operating-point estimate for ~25 mA)
    constexpr double biasSpan = 16.0;             // +/- volts over the Bias knob
    double idleSupplyCurrent = 0.0;

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set

    /** 5879 sharp-cutoff pentode (the FTR 37's V1 on the Click channel). No published Koren/SPICE fit for the 5879
        was found; the 5879 is the same small-signal pentode family as the EF86 (same miniature sharp-cutoff
        construction, near-identical datasheet curves), so the AC15's published EF86 fit stands in here -- an
        approximation, documented in docs/circuits/DividedBy13FTR37.md. */
    KorenPentode::Parameters pentode5879()
    {
        KorenPentode::Parameters p;
        p.mu = 34.9;
        p.ex = 1.35;
        p.kg1 = 2648.1;
        p.kg2 = 4500.0;
        p.kp = 222.06;
        p.kvb = 4.7;
        return p;
    }

    /** 6V6GT beam tetrode: the Deluxe Reverb's own fit -- published Koren 6V6GT set with kg1 softened for solver
        stability (the same documented departure that amp's file explains; no global feedback in this amp either,
        and the softened value has proven stable across every 6V6 model built here). */
    KorenPentode::Parameters pentode6V6()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 9000.0;
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer (not published): ~6.6k plate-to-plate for the 4x6V6 bank on the 8 ohm tap.
    constexpr double primaryHalfInductance = 6.0;
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 60.0;
    constexpr double secondaryResistance = 0.1;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the 8 ohm tap

    constexpr double outputKnee = 0.20, outputSpan = 0.08;
    inline double outputLimit (double v) noexcept
    {
        const double a = std::abs (v);
        return a <= outputKnee ? v : std::copysign (outputKnee + outputSpan * std::tanh ((a - outputKnee) / outputSpan), v);
    }

    // The Ch1 "Tone" rotary's six positions: a switched shunt capacitor on the channel's mix tap -- clockwise adds
    // capacitance, cutting more top end and so raising the apparent lows, exactly the feel the manual describes
    // ("clockwise increases low frequencies"). Values are ESTIMATED (no schematic published); positions are
    // monotonically spaced like the real switch's audible steps.
    constexpr double clickCaps[6] = { 1.0e-12, 0.56e-9, 1.2e-9, 2.7e-9, 5.6e-9, 12.0e-9 };

    // ---- reduced-order sag table (drive peak volts at pMix -> plate rail volts), Twin-shaped, rescaled; F37_POWERCAL
    // in the test file re-measures it against this model. ----
    constexpr int bmSagPoints = 19;
    constexpr double bmSagDrive[bmSagPoints] = { 0.002, 0.005, 0.010, 0.020, 0.032, 0.052, 0.078, 0.128, 0.20,
                                                 0.32, 0.50, 0.80, 1.20, 1.80, 2.60, 3.60, 4.80, 6.20, 8.00 };
    constexpr double bmSagRail[bmSagPoints] = { 410.0, 410.0, 410.0, 409.5, 409.0, 408.0, 406.5, 404.5, 402.0,
                                                398.5, 394.5, 390.0, 385.0, 380.0, 375.0, 370.0, 365.5, 361.5, 358.0 };
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
}

DividedBy13FTR37StyleAmplifierProcessor::DividedBy13FTR37StyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "f37_input", "Input", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("Channel 1"); case 2: return juce::String ("Both"); default: return juce::String ("Channel 2"); }
        }));
    auto ch1Vol = make ("f37_ch1_volume", "Ch1 Volume", 0.5f);
    auto click = std::make_unique<juce::AudioParameterFloat> (
        "f37_click", "Ch1 Tone", juce::NormalisableRange<float> (0.0f, 5.0f, 1.0f), 3.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return "Click " + juce::String (juce::roundToInt (v) + 1); }));
    auto ch2Vol = make ("f37_ch2_volume", "Ch2 Volume", 0.5f);
    auto boost = std::make_unique<juce::AudioParameterFloat> (
        "f37_boost", "Boost", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.5f ? juce::String ("Off") : juce::String ("On"); }));
    auto treble = make ("f37_treble", "Ch2 Treble", 0.5f);
    auto bass = make ("f37_bass", "Ch2 Bass", 0.5f);
    auto half = std::make_unique<juce::AudioParameterFloat> (
        "f37_half_power", "Power", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("Full") : juce::String ("Half"); }));
    auto output = make ("f37_output", "Output", 0.5f);
    auto power = make ("f37_power", "Power Drive", 1.0f);
    auto bias = make ("f37_bias", "Bias", 0.5f);
    auto feel = make ("f37_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "f37_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));

    inputParam = input.get();
    ch1VolumeParam = ch1Vol.get();
    clickParam = click.get();
    ch2VolumeParam = ch2Vol.get();
    boostParam = boost.get();
    trebleParam = treble.get();
    bassParam = bass.get();
    halfPowerParam = half.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ftr37", "Divided by 13 FTR 37-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (ch1Vol));
    group->addChild (std::move (click));
    group->addChild (std::move (ch2Vol));
    group->addChild (std::move (boost));
    group->addChild (std::move (treble));
    group->addChild (std::move (bass));
    group->addChild (std::move (half));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ftr37_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void DividedBy13FTR37StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply: GZ34 -> reservoir (plates) -> 1k -> screens
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 40.0e-6);
        c.addResistor (ch.sA, ch.sB, screenResistor);
        c.addCapacitor (ch.sB, gnd, 40.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -4.0 * idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(4.0 * idleScreenCurrent + piAndPreampCurrent));
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPlatesNominal - 8.0);
    }

    // ================================================================ preamp: two blocks (see the header note)
    // Two-pass build for the Click channel's block: a triode stands in for the 5879 once so the joint Newton
    // solve can find the DC neighbourhood (the EF86 fit's sharp knee sends a cold start off the cliff); the real
    // pentode is then built seeded with the measured voltages. Same two-pass trick the AC30 uses for its
    // cathode follower.
    {
        NodalCircuit probe;
        buildPreampCh1 (probe, ch, nullptr, true);
        probe.prepare (48000.0);
        const PreampSeed seed { probe.voltage (ch.pPlate1), probe.voltage (ch.pK1),
                               probe.voltage (ch.pPlate1b), probe.voltage (ch.pK1b),
                               0.0, 0.0, 0.0, 0.0 };
        buildPreampCh1 (ch.preCh1, ch, &seed, false);
        buildPreamp (ch.pre, ch);
    }

    buildPower (ch);
}

void DividedBy13FTR37StyleAmplifierProcessor::buildPreampCh1 (NodalCircuit& c, Channel& ch,
                                                             const PreampSeed* seed, bool triodeStandin)
{
    const auto gnd = NodalCircuit::ground;
    const auto rail = c.addNode();
    ch.pSrcRail1 = c.addSource (rail, railPlatesNominal * preFraction * 0.8);

    {
        const auto in1 = c.addNode();
        ch.pSrcIn1 = c.addSource (in1, 0.0);
        const auto g = c.addNode();
        ch.pK1 = c.addNode();
        ch.pPlate1 = c.addNode();
        c.addResistor (in1, g, 220.0e3); // the pentode grid's stopper (5879's own jack network, like the AC15's EF86)
        c.addResistor (g, gnd, 1.0e6);
        // Screen ~100 V through the real amp's large screen dropper (estimated), 2k7 cathode: the EF86 fit at
        // Vs 200 V pulls more than the 100k load can carry, so its DC point collapses to ~28 V and Newton
        // settles into that basin. At Vs 100 V / 2k7 the stage's available current stays below the load line's
        // -- the collapsed solution disappears and the stage lands mid-rail like the real 5879 preamp stage.
        if (triodeStandin)
            c.addTriode (ch.pPlate1, g, ch.pK1, triode12AX7());
        else
            ch.penPre1 = c.addPentode (ch.pPlate1, g, ch.pK1, pentode5879(), 100.0);
        c.addResistor (rail, ch.pPlate1, 100.0e3);
        c.addResistor (ch.pK1, gnd, 2.7e3);
        c.addCapacitor (ch.pK1, gnd, 25.0e-6);
        c.setInitialGuess (ch.pPlate1, seed != nullptr ? seed->plate1 : 210.0);
        c.setInitialGuess (ch.pK1, seed != nullptr ? seed->k1 : 1.4);

        // Ch1 Volume: 1M-A.
        const auto volTop = c.addNode(), wVol = c.addNode();
        c.addCapacitor (ch.pPlate1, volTop, 0.022e-6);
        c.addResistor (volTop, gnd, 1.0e6);
        ch.rVol1Top = c.addResistor (volTop, wVol, 0.5e6);
        ch.rVol1Bot = c.addResistor (wVol, gnd, 0.5e6);

        // Second Click-channel section: a 12AX7 whose output carries the six-position Tone switch.
        const auto g2 = c.addNode();
        ch.pK1b = c.addNode();
        ch.pPlate1b = c.addNode();
        c.addResistor (wVol, g2, 470.0e3); // series into the second stage's grid (the pentode channel runs hot)
        c.addResistor (g2, gnd, 1.0e6);
        c.addCapacitor (g2, ch.pPlate1b, cgp);
        c.addTriode (ch.pPlate1b, g2, ch.pK1b, triode12AX7());
        c.addResistor (rail, ch.pPlate1b, 100.0e3);
        c.addResistor (ch.pK1b, gnd, 1.5e3);
        c.addCapacitor (ch.pK1b, gnd, 25.0e-6);
        c.setInitialGuess (ch.pPlate1b, seed != nullptr ? seed->plate1b : 200.0);
        c.setInitialGuess (ch.pK1b, seed != nullptr ? seed->k1b : 1.5);

        ch.pToneTap = c.addNode();
        c.addCapacitor (ch.pPlate1b, ch.pToneTap, 0.022e-6);
        c.addResistor (ch.pToneTap, gnd, 470.0e3);
        ch.capClick = c.addCapacitor (ch.pToneTap, gnd, clickCaps[3]); // the click-switch shunt cap
        const auto mixRef = c.addNode();
        ch.pSrcMixRef = c.addSource (mixRef, 0.0); // set per sample to the other block's pMix
        c.addResistor (ch.pToneTap, mixRef, 100.0e3); // ch1's half of the real 100k/100k mix
    }
}

void DividedBy13FTR37StyleAmplifierProcessor::buildPreamp (NodalCircuit& c, Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const auto rail = c.addNode();
    ch.pSrcRail = c.addSource (rail, railPlatesNominal * preFraction * 0.8);
    ch.pMix = c.addNode();
    c.addResistor (ch.pMix, gnd, 1.0e6); // DC reference for the mix point (behind the coupling caps)
    const auto ch1In = c.addNode();
    ch.pSrcCh1Tap = c.addSource (ch1In, 0.0); // set per sample to preCh1's pToneTap
    c.addResistor (ch1In, ch.pMix, 100.0e3);  // ch1's 100k mix resistor, seen from this side

    // ---------------- Channel 2: 12AX7 -> Volume -> Treble/Bass stack -> boosted recovery -> pMix
    {
        const auto in2 = c.addNode();
        ch.pSrcIn2 = c.addSource (in2, 0.0);
        const auto g = c.addNode();
        ch.pK2a = c.addNode();
        ch.pPlate2a = c.addNode();
        c.addResistor (in2, g, 68.0e3);
        c.addResistor (g, gnd, 1.0e6);
        c.addCapacitor (g, ch.pPlate2a, cgp);
        c.addTriode (ch.pPlate2a, g, ch.pK2a, triode12AX7());
        c.addResistor (rail, ch.pPlate2a, 100.0e3);
        c.addResistor (ch.pK2a, gnd, 1.5e3);
        c.addCapacitor (ch.pK2a, gnd, 25.0e-6);
        c.setInitialGuess (ch.pPlate2a, 200.0);
        c.setInitialGuess (ch.pK2a, 1.5);

        const auto volTop = c.addNode(), wVol = c.addNode();
        c.addCapacitor (ch.pPlate2a, volTop, 0.022e-6);
        c.addResistor (volTop, gnd, 1.0e6);
        ch.rVol2Top = c.addResistor (volTop, wVol, 0.5e6);
        ch.rVol2Bot = c.addResistor (wVol, gnd, 0.5e6);

        // Blackface Treble/Bass stack (no Mid control on this channel's panel; fixed 6k8 tail where a Mid
        // pot would sit) -- wired exactly like the Deluxe's proven stack: the Bass pot bridges the two
        // cap-end nodes n1/n2, the treble pot feeds the wiper from the cap top into n1.
        const auto pA = c.addNode(), pSlope = c.addNode(), pN1 = c.addNode(), pN2 = c.addNode(), pTone = c.addNode();
        c.addCapacitor (wVol, pA, 250.0e-12);
        c.addResistor (wVol, pSlope, 100.0e3);
        ch.rTrebleTop = c.addResistor (pA, pTone, 125.0e3);
        ch.rTrebleBot = c.addResistor (pTone, pN1, 125.0e3);
        c.addCapacitor (pSlope, pN1, 0.1e-6);
        ch.rBass = c.addResistor (pN1, pN2, 250.0e3);
        c.addCapacitor (pSlope, pN2, 0.047e-6);
        c.addResistor (pN2, gnd, 6.8e3);

        // Recovery stage; its cathode bypass is switched in/out by the Volume knob's pull Mid+Gain boost.
        const auto g2 = c.addNode();
        ch.pK2b = c.addNode();
        ch.pPlate2b = c.addNode();
        ch.pBoostTap = c.addNode();
        c.addCapacitor (pTone, g2, 0.022e-6);
        c.addResistor (g2, gnd, 1.0e6);
        c.addCapacitor (g2, ch.pPlate2b, cgp);
        c.addTriode (ch.pPlate2b, g2, ch.pK2b, triode12AX7());
        c.addResistor (rail, ch.pPlate2b, 100.0e3);
        c.addResistor (ch.pK2b, gnd, 1.5e3);
        ch.rBoost = c.addResistor (ch.pK2b, ch.pBoostTap, 1.0e9); // off until pulled
        c.addCapacitor (ch.pBoostTap, gnd, 22.0e-6);
        c.setInitialGuess (ch.pPlate2b, 200.0);
        c.setInitialGuess (ch.pK2b, 1.5);
        const auto mixTap2 = c.addNode();
        c.addCapacitor (ch.pPlate2b, mixTap2, 0.022e-6);
        c.addResistor (mixTap2, gnd, 470.0e3);
        c.addResistor (mixTap2, ch.pMix, 100.0e3);
    }
}

void DividedBy13FTR37StyleAmplifierProcessor::buildPower (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ power: LTP 12AX7 + 4x 6V6GT fixed bias, no NFB
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (0.9); // the shared amp pattern: power netlists run near backward-Euler
        const auto rail = c.addNode(), piRail = c.addNode(), biasSrc = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcPi = c.addSource (piRail, railPlatesNominal * piFraction * 0.9);
        ch.wSrcBias = c.addSource (biasSrc, biasNominal);
        ch.wSrcIn = c.addSource (in, 0.0);

        // Long-tailed pair (12AX7): 82k/100k plates, 470 + 22k tail, 1M grid leaks to the tail tap.
        ch.wPiK = c.addNode();
        const auto tap = c.addNode(), gB = c.addNode();
        ch.wPiGrid = c.addNode();
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        c.addCapacitor (in, ch.wPiGrid, 0.047e-6);
        c.addResistor (ch.wPiGrid, tap, 1.0e6);
        c.addResistor (gB, tap, 1.0e6);
        c.addResistor (ch.wPiK, tap, 470.0);
        c.addResistor (tap, gnd, 22.0e3);
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode12AX7());
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode12AX7());
        c.addResistor (piRail, ch.wPiPlateA, 82.0e3);
        c.addResistor (piRail, ch.wPiPlateB, 100.0e3);
        c.setInitialGuess (ch.wPiPlateA, 210.0);
        c.setInitialGuess (ch.wPiPlateB, 215.0);
        c.setInitialGuess (ch.wPiK, 40.0);
        c.setInitialGuess (tap, 39.0);
        c.setInitialGuess (ch.wPiGrid, 39.0);
        c.setInitialGuess (gB, 39.0);

        // .047 uF couplings -> 220k grid leaks into the fixed-bias node (15k from the bias supply, 10 uF), 1k5
        // stoppers -- the same grid network the Trainwreck model uses, minus the NFB.
        ch.wBias = c.addNode();
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        const auto g3s = c.addNode(), g4s = c.addNode();
        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.wKA2 = c.addNode();
        ch.wKB2 = c.addNode();
        c.addResistor (biasSrc, ch.wBias, 15.0e3);
        c.addCapacitor (ch.wBias, gnd, 10.0e-6);
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 0.047e-6);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 0.047e-6);
        c.addResistor (ch.wGridA, ch.wBias, 220.0e3);
        c.addResistor (ch.wGridB, ch.wBias, 220.0e3);
        c.addResistor (ch.wGridA, g3s, 1.5e3);
        c.addResistor (ch.wGridB, g4s, 1.5e3);
        c.setInitialGuess (ch.wBias, biasNominal);
        c.setInitialGuess (ch.wGridA, biasNominal);
        c.setInitialGuess (ch.wGridB, biasNominal);
        c.setInitialGuess (g3s, biasNominal);
        c.setInitialGuess (g4s, biasNominal);

        // FOUR 6V6GTs as four real devices sharing plate/grid nodes per side: the Full/Half switch lifts the second
        // tube of each pair's cathode (47k to ground) -- the classic cathode-lift half-power circuit, so the two
        // surviving tubes keep their own operating point instead of the quartet's being emulated by scaling.
        const double screenIdle = railPlatesNominal - screenResistor * (4.0 * idleScreenCurrent + piAndPreampCurrent);
        ch.penA1 = c.addPentode (ch.wPP1, g3s, gnd, pentode6V6(), screenIdle);
        ch.penA2 = c.addPentode (ch.wPP1, g3s, ch.wKA2, pentode6V6(), screenIdle);
        ch.penB1 = c.addPentode (ch.wPP2, g4s, gnd, pentode6V6(), screenIdle);
        ch.penB2 = c.addPentode (ch.wPP2, g4s, ch.wKB2, pentode6V6(), screenIdle);
        ch.rLiftA = c.addResistor (ch.wKA2, gnd, 0.001); // Full: hard ground; Half: 47k -> self-cuts at idle
        ch.rLiftB = c.addResistor (ch.wKB2, gnd, 0.001);
        c.setInitialGuess (ch.wPP1, 405.0);
        c.setInitialGuess (ch.wPP2, 405.0);
        c.setInitialGuess (ch.wKA2, 0.0);
        c.setInitialGuess (ch.wKB2, 0.0);

        // Output transformer: centre tap on the plate rail, 8 ohm tap, no feedback off the secondary.
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double turns = std::sqrt (6600.0 / speakerNominal[matchedSpeaker]) / 2.0;
        const double lh = primaryHalfInductance;
        const double ls = lh / (turns * turns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, ch.wPP1 }, { a2, ch.wPP2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        ch.wOut = c.addNode();
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
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
    }
}

void DividedBy13FTR37StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void DividedBy13FTR37StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double biasVolts = biasNominal + biasSpan * (k.bias - 0.5);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double liftR = k.halfPower == 1 ? 47.0e3 : 0.001;
    const double boostR = k.boost == 1 ? 1.0 : 1.0e9;
    const double clickC = clickCaps[juce::jlimit (0, 5, k.click)];

    // setCapacitance marks the circuit dirty (next solve refactorizes), so only touch it on real changes.
    if (k.click != appliedClick)
    {
        for (auto& ch : channels)
            ch.preCh1.setCapacitance (ch.capClick, clickC);
        appliedClick = k.click;
    }

    for (auto& ch : channels)
    {
        const double vol1Bottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume1));
        const double vol2Bottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume2));
        const double trebleBottom = juce::jmax (1.0, 125.0e3 * 2.0 * k.treble); // 250k-A, split like the Deluxe's
        const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
        const double bassR = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
        ch.preCh1.setResistance (ch.rVol1Bot, vol1Bottom);
        ch.preCh1.setResistance (ch.rVol1Top, juce::jmax (1.0, 1.0e6 - vol1Bottom));
        ch.pre.setResistance (ch.rVol2Bot, vol2Bottom);
        ch.pre.setResistance (ch.rVol2Top, juce::jmax (1.0, 1.0e6 - vol2Bottom));
        ch.pre.setResistance (ch.rTrebleTop, trebleTop);
        ch.pre.setResistance (ch.rTrebleBot, trebleBottom);
        ch.pre.setResistance (ch.rBass, bassR);
        ch.pre.setResistance (ch.rBoost, boostR);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcBias, biasVolts);
            ch.power.setResistance (ch.rLiftA, liftR);
            ch.power.setResistance (ch.rLiftB, liftR);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void DividedBy13FTR37StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.preCh1.restoreDynamicState (ch.preCh1Rest);
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void DividedBy13FTR37StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (ch.sumCount > 0)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.sumScreen / n + piAndPreampCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcRail, plates);
        ch.power.setPentodeScreen (ch.penA1, screens);
        ch.power.setPentodeScreen (ch.penA2, screens);
        ch.power.setPentodeScreen (ch.penB1, screens);
        ch.power.setPentodeScreen (ch.penB2, screens);
        ch.power.setSource (ch.wSrcPi, ch.piRail);
    }
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * piFraction - ch.piRail);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau));
    ch.preRail += b * (screens * preFraction - ch.preRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
    ch.preCh1.setSource (ch.pSrcRail1, ch.preRail);
    // The mix node feeds the power block as a Thevenin source, so its DC has to be removed here the same way
    // the real amp's coupling cap + grid leak removes it: a ~5 ms one-pole tracker (~32 Hz, the corner of the real coupling
    // corner) follows the node's DC point as the supply sags into its settled state instead of injecting DC
    // into the LTP.
    ch.mixDc += (1.0 - std::exp (-dt / 0.005)) * (ch.pre.voltage (ch.pMix) - ch.mixDc);
}

double DividedBy13FTR37StyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    return ch.pre.voltage (ch.pMix) - ch.mixDc;
}

double DividedBy13FTR37StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    // Half power lifts one 6V6 pair's cathodes in the full-order model: less idle current,
    // earlier clip, a little less output. The behavioural model gets reduced drive plus a
    // lower ceiling below.
    if (lastKnobs.halfPower == 1)
        driveVoltage *= 0.85;
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope) * (lastKnobs.halfPower == 1 ? 0.8 : 1.0);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, driveVoltage);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double DividedBy13FTR37StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::ch1Plate: return ch.preCh1.voltage (ch.pPlate1);
        case Probe::ch1K2: return ch.preCh1.voltage (ch.pK1b);
        case Probe::ch2Plate1: return ch.pre.voltage (ch.pPlate2a);
        case Probe::ch2Plate2: return ch.pre.voltage (ch.pPlate2b);
        case Probe::mixNode: return ch.pre.voltage (ch.pMix);
        case Probe::pOut: return preampOutput (ch);
        case Probe::piPlateA: return ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return ch.power.voltage (ch.wPiPlateB);
        case Probe::piCathode: return ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

void DividedBy13FTR37StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

double DividedBy13FTR37StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double ip = 0.0, is = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA1, ip, is);
    double a = ip;
    channels[0].power.pentodeCurrents (channels[0].penA2, ip, is);
    a += ip;
    channels[0].power.pentodeCurrents (channels[0].penB1, ip, is);
    a += ip;
    channels[0].power.pentodeCurrents (channels[0].penB2, ip, is);
    a += ip;
    return a;
}

void DividedBy13FTR37StyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize, numChannels);
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    const auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double time)
    {
        s.reset (newSampleRate, time);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedCh1Volume, ch1VolumeParam, 0.02);
    setup (smoothedCh2Volume, ch2VolumeParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = idlePlateCurrent * 4.0 + idleScreenCurrent * 4.0 + piAndPreampCurrent;
    appliedSpeaker = matchedSpeaker;
    appliedClick = -1;
    updatePots ({ ch1VolumeParam->get(), ch2VolumeParam->get(), trebleParam->get(), bassParam->get(), powerParam->get(),
                  biasParam->get(), tubeFeelParam->get(), juce::roundToInt (inputParam->get()),
                  juce::roundToInt (clickParam->get()), juce::roundToInt (boostParam->get()),
                  juce::roundToInt (halfPowerParam->get()), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const bool s1 = ch.supply.prepare (supplyRate);
        dcOk = s1 && dcOk;

        ch.preCh1.setSource (ch.pSrcRail1, ch.supply.voltage (ch.sB) * preFraction);
        const bool p1 = ch.preCh1.prepare (newSampleRate);
        ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * preFraction);
        const bool p2 = ch.pre.prepare (newSampleRate);
        dcOk = p1 && p2 && dcOk;
        ch.mixDc = ch.pre.voltage (ch.pMix);

        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcIn, 0.0);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sA));
            const double scr = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA1, scr);
            ch.power.setPentodeScreen (ch.penA2, scr);
            ch.power.setPentodeScreen (ch.penB1, scr);
            ch.power.setPentodeScreen (ch.penB2, scr);
            dcOk = ch.power.prepare (newSampleRate) && dcOk;
            ch.power.solveSample();
            double ip = 0.0, is = 0.0;
            ch.power.pentodeCurrents (ch.penA1, ip, is); ipA += ip; isA += is;
            ch.power.pentodeCurrents (ch.penA2, ip, is); ipA += ip; isA += is;
            ch.power.pentodeCurrents (ch.penB1, ip, is); ipB += ip; isB += is;
            ch.power.pentodeCurrents (ch.penB2, ip, is); ipB += ip; isB += is;
        }
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB));
        ch.supply.setCurrentSource (ch.iB, -(isA + isB + piAndPreampCurrent));
        idleSupplyCurrent = ipA + ipB + isA + isB + piAndPreampCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sA));
            const double scr = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA1, scr);
            ch.power.setPentodeScreen (ch.penA2, scr);
            ch.power.setPentodeScreen (ch.penB1, scr);
            ch.power.setPentodeScreen (ch.penB2, scr);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
        }
        ch.piRail = ch.supply.voltage (ch.sB) * piFraction;
        ch.preRail = ch.supply.voltage (ch.sB) * preFraction;
        ch.pre.setSource (ch.pSrcRail, ch.preRail);
        ch.preCh1.setSource (ch.pSrcRail1, ch.preRail);
        ch.preCh1.saveDynamicState (ch.preCh1Rest);
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
    recoveries = 0;
    shortcut.reset();
}

void DividedBy13FTR37StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedCh1Volume.setTargetValue (ch1VolumeParam->get());
    smoothedCh2Volume.setTargetValue (ch2VolumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get()); // 0 Ch1, 1 Ch2, 2 Both
    const int clickChoice = juce::roundToInt (clickParam->get());
    const int boostChoice = juce::roundToInt (boostParam->get());
    const int halfChoice = juce::roundToInt (halfPowerParam->get());
    const bool inCh1 = inputChoice != 1;
    const bool inCh2 = inputChoice != 0;

    for (int i = 0; i < numSamples; ++i)
    {
        Knobs knobs {};
        knobs.volume1 = smoothedCh1Volume.getNextValue();
        knobs.volume2 = smoothedCh2Volume.getNextValue();
        knobs.treble = smoothedTreble.getNextValue();
        knobs.bass = smoothedBass.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            knobs.powerDrive = pw;
            knobs.bias = bi;
            knobs.tubeFeel = fe;
            knobs.input = inputChoice;
            knobs.click = clickChoice;
            knobs.boost = boostChoice;
            knobs.halfPower = halfChoice;
            knobs.speaker = speakerChoice;
            updatePots (knobs);
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            // One-sample-delayed Thevenin loop between the two preamp blocks: ch1 sees last sample's pMix behind
            // its 100k mix resistor, ch2's block sees this sample's ch1 tap behind its own (loop gain < 1).
            ch.preCh1.setSource (ch.pSrcIn1, inCh1 ? x : 0.0);
            ch.preCh1.setSource (ch.pSrcMixRef, ch.pre.voltage (ch.pMix));
            const bool okPre1 = ch.preCh1.solveSample();
            ch.pre.setSource (ch.pSrcIn2, inCh2 ? x : 0.0);
            ch.pre.setSource (ch.pSrcCh1Tap, ch.preCh1.voltage (ch.pToneTap));
            const bool okPre2 = ch.pre.solveSample();
            const bool okPre = okPre1 && okPre2;
            bool ok = okPre;

            const double drive = masterGain * preampOutput (ch);
            bool ok2 = true; // reducedOrder: ch.power is empty, nothing to solve
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, drive);
                ok2 = ch.power.solveSample();
            }
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            if (! reducedOrder)
            {
                double ip = 0.0, is = 0.0;
                ch.power.pentodeCurrents (ch.penA1, ip, is); ch.sumPlate += ip; ch.sumScreen += is;
                ch.power.pentodeCurrents (ch.penA2, ip, is); ch.sumPlate += ip; ch.sumScreen += is;
                ch.power.pentodeCurrents (ch.penB1, ip, is); ch.sumPlate += ip; ch.sumScreen += is;
                ch.power.pentodeCurrents (ch.penB2, ip, is); ch.sumPlate += ip; ch.sumScreen += is;
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, drive) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;

            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.preCh1.saveDynamicState (ch.preCh1Rest);
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
                out = speakerVolts * (reducedOrder ? outputScale : fullOutputScale) * outGain * speakerGain;
                if (ch.alignOutput)
                {
                    ch.declick = ch.lastEmitted - out;
                    ch.alignOutput = false;
                }
                out += ch.declick;
                ch.declick *= declickDecay;
                out = outputLimit (out);
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

void DividedBy13FTR37StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No dedicated amp glyph exists on the reference sheet yet (same placeholder the other amps use).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
