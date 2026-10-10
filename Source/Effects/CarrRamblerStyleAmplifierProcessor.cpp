#include "CarrRamblerStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/CarrRambler.md). No factory schematic is published; the rails are ESTIMATED for a
    // cathode-biased 2x5881/6L6GC class-A-ish stage (Steve Carr describes the power section as "early 50s"), so the idle
    // currents below are chosen to land plate/screen rails in the usual 6L6GC cathode-bias range. ----
    constexpr double railPlatesNominal = 405.0;   // OT centre tap (estimated)
    constexpr double rectifierResistance = 120.0; // GZ34/5AR4 + PT (estimated -- the power section is voiced vintage)
    constexpr double screenResistor = 1.0e3;      // plate node -> screen node dropper
    constexpr double idlePlateCurrent = 0.042;    // per output tube, a 6L6GC cathode-biased near class A
    constexpr double idleScreenCurrent = 0.005;
    constexpr double piAndPreampCurrent = 0.006;  // 5751 PI + two 12AX7 halves, drawn from the screen node (estimated)
    constexpr double piFraction = 0.72;           // PI rail / screen node
    constexpr double preFraction = 0.66;          // preamp rail / screen node
    double idleSupplyCurrent = 0.0;

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set

    /** 5751: the Rambler's phase inverter (a 12AX7-family tube with ~70% of a 12AX7's gain). Koren's published 5751
        SPICE set (Duncan/Munro circulation): mu 70, ex 1.35, kg1 1180, kp 300, kvb 45 -- used unmodified. */
    KorenTriode::Parameters triode5751()
    {
        KorenTriode::Parameters p;
        p.mu = 70.0;
        p.ex = 1.35;
        p.kg1 = 1180.0;
        p.kp = 300.0;
        p.kvb = 45.0;
        return p;
    }

    /** One 6L6GC/5881 beam tetrode per push-pull side, unmodified Koren 6L6GC set (KorenPentode::Parameters{}'s
        default -- the same fit Twin Reverb's pair doubles up). The real amp is cathode-biased, so no NFB-era
        stability softening is needed here. */
    KorenPentode::Parameters pentode6L6() { return {}; }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Output transformer (not published): ~6.6k plate-to-plate -- a believable load for a cathode-biased 6L6GC pair --
    // on the real amp's single 8 ohm secondary.
    constexpr double primaryHalfInductance = 6.0;
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 60.0;
    constexpr double secondaryResistance = 0.1;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the transformer's 8 ohm tap

    // Output protection, same reasoning as TwinReverbStyleAmplifierProcessor's outputLimit.
    constexpr double outputKnee = 0.20, outputSpan = 0.08;
    inline double outputLimit (double v) noexcept
    {
        const double a = std::abs (v);
        return a <= outputKnee ? v : std::copysign (outputKnee + outputSpan * std::tanh ((a - outputKnee) / outputSpan), v);
    }

    // ---- reduced-order sag table (drive peak volts at pOut -> plate rail volts). Seeded from the Twin's measured
    // table shape, rescaled to this amp's estimated rail; CR_POWERCAL in the test file re-measures it. ----
    constexpr int bmSagPoints = 19;
    constexpr double bmSagDrive[bmSagPoints] = { 0.002, 0.005, 0.010, 0.020, 0.032, 0.052, 0.078, 0.128, 0.20,
                                                 0.32, 0.50, 0.80, 1.20, 1.80, 2.60, 3.60, 4.80, 6.20, 8.00 };
    constexpr double bmSagRail[bmSagPoints] = { 405.0, 405.0, 405.0, 404.5, 404.0, 403.0, 401.5, 399.5, 397.0,
                                                393.5, 389.5, 385.0, 380.0, 375.0, 370.0, 365.0, 360.5, 356.5, 353.0 };
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

CarrRamblerStyleAmplifierProcessor::CarrRamblerStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("cr_volume", "Volume", 0.5f);
    auto treble = make ("cr_treble", "Treble", 0.5f);
    auto middle = make ("cr_middle", "Mid", 0.5f);
    auto bass = make ("cr_bass", "Bass", 0.5f);
    auto speed = make ("cr_speed", "Speed", 0.5f);
    // The panel calls this "Depth"; the param id ends in _intensity so the noon-unity suite leaves it at its
    // default 0 (a modulation-depth control, not a level control).
    auto intensity = make ("cr_intensity", "Depth", 0.0f);
    auto mode = std::make_unique<juce::AudioParameterFloat> (
        "cr_mode", "Mode", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("Pentode") : juce::String ("Triode"); }));
    auto output = make ("cr_output", "Output", 0.5f);
    auto power = make ("cr_power", "Power Drive", 1.0f);
    auto bias = make ("cr_bias", "Bias", 0.5f);
    auto feel = make ("cr_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "cr_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm"; }));

    volumeParam = volume.get();
    trebleParam = treble.get();
    middleParam = middle.get();
    bassParam = bass.get();
    speedParam = speed.get();
    intensityParam = intensity.get();
    modeParam = mode.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "carrrambler", "Carr Rambler-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (middle));
    group->addChild (std::move (bass));
    group->addChild (std::move (speed));
    group->addChild (std::move (intensity));
    group->addChild (std::move (mode));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("carrrambler_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void CarrRamblerStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply: rectifier -> reservoir (plates) -> 1k -> screens
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
        ch.iA = c.addCurrentSource (ch.sA, -2.0 * idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(2.0 * idleScreenCurrent + piAndPreampCurrent));
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPlatesNominal - 8.0);
    }

    // ================================================================ preamp: 12AX7 -> coupling cap -> Volume
    // (the Trainwreck-Express input shape exactly: gain stage, 22 nF into the pot top, pot halves to ground).
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, railPlatesNominal * preFraction * 0.8);

        // TrainwreckExpress's proven stage recipe, verbatim: stopper + optional leak + triode + Miller Cgp +
        // plate load + cathode R (+optional bypass). Three cascaded stages share ONE solve -- that shared solve
        // is what keeps the iterate on a single attractor (splitting stages into per-stage blocks cycled).
        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double rk, double ck,
                          NodalCircuit::Node& plate, NodalCircuit::Node& cathode, double plateGuess, double cathodeGuess)
        {
            const auto g = c.addNode();
            plate = c.addNode();
            cathode = c.addNode();
            c.addResistor (gIn, g, stopper);
            if (leak > 0.0)
                c.addResistor (g, gnd, leak);
            c.addTriode (plate, g, cathode, triode12AX7());
            c.addCapacitor (g, plate, cgp);
            c.addResistor (rail, plate, 100.0e3);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
        };

        // V1A input stage: 68k stopper, 1M input leak, 100k / 1k5 + 22 uF (blackface values).
        stage (in, 68.0e3, 1.0e6, 1.5e3, 22.0e-6, ch.pPlate1, ch.pK1, 180.0, 1.4);

        // .022 -> the single 1M-A Volume pot (top/bottom track halves).
        const auto v1Out = c.addNode();
        ch.pTone = c.addNode(); // the wiper (kept for probing)
        c.addCapacitor (ch.pPlate1, v1Out, 22.0e-9);
        ch.rVolTop = c.addResistor (v1Out, ch.pTone, 0.5e6);
        ch.rVolBot = c.addResistor (ch.pTone, gnd, 0.5e6);

        // V1B recovery: wiper-fed through 68k, no leak (floats at wiper DC), 2k7 + 0.68 uF partial bypass.
        stage (ch.pTone, 68.0e3, 0.0, 2.7e3, 0.68e-6, ch.pPlate2, ch.pK2, 200.0, 1.9);

        // .022 -> V2A cold stage (10k unbypassed), 1R stopper + 1M leak.
        const auto c2 = c.addNode();
        NodalCircuit::Node pPlate3 = 0, pK3 = 0;
        c.addCapacitor (ch.pPlate2, c2, 22.0e-9);
        stage (c2, 1.0, 1.0e6, 10.0e3, 0.0, pPlate3, pK3, 250.0, 3.0);

        // Blackface TMB hanging off V2A's plate (rides at plate DC; Fender wiring: both legs off the slope node,
        // Bass bridges the summing nodes, Mid resistor to ground).
        const auto pTop = c.addNode(), pSlope = c.addNode(), pN1 = c.addNode(), pN2 = c.addNode();
        ch.pOut = c.addNode(); // the treble wiper / stack output
        c.addCapacitor (pPlate3, pTop, 500.0e-12);
        c.addResistor (pPlate3, pSlope, 100.0e3);
        ch.rTrebleTop = c.addResistor (pTop, ch.pOut, 125.0e3);
        ch.rTrebleBot = c.addResistor (ch.pOut, pN1, 125.0e3);
        c.addCapacitor (pSlope, pN1, 22.0e-9);
        ch.rBass = c.addResistor (pN1, pN2, 125.0e3);
        c.addCapacitor (pSlope, pN2, 22.0e-9);
        ch.rMid = c.addResistor (pN2, gnd, 12.5e3);
        c.addResistor (ch.pOut, gnd, 1.0e6); // the PI grid leak, seen from the stack
    }

    // ================================================================ power: LTP 5751 + 2x 6L6GC cathode-biased, no NFB
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (0.9); // the shared amp pattern: power netlists run near backward-Euler
        const auto rail = c.addNode(), piRail = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcPi = c.addSource (piRail, railPlatesNominal * piFraction * 0.9);
        ch.wSrcIn = c.addSource (in, 0.0);

        // Long-tailed pair (5751): 82k/100k plates, 470 + 22k tail, 1M grid leaks to the tail tap -- the same
        // elevated-cathode LTP the Trainwreck model uses, which converges cleanly.
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
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode5751());
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode5751());
        c.addResistor (piRail, ch.wPiPlateA, 82.0e3);
        c.addResistor (piRail, ch.wPiPlateB, 100.0e3);
        c.setInitialGuess (ch.wPiPlateA, 200.0);
        c.setInitialGuess (ch.wPiPlateB, 205.0);
        c.setInitialGuess (ch.wPiK, 35.0);
        c.setInitialGuess (tap, 34.0);
        c.setInitialGuess (ch.wPiGrid, 34.0);
        c.setInitialGuess (gB, 34.0);

        // .047 uF couplings -> 220k grid leaks into wTrem: the bias-vary tremolo injection node. At Depth 0 the
        // source holds it at 0 V and the leaks are simply grounded; at Depth > 0 a slow LFO swings the grids' DC
        // reference, modulating the output-stage bias -- the "late 50s" bias tremolo the real amp ships.
        ch.wTrem = c.addNode();
        ch.wSrcTrem = c.addSource (ch.wTrem, 0.0);
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        const auto g3s = c.addNode(), g4s = c.addNode();
        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.wCathodeBias = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 0.047e-6);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 0.047e-6);
        c.addResistor (ch.wGridA, ch.wTrem, 220.0e3);
        c.addResistor (ch.wGridB, ch.wTrem, 220.0e3);
        c.addResistor (ch.wGridA, g3s, 1.5e3);
        c.addResistor (ch.wGridB, g4s, 1.5e3);
        const double screenIdle = railPlatesNominal - screenResistor * (2.0 * idleScreenCurrent + piAndPreampCurrent);
        ch.penA = c.addPentode (ch.wPP1, g3s, ch.wCathodeBias, pentode6L6(), screenIdle);
        ch.penB = c.addPentode (ch.wPP2, g4s, ch.wCathodeBias, pentode6L6(), screenIdle);
        // Shared cathode-bias pair: ~300 ohm + 100 uF to ground -- the self-bias "class A" operating point.
        ch.rCathodeBias = c.addResistor (ch.wCathodeBias, gnd, 300.0);
        c.addCapacitor (ch.wCathodeBias, gnd, 100.0e-6);
        c.setInitialGuess (ch.wPP1, 400.0);
        c.setInitialGuess (ch.wPP2, 400.0);
        c.setInitialGuess (ch.wCathodeBias, 27.0);
        c.setInitialGuess (ch.wGridA, 0.0);
        c.setInitialGuess (ch.wGridB, 0.0);
        c.setInitialGuess (g3s, 0.0);
        c.setInitialGuess (g4s, 0.0);

        // Output transformer: centre tap on the plate rail, 8 ohm tap. No feedback off the secondary.
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

void CarrRamblerStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void CarrRamblerStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Shared cathode-bias resistor: 300 ohm at noon, a plausible service-tweak range.
    const double cathodeR = 300.0 * (0.7 + 0.6 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        const double trebleBottom = juce::jmax (1.0, 125.0e3 * 2.0 * k.treble);
        const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
        const double bassR = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
        const double midR = juce::jmax (1.0, 10.0e3 * pots::audio (k.middle));
        const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
        ch.pre.setResistance (ch.rTrebleTop, trebleTop);
        ch.pre.setResistance (ch.rTrebleBot, trebleBottom);
        ch.pre.setResistance (ch.rBass, bassR);
        ch.pre.setResistance (ch.rMid, midR);
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        if (! reducedOrder)
            ch.power.setResistance (ch.rCathodeBias, cathodeR);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void CarrRamblerStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void CarrRamblerStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    const double measDt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    if (ch.sumCount > 0)
    {
        // A real rectifier+reservoir answers a change in plate draw over ~200 ms, not in one 8-sample tick.
        // The cathode-bias stage closes a positive-feedback loop through the sag path (rail drops -> cathode
        // voltage drops -> tubes bias hotter -> draw rises -> rail drops further) which motorboats outright
        // if the feedback is applied at full strength; responseAtDraw < 1 is the modelling equivalent of a
        // stiffer mains supply and damps the loop. Sag still happens -- just bounded.
        const double mk = 1.0 - std::exp (-measDt / 0.2);
        ch.fbPlate += mk * (ch.sumPlate / n - ch.fbPlate);
        ch.fbScreen += mk * (ch.sumScreen / n - ch.fbScreen);
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.fbPlate));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.fbScreen + piAndPreampCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcRail, plates);
        if (lastKnobs.mode == 0) // pentode: screens sit on the screen rail; triode follows plates per-sample
        {
            ch.power.setPentodeScreen (ch.penA, screens);
            ch.power.setPentodeScreen (ch.penB, screens);
        }
        ch.power.setSource (ch.wSrcPi, ch.piRail);
    }
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * piFraction - ch.piRail);
    // The tapped plate node feeds the power block as a Thevenin source, so its DC has to be removed here the
    // same way the real amp's coupling cap + grid leak removes it: a ~5 ms one-pole tracker (~32 Hz, the corner of the real
    // 22 nF / 470 k coupling corner) follows the plate's DC point as the supply sags into its settled state --
    // ~30 V of settle drift would otherwise inject DC into the LTP for seconds.
    ch.outDc += (1.0 - std::exp (-dt / 0.005)) * (ch.pre.voltage (ch.pOut) - ch.outDc);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau));
    ch.preRail += b * (screens * preFraction - ch.preRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
}

double CarrRamblerStyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    // Makeup gain for the cold (unbypassed) cathode bias: ~12 dB/stage vs a bypassed 1.5k stage,
    // needed to swing the phase splitter's grids. The bias choice is the stable solver operating
    // point (documented in docs/circuits/CarrRambler.md); this restores the lost AC drive.
    return ch.pre.voltage (ch.pOut) - ch.outDc;
}

double CarrRamblerStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, driveVoltage);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double CarrRamblerStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::stage1Plate: return ch.pre.voltage (ch.pPlate1);
        case Probe::stage1Cathode: return ch.pre.voltage (ch.pK1);
        case Probe::recoveryPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::recoveryCathode: return ch.pre.voltage (ch.pK2);
        case Probe::toneOut: return ch.pre.voltage (ch.pTone);
        case Probe::pOut: return preampOutput (ch);
        case Probe::piPlateA: return ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return ch.power.voltage (ch.wPiPlateB);
        case Probe::piCathode: return ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::cathodeBias: return ch.power.voltage (ch.wCathodeBias);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::tremNode: return ch.power.voltage (ch.wTrem);
    }
    return 0.0;
}

void CarrRamblerStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

double CarrRamblerStyleAmplifierProcessor::plateCurrentA() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, s = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, s);
    return a;
}

double CarrRamblerStyleAmplifierProcessor::plateCurrentB() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double b = 0.0, s = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penB, b, s);
    return b;
}

void CarrRamblerStyleAmplifierProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
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
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMiddle, middleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedSpeed, speedParam, 0.02);
    setup (smoothedIntensity, intensityParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = idlePlateCurrent * 2.0 + idleScreenCurrent * 2.0 + piAndPreampCurrent;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ volumeParam->get(), trebleParam->get(), middleParam->get(), bassParam->get(), intensityParam->get(),
                  powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()),
                  juce::roundToInt (modeParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * preFraction);
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        ch.outDc = ch.pre.voltage (ch.pOut);
        ch.driveDc = 0.0;
        ch.driveDcCoeff = 1.0 - std::exp (-1.0 / (0.03 * newSampleRate)); // ~5 Hz PI coupling corner

        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcIn, 0.0);
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sA));
            const double scr = ch.supply.voltage (ch.sB);
            ch.power.setPentodeScreen (ch.penA, scr);
            ch.power.setPentodeScreen (ch.penB, scr);
            dcOk = ch.power.prepare (newSampleRate) && dcOk;
            ch.power.solveSample();
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
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
            if (lastKnobs.mode == 0)
            {
                ch.power.setPentodeScreen (ch.penA, scr);
                ch.power.setPentodeScreen (ch.penB, scr);
            }
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sB) * piFraction);
        }
        ch.piRail = ch.supply.voltage (ch.sB) * piFraction;
        ch.preRail = ch.supply.voltage (ch.sB) * preFraction;
        ch.pre.setSource (ch.pSrcRail, ch.preRail);
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
    lfoPhase = 0.0;
}

void CarrRamblerStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedSpeed.setTargetValue (speedParam->get());
    smoothedIntensity.setTargetValue (intensityParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int modeChoice = juce::roundToInt (modeParam->get()); // 0 pentode, 1 triode

    for (int i = 0; i < numSamples; ++i)
    {
        Knobs knobs {};
        knobs.volume = smoothedVolume.getNextValue();
        knobs.treble = smoothedTreble.getNextValue();
        knobs.middle = smoothedMiddle.getNextValue();
        knobs.bass = smoothedBass.getNextValue();
        const float sp = smoothedSpeed.getNextValue();
        const float in = smoothedIntensity.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        const double lfoHz = 1.0 + 11.0 * pots::audio ((double) sp);
        lfoPhase += lfoHz / sampleRate;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
        const double lfo = std::sin (2.0 * juce::MathConstants<double>::pi * lfoPhase);
        const double depth = (double) in;
        // +-10 V of bias-vary swing at full Depth, on top of the grids' 0 V reference.
        const double tremVolts = depth * 10.0 * lfo;
        const double tremMod = 1.0 - depth * 0.5 * (1.0 + lfo); // reducedOrder's amplitude approximation

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            knobs.powerDrive = pw;
            knobs.bias = bi;
            knobs.tubeFeel = fe;
            knobs.speaker = speakerChoice;
            knobs.mode = modeChoice;
            knobs.depth = depth;
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
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            const double driveRaw = masterGain * preampOutput (ch);
            // The LTP's input is AC-coupled in the real amp: track away the preamp's sub-sonic point at the
            // real coupling corner (~5 Hz) so only audio reaches the PI grids.
            ch.driveDc += ch.driveDcCoeff * (driveRaw - ch.driveDc);
            const double drive = driveRaw - ch.driveDc;
            bool ok2 = true; // reducedOrder: ch.power is empty, nothing to solve
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, drive);
                ch.power.setSource (ch.wSrcTrem, tremVolts);
                if (modeChoice == 1) // triode: screens follow plates (previous sample's solved value)
                {
                    ch.power.setPentodeScreen (ch.penA, ch.power.voltage (ch.wPP1));
                    ch.power.setPentodeScreen (ch.penB, ch.power.voltage (ch.wPP2));
                }
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
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, drive * tremMod) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;

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

void CarrRamblerStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No dedicated amp glyph exists on the reference sheet yet (same placeholder the other amps use).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
