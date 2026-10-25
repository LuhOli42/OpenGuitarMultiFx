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

    // ---- supply, from the factory B-15-N drawing (rev C, part 591722; docs/circuits/AmpegB15.md) ----
    // 5AR4 -> C17 30 uF (OT centre tap, marked 450 V) -> R39 1k -> 40 uF (both screens, 445 V) -> two 22k / 40 uF
    // branches: the V3 driver rail and the V1/V2 preamp rail. Fixed bias from the HT winding through 100k, a diode
    // and 47k / 10k / 10 uF filtering (-50 V).
    constexpr double railPlatesNominal = 450.0;
    constexpr double rectifierResistance = 260.0; // 5AR4 + PT winding (estimated -- not on the drawing)
    constexpr double screenDropR = 1.0e3;         // R39
    constexpr double branchR = 22.0e3;            // R37 / R38
    constexpr double idlePlateCurrent = 0.070;    // two 6L6GCs (initial guess; prepare() measures)
    constexpr double idleScreenCurrent = 0.004;
    constexpr double idleDriverCurrent = 0.0027;  // V3, both halves (measured on the netlist at idle)
    constexpr double idlePreampCurrent = 0.0027;  // V1 + V2, both channels (measured on the netlist at idle)
    constexpr double biasSupplyVolts = -50.0;     // marked -50 V on the drawing

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

    // Output transformer (OT-214): ~6.6k plate-to-plate for the 6L6GC pair, 8 ohm secondary. The drawing gives
    // only the part number -- an estimate, see the doc's "Honest simplifications".
    constexpr double primaryHalfInductance = 8.0;         // H per half
    constexpr double halfToSecondaryTurns = 14.4;         // sqrt(1650/8): one half-primary is 1.65k, secondary 8
    constexpr double couplingHalves = 0.9993;
    constexpr double couplingSecondary = 0.9985;
    constexpr double primaryHalfResistance = 40.0;
    constexpr double secondaryResistance = 0.2;
    constexpr double feedbackResistor = 10.0e3;           // R40: speaker -> V3a cathode

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };

    constexpr double powerTheta = 0.9;
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::bass810, &tubeamp::cabs::tweed410, &tubeamp::cabs::greenback412 };
}

AmpegB15StyleAmplifierProcessor::AmpegB15StyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "ampegb15_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Normal" : "Bright"); // the two jacks per channel
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
        "ampegb15", "B-15", "|", std::move (input));
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
    auto cab = tubeamp::makeCabParam ("ampegb15_cab", "1x15 B15", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
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
        ch.sS = c.addNode();
        ch.sD = c.addNode();
        ch.sB = c.addNode();

        const double total = idlePlateCurrent + idleScreenCurrent + idleDriverCurrent + idlePreampCurrent;
        ch.srcVoc = c.addSource (vo, railPlatesNominal + rectifierResistance * total);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 30.0e-6);            // C17
        c.addResistor (ch.sA, gnd, 56.0e3);              // R35 bleeder
        c.addResistor (ch.sA, ch.sS, screenDropR);       // R39
        c.addCapacitor (ch.sS, gnd, 40.0e-6);
        c.addResistor (ch.sS, ch.sD, branchR);           // R37 -> V3
        c.addCapacitor (ch.sD, gnd, 40.0e-6);
        c.addResistor (ch.sS, ch.sB, branchR);           // R38 -> V1 / V2
        c.addCapacitor (ch.sB, gnd, 40.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -idlePlateCurrent);
        ch.iS = c.addCurrentSource (ch.sS, -idleScreenCurrent);
        c.addCurrentSource (ch.sD, -idleDriverCurrent);
        c.addCurrentSource (ch.sB, -idlePreampCurrent);
        c.setInitialGuess (vo, railPlatesNominal + rectifierResistance * total);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sS, railPlatesNominal - 5.0);
        c.setInitialGuess (ch.sD, railPlatesNominal - 55.0);
        c.setInitialGuess (ch.sB, railPlatesNominal - 55.0);
    }

    // ================================================================ preamp (channel 1)
    // V1a -> Ultra-Lo shunt -> Volume (+ Ultra-Hi) -> V1b -> the passive bass/treble network -> R18 270k mixer.
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railPlatesNominal - 55.0);
        ch.pSrcIn = c.addSource (in, 0.0);

        // Jacks: NORMAL through R2 47k; BRIGHT through R1 100k || C1 .005, with the unused NORMAL jack's shorting
        // contact then putting its 47k from grid to ground. Grid leak R5 5.6M.
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        ch.rInSeries = c.addResistor (in, g1, 47.0e3);
        ch.capBright = c.addCapacitor (in, g1, 1.0e-12);
        ch.rInShunt = c.addResistor (g1, gnd, 1.0e12);
        c.addResistor (g1, gnd, 5.6e6);
        c.addTriode (p1, g1, k1, triode6SL7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vcc, p1, 470.0e3);                // R7
        c.addResistor (k1, gnd, 5.6e3);                  // R8, unbypassed
        c.setInitialGuess (g1, 0.0);
        c.setInitialGuess (k1, 2.2);
        c.setInitialGuess (p1, 165.0);

        // C3 .1 -> node a. Ultra-Lo: C4 .02 + R9 39k + R10 1M to ground; the rocker grounds the R9/R10 junction.
        // a -> C19 .01 -> Volume VR1 1M LINEAR; Ultra-Hi puts C9 500 pF from the Volume top to its wiper.
        const auto na = c.addNode(), nb = c.addNode(), nc = c.addNode(), v = c.addNode(), w = c.addNode();
        c.addCapacitor (p1, na, 0.1e-6);
        c.addCapacitor (na, nb, 0.02e-6);
        c.addResistor (nb, nc, 39.0e3);
        ch.rUltraLo = c.addResistor (nc, gnd, 1.0e6);
        c.addCapacitor (na, v, 0.01e-6);
        ch.rVolumeTop = c.addResistor (v, w, 500.0e3);
        ch.rVolumeBot = c.addResistor (w, gnd, 500.0e3);
        ch.capUltraHi = c.addCapacitor (v, w, 1.0e-12);
        c.setInitialGuess (na, 0.0);

        // V1b: R15 220k, R16 2.2k unbypassed.
        const auto p2 = c.addNode(), k2 = c.addNode();
        c.addTriode (p2, w, k2, triode6SL7());
        c.addCapacitor (w, p2, cgp);
        c.addResistor (vcc, p2, 220.0e3);
        c.addResistor (k2, gnd, 2.2e3);
        c.setInitialGuess (k2, 2.0);
        c.setInitialGuess (p2, 175.0);
        ch.pPlate2 = p2;

        // C10 .1 -> the passive network (P.E.C. 250702): 220k into the Bass pot (1M log) with .001 top-to-wiper
        // and .01 bottom-to-wiper, 22k to ground; .00047 into the Treble pot (1M log), .0047 to ground; R17 120k from
        // the Bass wiper to the Treble wiper, which is the output.
        const auto t = c.addNode(), bt = c.addNode(), bw = c.addNode(), bb = c.addNode(), tt = c.addNode(),
                   x = c.addNode(), tb = c.addNode();
        c.addCapacitor (p2, t, 0.1e-6);
        c.addResistor (t, bt, 220.0e3);
        ch.rBassTop = c.addResistor (bt, bw, 500.0e3);
        ch.rBassBot = c.addResistor (bw, bb, 500.0e3);
        c.addCapacitor (bt, bw, 0.001e-6);
        c.addCapacitor (bb, bw, 0.01e-6);
        c.addResistor (bb, gnd, 22.0e3);
        c.addCapacitor (t, tt, 470.0e-12);
        ch.rTrebleTop = c.addResistor (tt, x, 500.0e3);
        ch.rTrebleBot = c.addResistor (x, tb, 500.0e3);
        c.addCapacitor (tb, gnd, 0.0047e-6);
        c.addResistor (bw, x, 120.0e3);
        ch.pTone = x;
        c.setInitialGuess (t, 0.0);

        // R18 270k into V3a's grid, summed with channel 2's R22 270k -- whose far end (the idle channel's tone
        // network) is ~120k to AC ground (estimated).
        ch.pOut = c.addNode();
        c.addResistor (x, ch.pOut, 270.0e3);
        c.addResistor (ch.pOut, gnd, 390.0e3);
        ch.pPlate1 = p1;
    }

    // ================================================================ power amp: V3 floating paraphase, two 6L6GCs
    // on fixed bias, output transformer, speaker, global NFB into V3a's cathode.
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
        ch.wSrcVdr = c.addSource (vdr, railPlatesNominal - 55.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcNeg = c.addSource (neg, biasSupplyVolts);

        // The mixer node's Thevenin resistance (270k || 390k plus the tone network's output) into V3a's grid.
        const auto gd = c.addNode(), pd = c.addNode(), kd = c.addNode(), ks = c.addNode();
        c.addResistor (cin, gd, 180.0e3);
        c.addTriode (pd, gd, kd, triode6SL7());
        c.addCapacitor (gd, pd, cgp);
        c.addResistor (vdr, pd, 120.0e3);                // R26
        c.addResistor (kd, ks, 220.0);                   // R25: NFB lands on V3a's cathode, above the shared R24
        c.addResistor (ks, gnd, 1.0e3);                  // R24
        c.addCapacitor (ks, gnd, 25.0e-6);
        c.setInitialGuess (pd, 235.0);
        c.setInitialGuess (kd, 3.0);
        c.setInitialGuess (ks, 2.2);
        ch.wDrvPlate = pd;

        // Floating paraphase: R28 470k from plate A and R29 510k from plate B meet at a node that drives V3b's grid
        // through C11 .022 (R23 470k leak). V3b shares the bypassed cathode; R27 120k plate.
        const auto m = c.addNode(), gi = c.addNode(), pi = c.addNode();
        c.addResistor (pd, m, 470.0e3);
        c.addCapacitor (m, gi, 0.022e-6);
        c.addResistor (gi, gnd, 470.0e3);
        c.addTriode (pi, gi, ks, triode6SL7());
        c.addCapacitor (gi, pi, cgp);
        c.addResistor (vdr, pi, 120.0e3);
        c.addResistor (pi, m, 510.0e3);
        c.setInitialGuess (pi, 225.0);
        c.setInitialGuess (m, 230.0);
        ch.wInvPlate = pi;

        // C13 / C14 .022 couplings, R30 / R31 270k leaks to the -50 V rail, R33 / R32 1k grid stoppers.
        const auto n1 = c.addNode(), n2 = c.addNode(), g3 = c.addNode(), g4 = c.addNode(),
                   pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addCapacitor (pd, n1, 0.022e-6);
        c.addCapacitor (pi, n2, 0.022e-6);
        ch.rBiasTapA = c.addResistor (n1, neg, 270.0e3);
        ch.rBiasTapB = c.addResistor (n2, neg, 270.0e3);
        c.addResistor (n1, g3, 1.0e3);
        c.addResistor (n2, g4, 1.0e3);
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

        // Negative feedback: R40 10k from the 8 ohm winding straight onto V3a's cathode (above R25).
        ch.rFeedback = c.addResistor (ch.wOut, kd, feedbackResistor);
        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (n1, biasSupplyVolts);
        c.setInitialGuess (n2, biasSupplyVolts);
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

double AmpegB15StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage,
                                                                  double bias, double feel) noexcept
{
    const double attackCoeff = 1.0 - std::exp (-1.0 / (0.008 * sampleRate));
    const double releaseCoeff = 1.0 - std::exp (-1.0 / (0.045 * sampleRate));
    const double absDrive = std::abs (toneVoltage);
    ch.bmEnvelope += (absDrive > ch.bmEnvelope ? attackCoeff : releaseCoeff) * (absDrive - ch.bmEnvelope);
    // Tube Feel folds how far the rail sags around its resting point; Bias shifts the knee's
    // operating point like the bias trims do on the reference netlist (feel/bias at defaults
    // give the fitted curve unchanged).
    ch.bmRail = railPlatesNominal - feel * (railPlatesNominal - sagRail (ch.bmEnvelope));

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double over = toneVoltage / bmGridClampV;
    const double clamped = toneVoltage / std::sqrt (1.0 + over * over);
    const auto knee = [k, rail = ch.bmRail] (double x) { return std::tanh (x / juce::jmax (1.0e-9, k)) * rail * bmYmax; };
    const double shift = (bmAsym + 0.6 * (bias - 0.5)) * k;
    const double raw = knee (clamped + shift) - knee (shift);

    const double dcCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmDcHz / sampleRate);
    ch.bmDcState += dcCoeff * (raw - ch.bmDcState);
    const double rawAc = raw - ch.bmDcState;

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / sampleRate);
    ch.bmToneState += shelfCoeff * (rawAc - ch.bmToneState);
    ch.bmOutput = ch.bmTail.process ((ch.bmToneState + bmShelfHfGain * (rawAc - ch.bmToneState)) * bmLevelTrim);
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
    const double volumeBot = juce::jmax (1.0, 1.0e6 * k.volume); // VR1 is 1M LINEAR on the drawing
    const double bassBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.treble));
    const bool bright = k.input > 0.5;

    const double ultraLoR = k.ultraLo > 0.5 ? 1.0 : 1.0e6;   // the rocker grounds the R9 / R10 junction
    const double uhC = k.ultraHi > 0.5 ? 500.0e-12 : 1.0e-12; // C9 across the Volume

    // Bias knob: the fixed-bias rail itself, -57 (cold) .. -43 V (hot), noon at the drawing's -50.
    const double biasV = -(57.0 - 14.0 * k.bias);

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
        ch.pre.setResistance (ch.rUltraLo, ultraLoR);
        ch.pre.setCapacitance (ch.capUltraHi, uhC);
        ch.pre.setResistance (ch.rInSeries, bright ? 100.0e3 : 47.0e3);
        ch.pre.setCapacitance (ch.capBright, bright ? 0.005e-6 : 1.0e-12);
        ch.pre.setResistance (ch.rInShunt, bright ? 47.0e3 : 1.0e12);
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
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 8.0, -0.8);
}

void AmpegB15StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sS);
}

void AmpegB15StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.0, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iS, -juce::jlimit (0.0, 0.2, ch.sumScreen / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 600.0));
        ch.power.setSource (ch.wSrcVdr, rail (ch.sD, 600.0));
    }
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sB, 600.0));
    ch.vScreen = rail (ch.sS, 600.0);
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
        auto bmVoicing = voicings::bassAmp;
        bmVoicing.cabLpHz = 8000.0;  // fitted against the netlist (the drawing's tone network keeps the top)
        bmVoicing.cabAirHz = 10000.0;
        bmVoicing.asym = 0.0; // even-order content already lives in the knee's dcShift law
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
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
        double ip = idlePlateCurrent, is = idleScreenCurrent;
        for (int pass = 0; pass < 6; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ip);
            ch.supply.setCurrentSource (ch.iS, -is);
            idleSupplyCurrent = ip + is + idleDriverCurrent + idlePreampCurrent;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance
                                                * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
            bool passOk = ch.supply.prepare (supplyRate);
            ch.vScreen = ch.supply.voltage (ch.sS);
            ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sB));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sD));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen);
            }
            passOk = ch.power.prepare (newSampleRate) && passOk;
            ch.power.solveSample();
            if (! reducedOrder)
            {
                double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ip = ipA + ipB;
                is = isA + isB;
            }
            if (pass == 5)
                dcOk = passOk && dcOk;
        }
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
    const double brightJack = juce::roundToInt (inputParam->get()) == 0 ? 0.0 : 1.0;
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
            updatePots ({ brightJack, (double) vo, ultraLoOn, ultraHiOn, (double) ba, (double) tr,
                          (double) ma, (double) pw, (double) bi, (double) fe, speakerChoice });
        }

        // Master (synthetic, linear -- the old 50k divider) and Power Drive both scale the mixer node into V3.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw)) * (double) ma;
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

            ch.power.setSource (ch.wSrcPre, masterGain * ch.pre.voltage (ch.pOut));
            if (! reducedOrder)
            {
                ch.power.setPentodeScreen (ch.penA, ch.vScreen);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen);
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
            ch.sumPlate += ipA + ipB;
            ch.sumScreen += isA + isB;
            ++ch.sumCount;

            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone), (double) bi, (double) fe)
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
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
