#include "AmpegB15StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/AmpegB15.md, "What the B-15N actually is") ----
    constexpr double railPlatesNominal = 395.0;   // OT centre tap + screens; 5AR4-fed, marked ~390-400 V
    constexpr double railPreampNominal = 330.0;   // preamp + paraphase driver plates, dropped via 6.8k
    constexpr double rectifierResistance = 260.0; // 5AR4 into a small PT: the B-15 sags far more than the SVT
    constexpr double screenResistor = 470.0;      // shared screen stopper per bank
    constexpr double idlePlateCurrent = 0.070;    // two 6L6GCs at ~35 mA
    constexpr double idleScreenCurrent = 0.008;
    constexpr double idleDriverCurrent = 0.0015;  // third 6SL7, two halves at ~0.7 mA
    constexpr double idlePreampCurrent = 0.0014;  // the marked ~330 V rail is the LOADED rail
    constexpr double biasSupplyVolts = -45.0;     // fixed bias, marked point "K" on the schematic

    // ---- tubes ----
    KorenTriode::Parameters triode6SL7()
    {
        // No published Koren 6SL7 set is vendored; estimated from the type's curves (mu ~70, high rp).
        KorenTriode::Parameters p;
        p.mu = 70.0;
        p.ex = 1.4;
        p.kg1 = 1200.0;
        p.kp = 400.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate Miller capacitance

    // Output transformer: ~6.6k plate-to-plate for the 6L6GC pair at ~395 V, 8 ohm secondary
    // (estimate -- see the doc's "Honest simplifications").
    constexpr double primaryHalfInductance = 8.0;         // H per half
    constexpr double halfToSecondaryTurns = 14.4;         // sqrt(1650/8): one half-primary is 1.65k, secondary 8
    constexpr double couplingHalves = 0.9993;
    constexpr double couplingSecondary = 0.9985;
    constexpr double primaryHalfResistance = 40.0;
    constexpr double secondaryResistance = 0.2;
    constexpr double feedbackResistor = 100.0e3;          // the B-15N's global NFB into the driver cathode

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };

    constexpr double powerTheta = 0.9;
}

AmpegB15StyleAmplifierProcessor::AmpegB15StyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "0 dB" : "-15 dB");
        }));
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto ultraLo = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_ultra_lo", "Ultra Lo", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto ultraHi = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_ultra_hi", "Ultra Hi", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto master = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_master", "Master", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto power = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_power", "Power Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto bias = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto feel = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_tube_feel", "Tube Feel", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    volumeParam = volume.get();
    ultraLoParam = ultraLo.get();
    ultraHiParam = ultraHi.get();
    bassParam = bass.get();
    trebleParam = treble.get();
    masterParam = master.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ampegb15", "B-15-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (volume));
    group->addChild (std::move (ultraLo));
    group->addChild (std::move (ultraHi));
    group->addChild (std::move (bass));
    group->addChild (std::move (treble));
    group->addChild (std::move (master));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ampegb15_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void AmpegB15StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();

        const double dcDrop = rectifierResistance * (idlePlateCurrent + idleScreenCurrent);
        ch.srcVoc = c.addSource (vo, railPlatesNominal + dcDrop);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 40.0e-6);            // the B-15's modest filter can

        c.addResistor (ch.sA, ch.sB, 6.8e3);             // dropper to the preamp/driver rail
        c.addCapacitor (ch.sB, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -(idlePlateCurrent + idleScreenCurrent));
        ch.iB = c.addCurrentSource (ch.sB, -(idlePreampCurrent + idleDriverCurrent));
        c.setInitialGuess (vo, railPlatesNominal + dcDrop);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPreampNominal);
    }

    // ================================================================ preamp
    // Two 6SL7 stages around the passive Ampeg tone network, then the Volume pot.
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railPreampNominal);
        ch.pSrcIn = c.addSource (in, 0.0);

        // V1a -- first gain stage (470k plate, 5.6k cathode bypassed).
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, g1, 56.0e3);                  // grid stopper
        c.addResistor (g1, gnd, 1.0e6);                  // grid leak
        c.addTriode (p1, g1, k1, triode6SL7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vcc, p1, 470.0e3);
        c.addResistor (k1, gnd, 5.6e3);
        c.addCapacitor (k1, gnd, 25.0e-6);
        c.setInitialGuess (g1, 0.0);
        c.setInitialGuess (k1, 1.8);
        c.setInitialGuess (p1, 180.0);

        // Ultra-Lo scoop divider into the tone network (same switch idea as the SVT's).
        const auto na = c.addNode(), u = c.addNode(), ti = c.addNode();
        c.addCapacitor (p1, na, 0.05e-6);
        c.addResistor (na, gnd, 1.0e6);
        c.addResistor (na, ti, 100.0e3);
        ch.rUltraLoA = c.addResistor (na, u, 1.0e9);     // open until engaged: 330k when on
        c.addResistor (u, ti, 220.0e3);
        c.addCapacitor (u, gnd, 0.0047e-6);
        c.addResistor (u, gnd, 3.3e6);
        c.setInitialGuess (na, 0.0);

        // Passive Ampeg bass/treble network (same family as the SVT's Baxandall-style section).
        const auto t1 = c.addNode(), w1 = c.addNode(), nb = c.addNode(),
                   ns = c.addNode(), t2 = c.addNode(), wt = c.addNode(), tb = c.addNode();
        c.addResistor (ti, t1, 220.0e3);
        ch.rBassTop = c.addResistor (t1, w1, 500.0e3);   // Bass 1M audio
        ch.rBassBot = c.addResistor (w1, nb, 500.0e3);
        c.addResistor (nb, gnd, 22.0e3);
        c.addCapacitor (w1, ns, 0.001e-6);
        ch.capUltraHi = c.addCapacitor (w1, ns, 1.0e-12); // Ultra-Hi adds .01 uF across the bass leg
        c.addResistor (ns, t2, 100.0e3);
        c.addCapacitor (ti, t2, 470.0e-12);              // treble feed
        ch.rTrebleTop = c.addResistor (t2, wt, 500.0e3); // Treble 1M audio
        ch.rTrebleBot = c.addResistor (wt, tb, 500.0e3);
        c.addCapacitor (tb, gnd, 0.0047e-6);
        ch.pTone = wt;
        c.setInitialGuess (ti, 0.0);

        // V1b -- recovery stage after the lossy network (220k plate, 2.2k cathode).
        const auto g2 = c.addNode(), p2 = c.addNode(), k2 = c.addNode();
        c.addCapacitor (wt, g2, 0.05e-6);
        c.addResistor (g2, gnd, 1.0e6);
        c.addTriode (p2, g2, k2, triode6SL7());
        c.addCapacitor (g2, p2, cgp);
        c.addResistor (vcc, p2, 220.0e3);
        c.addResistor (k2, gnd, 2.2e3);
        c.addCapacitor (k2, gnd, 25.0e-6);
        c.setInitialGuess (k2, 2.0);
        c.setInitialGuess (p2, 200.0);
        ch.pPlate2 = p2;

        // Volume (front panel, 1M audio) -> Master (synthetic) -> output.
        const auto vw = c.addNode(), mw = c.addNode(), po = c.addNode();
        c.addCapacitor (p2, vw, 0.05e-6);
        ch.rVolumeTop = c.addResistor (vw, mw, 500.0e3);
        ch.rVolumeBot = c.addResistor (mw, gnd, 500.0e3);
        ch.rMasterTop = c.addResistor (mw, po, 25.0e3);
        ch.rMasterBot = c.addResistor (po, gnd, 25.0e3);
        c.setInitialGuess (vw, 0.0);
        ch.pOut = po;
        ch.pPlate1 = p1;
    }

    // ================================================================ power amp: floating-paraphase
    // driver (third 6SL7), two 6L6GCs on fixed bias, output transformer, speaker, global NFB.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cin = c.addNode();
        const auto vdr = c.addNode(), ct = c.addNode(), neg = c.addNode();
        ch.wSrcPre = c.addSource (cin, 0.0);
        ch.wTone = cin;
        c.addResistor (cin, gnd, 1.0e6);

        // FULL reference power stage only -- reducedOrder replaces everything below with
        // behavioralPowerStage(), fitted to this same circuit (see the header + docs/circuits/AmpegB15.md).
        if (! reducedOrder)
        {
        ch.wSrcVdr = c.addSource (vdr, railPreampNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcNeg = c.addSource (neg, biasSupplyVolts);

        // V3a -- the paraphase driver half. Its plate feeds output grid A directly and a ~1/45 tap
        // feeds V3b's grid, so V3b's plate delivers the inverted copy.
        const auto gd = c.addNode(), pd = c.addNode(), kd = c.addNode(), fb = c.addNode();
        c.addCapacitor (cin, gd, 0.05e-6);
        c.addResistor (gd, gnd, 1.0e6);                  // driver grid leak
        c.addTriode (pd, gd, kd, triode6SL7());
        c.addCapacitor (gd, pd, cgp);
        c.addResistor (vdr, pd, 220.0e3);
        c.addResistor (kd, fb, 330.0);                   // un-bypassed top resistor: NFB sums in here
        c.addResistor (fb, gnd, 1.8e3);
        c.setInitialGuess (pd, 200.0);
        c.setInitialGuess (kd, 2.0);
        c.setInitialGuess (fb, 1.6);
        ch.wDrvPlate = pd;

        // The paraphase tap: plate -> 470k -> tap -> 10.4k -> gnd (~1/45 of V3a's swing into V3b's
        // grid, matching the ~45x gain the second half needs for equal output-tube drive).
        const auto tt = c.addNode(), gi = c.addNode(), pi = c.addNode(), ki = c.addNode();
        c.addResistor (pd, tt, 470.0e3);
        c.addResistor (tt, gnd, 10.4e3);
        c.addCapacitor (tt, gi, 0.05e-6);                // the tap sits ~+4 V DC; keep it off the grid
        c.addResistor (gi, gnd, 1.0e6);
        c.addTriode (pi, gi, ki, triode6SL7());
        c.addCapacitor (gi, pi, cgp);
        c.addResistor (vdr, pi, 220.0e3);
        c.addResistor (ki, gnd, 2.2e3);
        c.setInitialGuess (pi, 200.0);
        c.setInitialGuess (ki, 2.0);
        ch.wInvPlate = pi;

        // Output stage: each side's grid returns to the -50 V fixed-bias rail through 220k.
        const auto n1 = c.addNode(), n2 = c.addNode(), g3 = c.addNode(), g4 = c.addNode(),
                   pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addCapacitor (pd, n1, 0.05e-6);                // driver -> output grid A
        c.addResistor (n1, g3, 47.0e3);                  // grid stopper
        c.addCapacitor (pi, n2, 0.05e-6);                // inverter -> output grid B
        c.addResistor (n2, g4, 47.0e3);
        ch.rBiasTapA = c.addResistor (g3, neg, 220.0e3); // grid leaks to the bias rail
        ch.rBiasTapB = c.addResistor (g4, neg, 220.0e3);
        c.addCapacitor (g3, gnd, 220.0e-12);             // 6L6 input capacitance (Newton aid, see SVT)
        c.addCapacitor (g4, gnd, 220.0e-12);
        ch.wPowerGridA = g3;
        ch.penA = c.addPentode (pp1, g3, gnd, {}, railPlatesNominal); // Koren 6L6GC defaults, one per side
        ch.penB = c.addPentode (pp2, g4, gnd, {}, railPlatesNominal);

        c.addCapacitor (pp1, pp2, 250.0e-12);
        c.addResistor (pp1, pp2, 100.0e3);
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        // Secondary sense chosen so feedback into the driver cathode is NEGATIVE: input up drives
        // pp1 DOWN, so the secondary must swing UP into the cathode node to oppose it -- the opposite
        // sign convention from the SVT (whose NFB lands on a grid, not a cathode). The wrong sign
        // motorboats and shows up as mass solve failures + rest-state recoveries.
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12,  -mps,
                                 m12, lh,   mps,
                                 -mps, mps,  ls });
        ch.wOut = c.addNode();
        c.addResistor (sw, ch.wOut, secondaryResistance);
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        {
            const auto sm = speakerModel (8.0);
            const auto na2 = c.addNode(), nbb2 = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na2, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na2, nbb2 } }, { sm.le });
            ch.rSpkRp = c.addResistor (nbb2, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb2, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb2, gnd, sm.cp);
        }

        // Negative feedback: speaker terminal -> R -> driver cathode node fb (the small un-bypassed
        // resistor in V3a's cathode; the feedback cap blocks the cathode's DC).
        ch.rFeedback = c.addResistor (ch.wOut, fb, feedbackResistor);
        c.addCapacitor (ch.wOut, fb, 0.1e-6);
        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (g3, biasSupplyVolts);
        c.setInitialGuess (g4, biasSupplyVolts);
        }
    }
}

double AmpegB15StyleAmplifierProcessor::sagRail (double envelope) const noexcept
{
    // 5AR4-style sag: rail droops up to ~12 % into full drive (fitted to the reference netlist).
    return railPlatesNominal * juce::jlimit (0.80, 1.0, 1.0 - 0.012 * envelope);
}

double AmpegB15StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) noexcept
{
    const double attackCoeff = 1.0 - std::exp (-1.0 / (0.008 * sampleRate));
    const double releaseCoeff = 1.0 - std::exp (-1.0 / (0.045 * sampleRate));
    const double absDrive = std::abs (toneVoltage);
    ch.bmEnvelope += (absDrive > ch.bmEnvelope ? attackCoeff : releaseCoeff) * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRail (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double over = toneVoltage / bmGridClampV;
    const double clamped = toneVoltage / std::sqrt (1.0 + over * over);
    const auto knee = [k, rail = ch.bmRail] (double x) { return std::tanh (x / juce::jmax (1.0e-9, k)) * rail * bmYmax; };
    const double shift = bmAsym * k;
    const double raw = knee (clamped + shift) - knee (shift);

    const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / sampleRate);
    ch.bmDcState += dcCoeff * (raw - ch.bmDcState);
    const double rawAc = raw - ch.bmDcState;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / sampleRate);
    ch.bmToneState += shelfCoeff * (rawAc - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (rawAc - ch.bmToneState);
    return ch.bmOutput;
}

void AmpegB15StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 2, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void AmpegB15StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volumeBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    const double bassBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.treble));
    const double masterBot = juce::jmax (1.0, 50.0e3 * k.master);

    const double ulA = k.ultraLo > 0.5 ? 330.0e3 : 1.0e9;
    const double uhC = k.ultraHi > 0.5 ? 0.01e-6 : 1.0e-12;

    // Bias knob: the fixed-bias rail itself, -52 (cold) .. -38 V (hot), noon -45.
    const double biasV = -(52.0 - 14.0 * k.bias);

    // Tube Feel: the supply's series resistance (sag) and the feedback amount. 1 = the real amp.
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride
                                                  : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolumeTop, juce::jmax (1.0, 1.0e6 - volumeBot));
        ch.pre.setResistance (ch.rVolumeBot, volumeBot);
        ch.pre.setResistance (ch.rBassTop, juce::jmax (1.0, 1.0e6 - bassBot));
        ch.pre.setResistance (ch.rBassBot, bassBot);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 1.0e6 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rMasterTop, juce::jmax (1.0, 50.0e3 - masterBot));
        ch.pre.setResistance (ch.rMasterBot, masterBot);
        ch.pre.setResistance (ch.rUltraLoA, ulA);
        ch.pre.setCapacitance (ch.capUltraHi, uhC);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcNeg, biasV);
            ch.power.setResistance (ch.rFeedback, feedbackR);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! reducedOrder && ! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    // Heavier loads take fewer volts; compensate so the impedance choices change feel, not loudness.
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 8.0, -0.8);
}

void AmpegB15StyleAmplifierProcessor::recover (Channel& ch) const
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
    ch.vScreen = ch.supply.voltage (ch.sA);
}

void AmpegB15StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.0, (ch.sumPlate + ch.sumScreen) / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 600.0));
        ch.power.setSource (ch.wSrcVdr, rail (ch.sB, 450.0));
    }
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sB, 450.0));
    ch.vScreen = rail (ch.sA, 600.0);
}

double AmpegB15StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::firstPlate: return ch.pre.voltage (ch.pPlate1);
        case Probe::toneStackOut: return ch.pre.voltage (ch.pTone);
        case Probe::secondPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::driverPlate: return reducedOrder ? 0.0 : ch.power.voltage (ch.wDrvPlate);
        case Probe::inverterPlate: return reducedOrder ? 0.0 : ch.power.voltage (ch.wInvPlate);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPowerGridA);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double AmpegB15StyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void AmpegB15StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
{
    for (auto& ch : channels)
    {
        ch.power.setResistance (ch.rSpkRe, ohms);
        ch.power.setResistance (ch.rSpkRp, 1.0e-3);
        ch.power.setInductorInverse (ch.grpSpkLe, { 1.0e6 });
        ch.power.setInductorInverse (ch.grpSpkLp, { 1.0 });
        ch.power.setCapacitance (ch.capSpkCp, 1.0e-9);
    }
    appliedSpeaker = -2;
    resistiveLoadForced = true;
}

void AmpegB15StyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double AmpegB15StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

void AmpegB15StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bassParam->get());
    smoothedTreble.reset (newSampleRate, 0.02);
    smoothedTreble.setCurrentAndTargetValue (trebleParam->get());
    smoothedMaster.reset (newSampleRate, 0.02);
    smoothedMaster.setCurrentAndTargetValue (masterParam->get());
    smoothedOutput.reset (newSampleRate, 0.02);
    smoothedOutput.setCurrentAndTargetValue (outputParam->get());
    smoothedPower.reset (newSampleRate, 0.02);
    smoothedPower.setCurrentAndTargetValue (powerParam->get());
    smoothedBias.reset (newSampleRate, 0.05);
    smoothedBias.setCurrentAndTargetValue (biasParam->get());
    smoothedFeel.reset (newSampleRate, 0.05);
    smoothedFeel.setCurrentAndTargetValue (tubeFeelParam->get());

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + idleDriverCurrent + idlePreampCurrent;
    appliedSpeaker = 1;   // the circuits are built with the 8 ohm speaker
    updatePots ({ 1.0, (double) volumeParam->get(), (double) ultraLoParam->get(),
                  (double) ultraHiParam->get(), (double) bassParam->get(), (double) trebleParam->get(),
                  (double) masterParam->get(), (double) powerParam->get(), (double) biasParam->get(),
                  (double) tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sB));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;

        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sB));
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
        }
        ch.vScreen = ch.supply.voltage (ch.sA);
        if (! reducedOrder)
        {
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.0);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.0);
        }
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.power.solveSample();

        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        if (! reducedOrder)
        {
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
        }
        else
        {
            ipA = ipB = idlePlateCurrent * 0.5;
            isA = isB = idleScreenCurrent * 0.5;
        }
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB + isA + isB));
        idleSupplyCurrent = ipA + ipB + isA + isB + idleDriverCurrent + idlePreampCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        ch.screenDropA = screenResistor * isA;
        ch.screenDropB = screenResistor * isB;
        ch.vScreen = ch.supply.voltage (ch.sA);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sB));
        }
        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sB));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    channelsSynced = true;
    channel1Stale = false;
    identicalRun = 0;
}

void AmpegB15StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();

    const bool dualMono = numChannels == 2 && blockIsDualMono (buffer);
    bool useShortcut = false;
    if (numChannels == 2)
    {
        identicalRun = dualMono ? identicalRun + numSamples : 0;
        if (! channelsSynced && identicalRun >= (long long) (10.0 * sampleRate))
        {
            channels[1] = channels[0];
            channelsSynced = true;
            channel1Stale = false;
        }
        useShortcut = dualMono && channelsSynced;
        if (! useShortcut && channel1Stale)
        {
            channels[1] = channels[0];
            channel1Stale = false;
        }
        if (! dualMono)
            channelsSynced = false;
    }
    const int solveChannels = useShortcut ? 1 : numChannels;
    if (useShortcut)
        channel1Stale = true;

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const double inputGain = juce::roundToInt (inputParam->get()) == 0 ? 1.0 : 0.1778; // 0 dB / -15 dB jacks
    const double ultraLoOn = ultraLoParam->get() > 0.5f ? 1.0 : 0.0;
    const double ultraHiOn = ultraHiParam->get() > 0.5f ? 1.0 : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ma = smoothedMaster.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ inputGain, (double) vo, ultraLoOn, ultraHiOn, (double) ba, (double) tr,
                          (double) ma, (double) pw, (double) bi, (double) fe, speakerChoice });
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputGain * inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            ch.power.setSource (ch.wSrcPre, masterGain * ch.pre.voltage (ch.pOut));
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

            double ipA = idlePlateCurrent * 0.5, ipB = ipA, isA = idleScreenCurrent * 0.5, isB = isA;
            if (! reducedOrder)
            {
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
            }
            ch.screenDropA += 0.3 * (screenResistor * isA - ch.screenDropA);
            ch.screenDropB += 0.3 * (screenResistor * isB - ch.screenDropB);
            ch.sumPlate += ipA + ipB;
            ch.sumScreen += isA + isB;
            ++ch.sumCount;

            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone))
                                                     : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 250.0;
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
                if (! ok)
                    ++failureCount;
            }
        }
    }

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void AmpegB15StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
