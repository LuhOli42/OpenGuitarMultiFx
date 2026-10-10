#include "GarnetHerzogStyleAmplifierProcessor.h"
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

    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set, same as every other amp here

    /** ONE 6V6GT: published Koren 6V6GT set (RockerverbStyleAmplifierProcessor's pair set with the pair-doubling
        undone): mu 10, Ex 1.35, Kg1 1400, Kg2 4500, Kp 48.5, Kvb 12. */
    KorenPentode::Parameters pentode6V6()
    {
        KorenPentode::Parameters p;
        p.mu = 10.0;
        p.ex = 1.35;
        p.kg1 = 1400.0;
        p.kg2 = 4500.0;
        p.kp = 48.5;
        p.kvb = 12.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Rails from Garnet's own "H-ZOG RANDY BACHMAN" drawing (el34world Garnet collection): 320+ at the output
    // transformer, 315+ past the 1k, 295+ past the 10k preamp dropper.
    constexpr double railPlatesNominal = 320.0;
    constexpr double rectifierR = 40.0;      // silicon diodes + PT winding (stiffer than a tube rectifier)
    constexpr double reservoir = 20.0e-6;    // period filter caps were small; the Herzog's sag is part of its feel
    constexpr double screenResistor = 1.0e3; // the 1K-1W between 320+ and 315+
    constexpr double idlePlateCurrent = 0.032;
    constexpr double preampCurrent = 0.003;  // two 12AX7 sections down the 10k: (315 - 295) / 10k ~ 2 mA + margin

    // Output transformer: Garnet 145A189, a Champ-type single-ended ~5k primary into the 6R/10W dummy load.
    constexpr double otPrimary = 5000.0;
    constexpr double primaryL = 12.0;
    constexpr double loadOhms = 6.0;
    constexpr double couplingSE = 0.995;
    constexpr double primaryResistance = 200.0;
    constexpr double secondaryResistance = 0.3;

    // ---- reduced-order power stage: calibration data, HZG_POWERCAL in the test file ----
    // Plate rail vs. the preamp-signal drive PEAK, measured on the full reference model (Volume 1, Level 1,
    // Power Drive max). SE tube + stiff silicon supply: ~30 V of sag across the usable range.
    constexpr int bmSagPoints = 9;
    constexpr double bmSagDrive[bmSagPoints] = { 0.02, 0.06, 0.14, 0.30, 0.60, 1.20, 2.40, 4.80, 8.00 };
    constexpr double bmSagRail[bmSagPoints] = { 320.0, 319.6, 318.5, 316.0, 311.0, 304.0, 296.0, 290.0, 286.0 };

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

GarnetHerzogStyleAmplifierProcessor::GarnetHerzogStyleAmplifierProcessor()
{
    const juce::String px ("hzg_");
    auto make = [&] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (px + id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("volume", "Volume", 0.5f);
    auto deep = make ("deep", "Deep", 0.0f);
    auto level = make ("level", "Level", 0.5f);
    auto power = make ("power", "Power Drive", 1.0f);
    auto feel = make ("tube_feel", "Tube Feel", 1.0f);
    auto output = make ("output", "Output", 0.5f);

    volumeParam = volume.get();
    deepParam = deep.get();
    levelParam = level.get();
    powerParam = power.get();
    tubeFeelParam = feel.get();
    outputParam = output.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("garnet_herzog", "Garnet Herzog-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (deep));
    group->addChild (std::move (level));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("garnet_herzog_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void GarnetHerzogStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const double screenNominal = railPlatesNominal - screenResistor * (idlePlateCurrent + preampCurrent + 0.003);
    const double preNominal = screenNominal - 10.0e3 * preampCurrent;

    // ================================================================ supply: diodes -> reservoir (320+) -> 1k -> screens (315+)
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
        c.addCapacitor (ch.sB, gnd, 20.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(preampCurrent + 0.003));
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, screenNominal);
    }

    // ================================================================ preamp: V1A -> (.022 || Deep .022) -> Volume -> V1B
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, preNominal);

        auto stage = [&] (NodalCircuit::Node gIn, double stopper, double leak, double plateR, double rk, double ck,
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
            c.addResistor (rail, plate, plateR);
            c.addResistor (cathode, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (cathode, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (cathode, cathodeGuess);
            return g;
        };

        // V1A: 68k stopper, 1M leak, 220k plate, 1k5 + 25 uF (Garnet drawing).
        stage (in, 68.0e3, 1.0e6, 220.0e3, 1.5e3, 25.0e-6, ch.pPlate1, ch.pK1, 200.0, 1.5);

        const auto d = c.addNode();
        c.addCapacitor (ch.pPlate1, d, 22.0e-9);

        // Input Volume 1M-L.
        const auto w = c.addNode();
        ch.rVolTop = c.addResistor (d, w, 900.0e3);
        ch.rVolBot = c.addResistor (w, gnd, 100.0e3);

        // V1B: 100k plate, 1k5 + 25 uF.
        const auto g2 = stage (w, 68.0e3, 0.0, 100.0e3, 1.5e3, 25.0e-6, ch.pPlate2, ch.pK2, 210.0, 1.6);

        // Deep S.P.S.T.: the drawing's second .022 is modelled as a pot-bypassing path
        // straight into V1B's grid (fuller signal at any volume) -- see docs/circuits/GarnetHerzog.md.
        ch.capDeep = c.addCapacitor (ch.pPlate1, g2, 1.0e-12);
    }

    // ================================================================ power: 6V6 single-ended -> OT -> 6R load -> Level pot + 150k
    // the FULL reference netlist only (reducedOrder replaces all of this with behavioralPowerStage())
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto rail = c.addNode(), in = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        ch.wSrcIn = c.addSource (in, 0.0);

        // .047 coupling + 220k grid leak (Garnet drawing).
        ch.wGrid = c.addNode();
        c.addCapacitor (in, ch.wGrid, 0.047e-6);
        c.addResistor (ch.wGrid, gnd, 220.0e3);
        c.setInitialGuess (ch.wGrid, 0.0);

        // 6V6: 470R 1W cathode + 25 uF bypass.
        ch.wPP = c.addNode();
        ch.wK = c.addNode();
        ch.pen = c.addPentode (ch.wPP, ch.wGrid, ch.wK, pentode6V6(), screenNominal);
        c.addResistor (ch.wK, gnd, 470.0);
        c.addCapacitor (ch.wK, gnd, 25.0e-6);
        c.setInitialGuess (ch.wPP, railPlatesNominal - 15.0);
        c.setInitialGuess (ch.wK, 16.0);

        // .003 uF / 1600 V across the primary (on the drawing).
        const auto a1 = c.addNode();
        c.addCapacitor (ch.wPP, a1, 3.0e-9);
        c.addResistor (rail, a1, primaryResistance);

        // Single-ended output transformer: 2 windings, primary carries the DC idle current.
        const auto sw = c.addNode();
        const double turns = std::sqrt (otPrimary / loadOhms);
        const double ls = primaryL / (turns * turns);
        const double m = -couplingSE * std::sqrt (primaryL * ls);
        c.addCoupledInductors ({ { a1, ch.wPP }, { sw, gnd } },
                               { primaryL, m,
                                 m,        ls });
        ch.wLoad = c.addNode();
        c.addResistor (sw, ch.wLoad, secondaryResistance);
        c.addResistor (ch.wLoad, gnd, loadOhms); // the 6R 10W dummy load
        c.setInitialGuess (a1, railPlatesNominal);

        // Output Volume 1M-L across the load, wiper -> 150k -> out, 1M leak (the next amp's input).
        const auto vw = c.addNode();
        ch.rLevelTop = c.addResistor (ch.wLoad, vw, 900.0e3);
        ch.rLevelBot = c.addResistor (vw, gnd, 100.0e3);
        ch.wOut = c.addNode();
        c.addResistor (vw, ch.wOut, 150.0e3);
        c.addResistor (ch.wOut, gnd, 1.0e6);
    }
}

void GarnetHerzogStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    const double levelBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.level));
    const double deepCap = 1.0e-12 + 22.0e-9 * k.deep;
    const double rectifier = rectifierR * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        ch.pre.setCapacitance (ch.capDeep, deepCap);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rLevelTop, juce::jmax (1.0, 1.0e6 - levelBottom));
            ch.power.setResistance (ch.rLevelBot, levelBottom);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
    }
}

void GarnetHerzogStyleAmplifierProcessor::recover (Channel& ch) const
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

void GarnetHerzogStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (ch.sumCount > 0)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.3, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.1, ch.sumScreen / n + preampCurrent));
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
        ch.power.setPentodeScreen (ch.pen, screens);
    }
    // the 10k / ~30 uF preamp dropper (315+ -> 295+)
    constexpr double decouplingTau = 10.0e3 * 30.0e-6;
    const double dt = (double) supplyInterval / juce::jmax (1.0, sampleRate);
    const double b = 1.0 - std::exp (-dt / decouplingTau);
    ch.preRail += b * ((screens - 10.0e3 * preampCurrent) - ch.preRail);
    ch.pre.setSource (ch.pSrcRail, ch.preRail);
}

double GarnetHerzogStyleAmplifierProcessor::preampOutput (const Channel& ch) const noexcept
{
    return ch.pre.voltage (ch.pPlate2) - ch.outDc;
}

double GarnetHerzogStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept
{
    constexpr double attackMs = 6.0, releaseMs = 60.0; // silicon diodes + 20 uF: quicker than a tube-rectified amp
    const double absDrive = std::abs (driveVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    // reducedOrder folds the power-only controls into the fit (same recipe as the
    // big-iron amps): tube feel scales sag depth, bias shifts the knee's operating
    // point, presence scales the HF shelf, focus tightens the low-end sag — all
    // centred on the shipped defaults so noon response is unchanged.
    const double feel = juce::jlimit (0.0, 1.0, (double) lastKnobs.tubeFeel);
    ch.bmRail = bmSagRail[0] - feel * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

    const double drive = driveVoltage;
    const double k = ch.bmRail * bmYmax / bmGain0;
    const auto knee = [&] (double x) { return bmYmax * x / std::pow (1.0 + std::pow (x, bmKneeN), 1.0 / bmKneeN); };
    const double u = std::abs (drive) / juce::jmax (1.0e-9, k);
    const double raw = std::copysign (knee (u) * ch.bmRail, drive);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    const double hfGain = bmShelfHfGain;
    ch.bmOutput = ch.bmToneState + hfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double GarnetHerzogStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::stage1Plate: return ch.pre.voltage (ch.pPlate1);
        case Probe::stage1Cathode: return ch.pre.voltage (ch.pK1);
        case Probe::stage2Plate: return ch.pre.voltage (ch.pPlate2);
        case Probe::stage2Cathode: return ch.pre.voltage (ch.pK2);
        case Probe::powerGrid: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGrid);
        case Probe::powerPlate: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP);
        case Probe::powerCathode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wK);
        case Probe::load: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wLoad);
        case Probe::output: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return ch.power.voltage (ch.wK);
    }
    return 0.0;
}

double GarnetHerzogStyleAmplifierProcessor::plateCurrent() const noexcept
{
    return reducedOrder ? 0.0 : channels[0].power.pentodePlateCurrent (channels[0].pen);
}

void GarnetHerzogStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

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
    setup (smoothedDeep, deepParam, 0.05);
    setup (smoothedLevel, levelParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedFeel, tubeFeelParam, 0.05);
    setup (smoothedOutput, outputParam, 0.02);

    updatePots ({ volumeParam->get(), deepParam->get(), levelParam->get(), powerParam->get(), tubeFeelParam->get() });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = idlePlateCurrent, isRun = 0.004;
        for (int pass = 0; pass < 10; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -(isRun + preampCurrent));
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierR * (ipRun + isRun + preampCurrent));
            passOk = ch.supply.prepare (supplyRate);
            const double plates = ch.supply.voltage (ch.sA), screens = ch.supply.voltage (ch.sB);
            ch.preRail = screens - 10.0e3 * preampCurrent;

            ch.pre.setSource (ch.pSrcRail, ch.preRail);
            passOk = ch.pre.prepare (newSampleRate) && passOk;

            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcIn, 0.0);
                ch.power.setSource (ch.wSrcRail, plates);
                ch.power.setPentodeScreen (ch.pen, screens);
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                double ipA = 0.0, isA = 0.0;
                ch.power.pentodeCurrents (ch.pen, ipA, isA);
                ipRun += 0.5 * (ipA - ipRun);
                isRun += 0.5 * (isA - isRun);
            }
            else
            {
                ch.power.prepare (newSampleRate); // empty circuit in reducedOrder -- prepared so saveDynamicState() is well-defined
            }
        }
        dcOk = passOk && dcOk;
        ch.outDc = ch.pre.voltage (ch.pPlate2);
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

void GarnetHerzogStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedDeep.setTargetValue (deepParam->get());
    smoothedLevel.setTargetValue (levelParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    smoothedOutput.setTargetValue (outputParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float dp = smoothedDeep.getNextValue();
        const float lv = smoothedLevel.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float fe = smoothedFeel.getNextValue();
        const float ou = smoothedOutput.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, dp, lv, pw, fe });
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
                double ipA, isA;
                ch.power.pentodeCurrents (ch.pen, ipA, isA);
                ch.sumPlate += ipA;
                ch.sumScreen += isA;
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

            // reducedOrder: the behavioural stage returns wOut-equivalent volts at Level = 1; the real pot's
            // taper (applied to a load of 1M into 150k -> 1M) is applied on top, same as the netlist does.
            const double levelTap = reducedOrder ? pots::audio ((double) lv) * (1.0e6 / (1.15e6)) : 1.0;
            const double outVolts = reducedOrder ? behavioralPowerStage (ch, drive) * levelTap : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 40.0;
            const bool sane = std::isfinite (outVolts) && std::abs (outVolts) < saneLimit;
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
                out = outVolts * (reducedOrder ? outputScale : fullOutputScale) * outGain;
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

void GarnetHerzogStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
