#include "GibsonEH150StyleAmplifierProcessor.h"
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

    /** The 6SQ7 is the octal high-mu triode (mu ~ 100) the 12AX7/ECC83 descends from; Koren's ECC83 set is the right
        curve family. */
    KorenTriode::Parameters triode6SQ7() { return {}; }

    /** The 6N7 is a twin triode with mu ~ 35, much beefier than a 12AX7: lower mu, higher perveance. Fitted to its
        published operating region (~9 mA at 250 V / -5 V) rather than copied from a datasheet set. */
    KorenTriode::Parameters triode6N7()
    {
        KorenTriode::Parameters p;
        p.mu = 35.0;
        p.kg1 = 1500.0;
        return p;
    }

    /** ONE 6L6 (the original 19 W 6L6G/GA; Koren's 6L6GC defaults are the same curve family): the Twin Reverb's pair
        set (pentode6L6Pair) with the pair-doubling undone. */
    KorenPentode::Parameters pentode6L6()
    {
        KorenPentode::Parameters p; // Koren's 6L6GC defaults
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Estimated Style-4 rails: 5U4G rectifier (sagging), small period filter caps, cathode-biased 6L6s at ~15-20 W
    // class A1 (docs/circuits/GibsonEH150.md).
    constexpr double railPlatesNominal = 360.0;
    constexpr double rectifierR = 150.0;
    constexpr double reservoir = 16.0e-6;
    constexpr double screenResistor = 1.5e3;
    constexpr double piFraction = 300.0 / 330.0;
    constexpr double preFraction = 250.0 / 330.0;
    constexpr double idlePlateCurrent = 0.030;   // per output tube
    constexpr double preampAndPiCurrent = 0.006;

    constexpr double otPrimary = 5000.0;
    constexpr double primaryHalfL = 7.0;
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.999;
    constexpr double primaryHalfResistance = 80.0;
    constexpr double secondaryResistance = 0.2;
    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // the 8 ohm tap

    // ---- reduced-order power stage: calibration data, EH150_POWERCAL in the test file ----
    // Plate rail vs. the preamp-signal drive PEAK, measured on the full reference model (Volume 1, Power Drive
    // max, 8 ohm). The 5U4G sags hard -- the amp's compressed feel is mostly this table.
    constexpr int bmSagPoints = 9;
    constexpr double bmSagDrive[bmSagPoints] = { 0.05, 0.15, 0.40, 0.90, 1.80, 3.60, 7.00, 12.0, 18.0 };
    constexpr double bmSagRail[bmSagPoints] = { 360.0, 358.0, 354.0, 346.0, 332.0, 312.0, 292.0, 278.0, 270.0 };

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

GibsonEH150StyleAmplifierProcessor::GibsonEH150StyleAmplifierProcessor()
{
    const juce::String px ("eh150_");
    auto make = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (px + id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("volume", "Volume", 0.5f);
    auto tone = make ("tone", "Tone", 0.5f);
    auto power = make ("power", "Power Drive", 1.0f);
    auto bias = make ("bias", "Bias", 0.5f);
    auto feel = make ("tube_feel", "Tube Feel", 1.0f);
    auto output = make ("output", "Output", 0.5f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        px + "speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    volumeParam = volume.get();
    toneParam = tone.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();
    outputParam = output.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("gibson_eh150", "Gibson EH-150-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (tone));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("gibson_eh150_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void GibsonEH150StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const double screenNominal = railPlatesNominal - screenResistor * (2.0 * 0.008 + preampAndPiCurrent);

    // ================================================================ supply: 5U4G -> reservoir (plates) -> 1k5 -> screens
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierR);
        c.addCapacitor (ch.sA, gnd, reservoir);
        c.addResistor (ch.sA, ch.sB, screenResistor);
        c.addCapacitor (ch.sB, gnd, 16.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -2.0 * idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -preampAndPiCurrent);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, screenNominal);
    }

    // ================================================================ preamp: 6SQ7 -> Volume -> 6SQ7 + Tone
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, screenNominal * preFraction);

        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double rk, double ck,
                          NodalCircuit::Node& plate, NodalCircuit::Node& cathode, double plateGuess, double cathodeGuess)
        {
            const auto g = c.addNode();
            plate = c.addNode();
            cathode = c.addNode();
            c.addResistor (gIn, g, stopper);
            if (leak > 0.0)
                c.addResistor (g, gnd, leak);
            c.addTriode (plate, g, cathode, triode6SQ7());
            c.addCapacitor (g, plate, cgp);
            c.addResistor (rail, plate, 250.0e3); // the high plate loads Gibson ran in this era
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
        };

        // V1 (instrument channel): 68k stopper, 1M leak, 1k5 + 10 uF.
        stage (in, 68.0e3, 1.0e6, 1.5e3, 10.0e-6, ch.pPlate1, ch.pK1, 170.0, 1.2);

        // .022 coupling -> Volume 1M.
        const auto v1Out = c.addNode();
        c.addCapacitor (ch.pPlate1, v1Out, 22.0e-9);
        const auto w = c.addNode();
        ch.rVolTop = c.addResistor (v1Out, w, 900.0e3);
        ch.rVolBot = c.addResistor (w, gnd, 100.0e3);

        // V2 (the "common" stage): 2k2 unbypassed (some local degeneration, era-typical).
        stage (w, 68.0e3, 0.0, 2.2e3, 0.0, ch.pPlate2, ch.pK2, 190.0, 1.6);

        // Tone: the single pot is a treble-cut -- cap from V2's plate through the pot's variable R to ground. Knob up
        // = brighter = larger series R (less shunt). Values estimated (docs/circuits/GibsonEH150.md).
        const auto tn = c.addNode();
        ch.rTone = c.addResistor (ch.pPlate2, tn, 20.0e3);
        c.addCapacitor (tn, gnd, 10.0e-9);
        ch.pOut = ch.pPlate2;
    }

    // ================================================================ power: 6N7 paraphase -> 2x 6L6 cathode-biased PP, no NFB
    // the FULL reference netlist only (reducedOrder replaces all of this with behavioralPowerStage())
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto rail = c.addNode(), piRail = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcPi = c.addSource (piRail, screenNominal * piFraction);
        ch.wSrcIn = c.addSource (in, 0.0);

        // .047 coupling + 470k leak into PI half A's grid.
        ch.wPiGrid = c.addNode();
        c.addCapacitor (in, ch.wPiGrid, 47.0e-9);
        c.addResistor (ch.wPiGrid, gnd, 470.0e3);

        // Paraphase: both 6N7 halves share the cathode string (2k + 25 uF); half A amplifies the input from its plate;
        // a ~1/20 divider off plate A feeds half B's grid, whose plate then carries the inverted phase. Plate loads
        // 100k each.
        ch.wPiK = c.addNode();
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        c.addTriode (ch.wPiPlateA, ch.wPiGrid, ch.wPiK, triode6N7());
        c.addResistor (piRail, ch.wPiPlateA, 100.0e3);
        c.addResistor (ch.wPiK, gnd, 2.0e3);
        c.addCapacitor (ch.wPiK, gnd, 25.0e-6);
        const auto dv = c.addNode(), gB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, dv, 47.0e-9); // the divider is AC-coupled -- grid B biases through its own leak
        c.addResistor (dv, gB, 470.0e3);          // paraphase divider
        c.addResistor (gB, gnd, 24.0e3);
        c.addCapacitor (gB, gnd, 10.0e-9);         // cleans the divider's HF edge
        c.addTriode (ch.wPiPlateB, gB, ch.wPiK, triode6N7());
        c.addResistor (piRail, ch.wPiPlateB, 100.0e3);
        c.setInitialGuess (ch.wPiPlateA, 160.0);
        c.setInitialGuess (ch.wPiPlateB, 170.0);
        c.setInitialGuess (ch.wPiK, 4.0);
        c.setInitialGuess (ch.wPiGrid, 0.0);
        c.setInitialGuess (dv, 0.0);
        c.setInitialGuess (gB, 0.0);

        // .047 couplings, 470k grid leaks to ground (cathode bias -- no negative supply), 1k5 stoppers.
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 47.0e-9);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 47.0e-9);
        c.addResistor (ch.wGridA, gnd, 470.0e3);
        c.addResistor (ch.wGridB, gnd, 470.0e3);
        const auto gAs = c.addNode(), gBs = c.addNode();
        c.addResistor (ch.wGridA, gAs, 1.5e3);
        c.addResistor (ch.wGridB, gBs, 1.5e3);
        c.setInitialGuess (ch.wGridA, 0.0);
        c.setInitialGuess (ch.wGridB, 0.0);

        // Shared 6L6 cathode string: ~500R + 50 uF (class-A-ish bias ~30 mA/tube; the Bias knob sweeps it).
        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        ch.wK = c.addNode();
        ch.penA = c.addPentode (ch.wPP1, gAs, ch.wK, pentode6L6(), screenNominal);
        ch.penB = c.addPentode (ch.wPP2, gBs, ch.wK, pentode6L6(), screenNominal);
        ch.rBias = c.addResistor (ch.wK, gnd, 500.0);
        c.addCapacitor (ch.wK, gnd, 50.0e-6);
        c.setInitialGuess (ch.wPP1, railPlatesNominal - 25.0);
        c.setInitialGuess (ch.wPP2, railPlatesNominal - 25.0);
        c.setInitialGuess (ch.wK, 30.0);

        // Winding capacitance / losses, same treatment as the other models.
        c.addCapacitor (ch.wPP1, ch.wPP2, 400.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 4.0 * otPrimary);
        c.addCapacitor (ch.wPP1, gnd, 400.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 400.0e-12);

        // Output transformer, centre tap on the plate rail, 8 ohm tap.
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double turns = std::sqrt (otPrimary / speakerNominal[matchedSpeaker]) / 2.0;
        const double lh = primaryHalfL;
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

void GibsonEH150StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    // Tone up = brighter = bigger series R under the 10 nF shunt cap.
    const double toneR = 200.0 + 250.0e3 * k.tone * k.tone;
    // Bias knob sweeps the shared cathode resistor (cathode bias: knob up = smaller R = hotter idle, matching the
    // fixed-bias amps' "up = hotter" convention).
    const double cathodeR = 750.0 - 500.0 * k.bias;
    const double rectifier = rectifierR * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.pre.setResistance (ch.rTone, toneR);
        if (! reducedOrder)
            ch.power.setResistance (ch.rBias, cathodeR);
        ch.supply.setResistance (ch.rRect, rectifier);
    }
    if (appliedSpeaker != k.speaker && ! resistiveLoadForced && ! reducedOrder)
    {
        for (auto& ch : channels)
            applySpeaker (ch, k.speaker);
        appliedSpeaker = k.speaker;
    }

    // reducedOrder: the fitted speaker response is baked into the behavioural stage at the matched tap; the
    // loudness-vs-load approximation stands in for the load sweep, same as the Bassman model.
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void GibsonEH150StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    if (resistiveLoadForced || reducedOrder)
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

void GibsonEH150StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void GibsonEH150StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void GibsonEH150StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (ch.sumCount > 0)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.sumScreen / n + preampAndPiCurrent));
    }
    ch.supply.solveSample();
    ch.sumPlate = 0.0;
    ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const double plates = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sA));
    const double screens = juce::jlimit (0.0, 600.0, ch.supply.voltage (ch.sB));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcRail, plates);
        ch.power.setPentodeScreen (ch.penA, screens);
        ch.power.setPentodeScreen (ch.penB, screens);
    }
    constexpr double decouplingTau = 9.1e3 * 20.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double a = 1.0 - std::exp (-dt / decouplingTau);
    ch.piRail += a * (screens * piFraction - ch.piRail);
    const double b = 1.0 - std::exp (-dt / (2.0 * decouplingTau));
    ch.preRail += b * (screens * preFraction - ch.preRail);
    if (! reducedOrder)
        ch.power.setSource (ch.wSrcPi, ch.piRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
}

double GibsonEH150StyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    return ch.pre.voltage (ch.pOut) - ch.outDc;
}

double GibsonEH150StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 10.0, releaseMs = 80.0; // 5U4G sag is slow both ways
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

double GibsonEH150StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::stage1Plate: return ch.pre.voltage (ch.pPlate1);
        case Probe::stage1Cathode: return ch.pre.voltage (ch.pK1);
        case Probe::stage2Plate: return ch.pre.voltage (ch.pPlate2);
        case Probe::stage2Cathode: return ch.pre.voltage (ch.pK2);
        case Probe::toneOut: return ch.pre.voltage (ch.pOut);
        case Probe::piPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiPlateB);
        case Probe::piCathode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPiK);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridA);
        case Probe::powerGridB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridB);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::powerCathode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wK);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wK);
    }
    return 0.0;
}

double GibsonEH150StyleAmplifierProcessor::plateCurrentA() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penA);
}

double GibsonEH150StyleAmplifierProcessor::plateCurrentB() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].penB);
}

void GibsonEH150StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    resistiveLoadForced = false;
    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    auto setup = [&] (juce::SmoothedValue<float>& sv, juce::AudioParameterFloat* p, double seconds)
    {
        sv.reset (newSampleRate, seconds);
        sv.setCurrentAndTargetValue (p != nullptr ? p->get() : 0.0f);
    };
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedTone, toneParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);
    setup (smoothedOutput, outputParam, 0.02);

    appliedSpeaker = -1;
    updatePots ({ volumeParam->get(), toneParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = 2.0 * idlePlateCurrent, isRun = 0.01;
        for (int pass = 0; pass < 10; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + preampAndPiCurrent));
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierR * (ipRun + isRun + preampAndPiCurrent));
            passOk = ch.supply.prepare (supplyRate);
            const double plates = ch.supply.voltage (ch.sA), screens = ch.supply.voltage (ch.sB);
            ch.piRail = screens * piFraction;
            ch.preRail = screens * preFraction;

            ch.pre.setSource (ch.pSrcRail, ch.preRail);
            passOk = ch.pre.prepare (newSampleRate) && passOk;

            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, 0.0);
                ch.power.setSource (ch.wSrcRail, plates);
                ch.power.setSource (ch.wSrcPi, ch.piRail);
                ch.power.setPentodeScreen (ch.penA, screens);
                ch.power.setPentodeScreen (ch.penB, screens);
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ipRun += 0.5 * ((ipA + ipB) - ipRun);
                isRun += 0.5 * ((isA + isB) - isRun);
            }
            else
            {
                ch.power.prepare (newSampleRate); // empty circuit in reducedOrder -- prepared so saveDynamicState() is well-defined
            }
        }
        dcOk = passOk && dcOk;
        ch.outDc = ch.pre.voltage (ch.pOut);
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void GibsonEH150StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTone.setTargetValue (toneParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float tn = smoothedTone.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, tn, pw, bi, fe, speakerChoice });
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

            // reducedOrder: ch.power is empty, nothing to solve; the behavioural stage takes the drive directly.
            const double drive = masterGain * preampOutput (ch);
            bool okPower = true;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, drive);
                okPower = ch.power.solveSample();
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            bool ok = okPre && okPower;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += okPower ? 0 : 1;
            }

            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, drive) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 60.0;
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

void GibsonEH150StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
