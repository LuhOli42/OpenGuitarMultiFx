#include "MesaBass400PlusStyleAmplifierProcessor.h"
#include "PotTaper.h"
#include "DualMono.h"
#include "IconKit.h"
#include "TubeAmpCommon.h"

#include <cstdlib>

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    using namespace tubeamp;

    // ---- supply (docs/circuits/MesaBass400Plus.md, "What the Bass 400+ actually is") ----
    // Supply, from Mesa/Boogie's 1983 Bass 400 drawing (docs/circuits/MesaBass400Plus.md): A 540 V (plates; screens
    // through 2700 each), filter choke to B 535 V (the V4 buffer), 10k 2 W to C 370 V (preamp + V3 driver).
    constexpr double railPlatesNominal = 540.0;
    constexpr double railBufferNominal = 535.0;
    constexpr double railPreampNominal = 370.0;
    constexpr double rectifierResistance = 60.0;  // bridge + PT (estimated -- not on the drawing)
    constexpr double chokeResistance = 50.0;      // filter choke DCR (estimated)
    constexpr double screenResistor = 2700.0 / 6.0; // 2700 5 W per tube, six per composite bank
    constexpr double idlePlateCurrent = 0.42;     // twelve 6L6s (initial guess; prepare() measures)
    constexpr double idleScreenCurrent = 0.030;
    constexpr double idleBufferCurrent = 0.0043;  // V4 pair: the marked 2 V across 470
    constexpr double idlePreampCurrent = 0.0165;  // everything on C: the marked 535 -> 370 V across the 10k
    constexpr double tailSupplyVolts = -68.0;     // F
    constexpr double biasSupplyVolts = -63.0;     // D

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7()
    {
        return {}; // Koren's ECC83 set
    }

    KorenTriode::Parameters triode12AU7()
    {
        KorenTriode::Parameters p;
        p.mu = 21.5;
        p.ex = 1.3;
        p.kg1 = 1180.0;
        p.kp = 84.0;
        return p;
    }

    KorenPentode::Parameters pentode6L6Sextuple()
    {
        // Six 6L6GCs share each bank's nodes: Kg1, Kg2 divide by six, the grid-current Gg multiplies,
        // arc resistance divides -- the pair pattern from DualRectifierStyleAmplifierProcessor, scaled.
        KorenPentode::Parameters p; // defaults are Koren's 6L6GC set
        p.kg1 /= 6.0;
        p.kg2 /= 6.0;
        p.grid.Gg *= 6.0;
        p.arcResistance /= 6.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer: ~1.2k plate-to-plate for twelve 6L6s, modelled at the 4 ohm tap (not on the drawing:
    // estimated).
    constexpr double primaryHalfInductance = 15.0;
    constexpr double halfToSecondaryTurns = 8.66;         // sqrt(300/4): one half-primary is 300 ohm
    constexpr double couplingHalves = 0.9996;
    constexpr double couplingSecondary = 0.9990;
    constexpr double primaryHalfResistance = 20.0;
    constexpr double secondaryResistance = 0.05;

    // Graphic EQ: the Bass 400 PLUS's seven bands (its manual). The Bass 400 drawing has six, each 470 (680 on the top
    // band) + L + C on a 50k slider: 1 H / 22 u, 1 H / 2.2 u, .35 H / 1 u, .22 H / .22 u, .068 H / .15 u, .033 H / .047 u.
    // The Plus keeps those parts' pattern: L from the drawing's family, C for the panel frequency (estimated).
    constexpr int eqBands = 7;
    constexpr double eqFreq[eqBands] = { 40.0, 80.0, 160.0, 320.0, 750.0, 2200.0, 6600.0 };
    constexpr double eqInductance[eqBands] = { 1.0, 1.0, 0.35, 0.22, 0.068, 0.033, 0.033 };
    constexpr double eqSeriesR[eqBands] = { 470.0, 470.0, 470.0, 470.0, 470.0, 680.0, 680.0 };

    constexpr double speakerNominal[3] = { 2.0, 4.0, 8.0 };

    constexpr double powerTheta = 0.9;
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::bass810, &tubeamp::cabs::tweed410, &tubeamp::cabs::greenback412 };
}

MesaBass400PlusStyleAmplifierProcessor::MesaBass400PlusStyleAmplifierProcessor()
{
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto bright = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_bright", "Bright", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto middle = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_middle", "Middle", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto master = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_master", "Master", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    const auto onOff = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
    });
    auto trebleShift = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_treble_shift", "Treble Shift", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f, onOff);
    auto bassShift = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_bass_shift", "Bass Shift", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f, onOff);

    volumeParam = volume.get();
    brightParam = bright.get();
    bassParam = bass.get();
    middleParam = middle.get();
    trebleParam = treble.get();
    masterParam = master.get();
    trebleShiftParam = trebleShift.get();
    bassShiftParam = bassShift.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "mesa400p", "Bass 400+", "|", std::move (volume));
    group->addChild (std::move (bright));
    group->addChild (std::move (bass));
    group->addChild (std::move (middle));
    group->addChild (std::move (treble));
    group->addChild (std::move (master));
    group->addChild (std::move (trebleShift));
    group->addChild (std::move (bassShift));

    const char* eqNames[eqBands] = { "40 Hz", "80 Hz", "160 Hz", "320 Hz", "750 Hz", "2.2 kHz", "6.6 kHz" };
    // The 7-band graphic EQ is a front-panel control on the real Bass 400+ -> page 1, not page 2.
    for (int i = 0; i < eqBands; ++i)
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (
            juce::String ("mesa400p_eq") + juce::String (i + 1), eqNames[i],
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
        eqParam[i] = p.get();
        group->addChild (std::move (p));
    }
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("mesa400p_page2", "Page 2", "|");
    auto power = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_power", "Power Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto bias = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto feel = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_tube_feel", "Tube Feel", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "mesa400p_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();
    outputParam = output.get();

    page2->addChild (std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("mesabass400_cab", "1x15 Bass", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void MesaBass400PlusStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply (page 2 of the drawing)
    // Bridge -> A 540 V (plates through the OT, screens through 2700 each) -> filter choke -> B 535 V (the V4 buffer;
    // 2 x 40 uF in series with 150k / 150k balancing) -> 10k 2 W -> C 370 V (30 uF: the preamp and the V3 driver).
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();

        const double total = idlePlateCurrent + idleScreenCurrent + idleBufferCurrent + idlePreampCurrent;
        ch.srcVoc = c.addSource (vo, railPlatesNominal + rectifierResistance * total);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 200.0e-6);
        c.addResistor (ch.sA, ch.sB, chokeResistance);
        c.addCapacitor (ch.sB, gnd, 20.0e-6);
        c.addResistor (ch.sB, gnd, 300.0e3);
        c.addResistor (ch.sB, ch.sC, 10.0e3);
        c.addCapacitor (ch.sC, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -(idlePlateCurrent + idleScreenCurrent));
        ch.iB = c.addCurrentSource (ch.sB, -idleBufferCurrent);
        ch.iC = c.addCurrentSource (ch.sC, -idlePreampCurrent);
        c.setInitialGuess (vo, railPlatesNominal + rectifierResistance * total);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railBufferNominal);
        c.setInitialGuess (ch.sC, railPreampNominal);
    }

    // ================================================================ preamp (channel 1)
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railPreampNominal);
        ch.pSrcIn = c.addSource (in, 0.0);

        // Input 1 -> .047 -> V1A (1M leak, 100k plate, 1500 || 15 uF).
        const auto ni = c.addNode(), g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, ni, 1.0e3);
        c.addCapacitor (ni, g1, 0.047e-6);
        c.addResistor (g1, gnd, 1.0e6);
        c.addTriode (p1, g1, k1, triode12AX7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vcc, p1, 100.0e3);
        c.addResistor (k1, gnd, 1500.0);
        c.addCapacitor (k1, gnd, 15.0e-6);
        c.setInitialGuess (p1, 247.0);
        c.setInitialGuess (k1, 1.84);

        // .047 -> Volume 1 (1M) with the pull Bright's 120 pF from top to wiper. The wiper meets channel 2's through
        // 1.5M (channel 2: 470k) on V2A's grid, which has 470k to ground; channel 2's Volume down adds its 470k.
        const auto v = c.addNode(), w = c.addNode(), m = c.addNode();
        c.addCapacitor (p1, v, 0.047e-6);
        ch.rVolumeTop = c.addResistor (v, w, 500.0e3);
        ch.rVolumeBot = c.addResistor (w, gnd, 500.0e3);
        ch.capBright = c.addCapacitor (v, w, 1.0e-12);
        c.addResistor (w, m, 1.5e6);
        c.addResistor (m, gnd, 470.0e3);
        c.addResistor (m, gnd, 470.0e3);
        c.setInitialGuess (v, 0.0);

        // V2A: 100k plate, 1000 cathode with .15 uF (treble bypass) and 15 uF + 33k; Bass Shift grounds the 33k end.
        const auto p2 = c.addNode(), k2 = c.addNode(), ns = c.addNode();
        c.addTriode (p2, m, k2, triode12AX7());
        c.addCapacitor (m, p2, cgp);
        c.addResistor (vcc, p2, 100.0e3);
        c.addResistor (k2, gnd, 1000.0);
        c.addCapacitor (k2, gnd, 0.15e-6);
        c.addCapacitor (k2, ns, 15.0e-6);
        c.addResistor (ns, gnd, 33.0e3);
        ch.rBassShift = c.addResistor (ns, gnd, 1.0e12);
        c.setInitialGuess (p2, 224.0);
        c.setInitialGuess (k2, 1.5);
        c.setInitialGuess (ns, 0.0);

        // Tone stack straight off V2A's plate: 250 pF (+750 pF Treble Shift) into the Treble pot; 100k slope to a node
        // that feeds .047 to the Treble pot's bottom / Bass top and .1 to the Bass bottom / Mid top; Bass 250k and Mid
        // 10k are rheostats. Treble wiper -> .047 -> V2B.
        const auto tt = c.addNode(), tw = c.addNode(), u = c.addNode(), sb = c.addNode(), vm = c.addNode();
        c.addCapacitor (p2, tt, 250.0e-12);
        ch.capTrebleShift = c.addCapacitor (p2, tt, 1.0e-12);
        ch.rTrebleTop = c.addResistor (tt, tw, 125.0e3);
        ch.rTrebleBot = c.addResistor (tw, u, 125.0e3);
        c.addResistor (p2, sb, 100.0e3);
        c.addCapacitor (sb, u, 0.047e-6);
        c.addCapacitor (sb, vm, 0.1e-6);
        ch.rBassTop = c.addResistor (u, vm, 125.0e3);
        ch.rMid = c.addResistor (vm, gnd, 5.0e3);
        ch.pTone = tw;
        c.setInitialGuess (tt, 224.0);
        c.setInitialGuess (sb, 224.0);

        // V2B cathode follower (plate on C, 33k cathode; grid 3.3M from C and ~2.8M to ground for the marked 170 V)
        // -> .1 -> the 15k send node.
        const auto g4 = c.addNode(), k4 = c.addNode();
        c.addCapacitor (tw, g4, 0.047e-6);
        c.addResistor (g4, vcc, 3.3e6);
        c.addResistor (g4, gnd, 2.8e6);
        c.addTriode (vcc, g4, k4, triode12AX7());
        c.addResistor (k4, gnd, 33.0e3);
        c.setInitialGuess (g4, 170.0);
        c.setInitialGuess (k4, 172.0);
        ch.pOut = c.addNode();
        c.addCapacitor (k4, ch.pOut, 0.1e-6);
        c.addResistor (ch.pOut, gnd, 15.0e3);
        c.setInitialGuess (ch.pOut, 0.0);
        ch.pPlate1 = p1;
        ch.pPlate2 = p2;
        ch.pPlate3 = k4;
    }

    // ================================================================ graphic EQ (linear block)
    // The discrete op-amp of the drawing (Q1 follower, Q2 / Q3 pair, Q4 output) as a high-gain block: 3300 from the
    // input into its + input, 3300 (|| 10 pF) from the output back to its - input; every 50k slider spans + and -, its
    // wiper carrying series R + L + C to AC ground. Wiper at the + end shunts the input (cut), at the - end the
    // feedback (boost).
    {
        auto& c = ch.eq;
        c.setIntegrationTheta (0.5);
        const auto cin = c.addNode(), pp = c.addNode(), nn = c.addNode();
        ch.qOut = c.addNode();
        ch.qSrcIn = c.addSource (cin, 0.0);
        c.addResistor (cin, pp, 3300.0);
        c.addResistor (ch.qOut, nn, 3300.0);
        c.addCapacitor (ch.qOut, nn, 10.0e-12);
        c.addFiniteGainOpAmp (pp, nn, ch.qOut, 1.0e4);
        for (int i = 0; i < eqBands; ++i)
        {
            const auto w = c.addNode(), r = c.addNode(), x = c.addNode();
            ch.rEqTop[i] = c.addResistor (pp, w, 25.0e3);
            ch.rEqBot[i] = c.addResistor (w, nn, 25.0e3);
            c.addResistor (w, r, eqSeriesR[i]);
            c.addCoupledInductors ({ { r, x } }, { eqInductance[i] });
            const double cap = 1.0 / (39.478 * eqFreq[i] * eqFreq[i] * eqInductance[i]);
            c.addCapacitor (x, gnd, cap);
        }
    }

    // ================================================================ power amp: V3 driver (12AX7 long-tailed pair),
    // V4 buffer (12AX7 pair), twelve 6L6s as two sextets, output transformer, speaker. No global feedback on the drawing.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cin = c.addNode();
        const auto vc = c.addNode(), vb = c.addNode(), ct = c.addNode(), negF = c.addNode(), negD = c.addNode();
        ch.wSrcPre = c.addSource (cin, 0.0);
        ch.wTone = cin;
        c.addResistor (cin, gnd, 1.0e6);

        // FULL reference power stage only -- reducedOrder replaces everything below with
        // behavioralPowerStage(), fitted to this same circuit (see the header + docs/circuits/MesaBass400Plus.md).
        if (! reducedOrder)
        {
        ch.wSrcPi = c.addSource (vc, railPreampNominal);
        ch.wSrcVdr = c.addSource (vb, railBufferNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcTail = c.addSource (negF, tailSupplyVolts);
        ch.wSrcNeg = c.addSource (negD, biasSupplyVolts);

        // Master 250k wiper (~60k source at the shipped setting) into V3A; 33k plates on C, 33k tail to F (-68 V).
        // V3B's grid sits on the junction of two 62k from the two coupled plate outputs: the pair balances itself.
        const auto ga = c.addNode(), gb = c.addNode(), pa = c.addNode(), pb = c.addNode(), kt = c.addNode(),
                   a = c.addNode(), b = c.addNode();
        c.addResistor (cin, ga, 60.0e3);
        c.addTriode (pa, ga, kt, triode12AX7());
        c.addTriode (pb, gb, kt, triode12AX7());
        c.addCapacitor (ga, pa, cgp);
        c.addCapacitor (gb, pb, cgp);
        c.addResistor (vc, pa, 33.0e3);
        c.addResistor (vc, pb, 33.0e3);
        c.addResistor (kt, negF, 33.0e3);
        c.addCapacitor (pa, a, 0.047e-6);
        c.addCapacitor (pb, b, 0.047e-6);
        c.addResistor (a, gb, 62.0e3);
        c.addResistor (b, gb, 62.0e3);
        c.setInitialGuess (pa, 335.0);
        c.setInitialGuess (pb, 335.0);
        c.setInitialGuess (kt, 1.5);
        ch.wGridA = ga;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = kt;

        // 470k / 220k into the V4 buffer pair: plates to B (120k -- unmarked; it is what the drawing's 276 V plates and
        // 2 V cathodes need), shared 470 cathode.
        const auto gv1 = c.addNode(), gv2 = c.addNode(), pv1 = c.addNode(), pv2 = c.addNode(), kv = c.addNode();
        c.addResistor (a, gv1, 470.0e3);
        c.addResistor (gv1, gnd, 220.0e3);
        c.addResistor (b, gv2, 470.0e3);
        c.addResistor (gv2, gnd, 220.0e3);
        c.addTriode (pv1, gv1, kv, triode12AX7());
        c.addTriode (pv2, gv2, kv, triode12AX7());
        c.addCapacitor (gv1, pv1, cgp);
        c.addCapacitor (gv2, pv2, cgp);
        c.addResistor (vb, pv1, 120.0e3);
        c.addResistor (vb, pv2, 120.0e3);
        c.addResistor (kv, gnd, 470.0);
        c.setInitialGuess (pv1, 276.0);
        c.setInitialGuess (pv2, 276.0);
        c.setInitialGuess (kv, 2.0);
        ch.wDrvPlateA = pv1;

        // .1 600 V couplings, 100k to D (-63 V), 2200 grid stoppers per tube (367 per composite sextet).
        const auto na = c.addNode(), nb = c.addNode(), g3 = c.addNode(), g4 = c.addNode(), pp1 = c.addNode(),
                   pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addCapacitor (pv1, na, 0.1e-6);
        c.addCapacitor (pv2, nb, 0.1e-6);
        c.addResistor (na, negD, 100.0e3);
        c.addResistor (nb, negD, 100.0e3);
        c.addResistor (na, g3, 2200.0 / 6.0);
        c.addResistor (nb, g4, 2200.0 / 6.0);
        c.addCapacitor (g3, gnd, 660.0e-12);             // six 6L6s' input capacitance per bank
        c.addCapacitor (g4, gnd, 660.0e-12);
        ch.wPowerGridA = g3;
        ch.penA = c.addPentode (pp1, g3, gnd, pentode6L6Sextuple(), railPlatesNominal);
        ch.penB = c.addPentode (pp2, g4, gnd, pentode6L6Sextuple(), railPlatesNominal);

        c.addCapacitor (pp1, pp2, 400.0e-12);
        c.addResistor (pp1, pp2, 60.0e3);
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12,  mps,
                                 m12, lh,  -mps,
                                 mps, -mps, ls });
        ch.wOut = c.addNode();
        c.addResistor (sw, ch.wOut, secondaryResistance);
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        {
            const auto sm = speakerModel (4.0);
            const auto na2 = c.addNode(), nbb2 = c.addNode();
            ch.rSpkRe = c.addResistor (ch.wOut, na2, sm.re);
            ch.grpSpkLe = c.addCoupledInductors ({ { na2, nbb2 } }, { sm.le });
            ch.rSpkRp = c.addResistor (nbb2, gnd, sm.rp);
            ch.grpSpkLp = c.addCoupledInductors ({ { nbb2, gnd } }, { sm.lp });
            ch.capSpkCp = c.addCapacitor (nbb2, gnd, sm.cp);
        }

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (g3, biasSupplyVolts);
        c.setInitialGuess (g4, biasSupplyVolts);
        c.setInitialGuess (na, biasSupplyVolts);
        c.setInitialGuess (nb, biasSupplyVolts);
        }
    }
}

double MesaBass400PlusStyleAmplifierProcessor::sagRail (double envelope) const noexcept
{
    // Sag fitted to the reference netlist's supply droop: up to ~10 % into full drive.
    return railPlatesNominal * juce::jlimit (0.80, 1.0, 1.0 - 0.010 * envelope);
}

double MesaBass400PlusStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage,
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

void MesaBass400PlusStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 2, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void MesaBass400PlusStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double volumeBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));
    // Treble 250k (unmarked; audio), Bass 250k and Mid 10k rheostats.
    const double trebleBot = juce::jmax (1.0, 250.0e3 * pots::audio (k.treble));
    const double bassR = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
    const double midR = juce::jmax (1.0, 10.0e3 * k.middle);
    const double brightC = k.bright > 0.5 ? 120.0e-12 : 1.0e-12;
    // Bias: D, -70 (cold) .. -56 V (hot), noon at the drawing's -63.
    const double biasV = -(70.0 - 14.0 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolumeTop, juce::jmax (1.0, 1.0e6 - volumeBot));
        ch.pre.setResistance (ch.rVolumeBot, volumeBot);
        ch.pre.setCapacitance (ch.capBright, brightC);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 250.0e3 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rBassTop, bassR);
        ch.pre.setResistance (ch.rMid, midR);
        ch.pre.setCapacitance (ch.capTrebleShift, k.trebleShift > 0.5 ? 750.0e-12 : 1.0e-12);
        ch.pre.setResistance (ch.rBassShift, k.bassShift > 0.5 ? 1.0 : 1.0e12);
        for (int i = 0; i < eqBands; ++i)
        {
            const double slider = juce::jlimit (0.0, 1.0, k.eq[i]);
            // Slider up = boost: the wiper toward the - (feedback) end.
            ch.eq.setResistance (ch.rEqTop[i], juce::jmax (1.0, 50.0e3 * slider));
            ch.eq.setResistance (ch.rEqBot[i], juce::jmax (1.0, 50.0e3 * (1.0 - slider)));
        }
        if (! reducedOrder)
            ch.power.setSource (ch.wSrcNeg, biasV);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! reducedOrder && ! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
    }
    appliedSpeaker = k.speaker;
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 4.0, -0.8);
}

void MesaBass400PlusStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.eq.restoreDynamicState (ch.eqRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sA);
}

void MesaBass400PlusStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 2.5, (ch.sumPlate + ch.sumScreen) / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder) ch.power.setSource (ch.wSrcCt, rail (ch.sA, 700.0));
    if (! reducedOrder) ch.power.setSource (ch.wSrcPi, rail (ch.sC, 600.0));
    if (! reducedOrder) ch.power.setSource (ch.wSrcVdr, rail (ch.sB, 700.0));
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sC, 600.0));
    ch.vScreen = rail (ch.sA, 700.0);
}

double MesaBass400PlusStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::firstPlate: return ch.pre.voltage (ch.pPlate1);
        case Probe::secondPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::toneStackOut: return ch.pre.voltage (ch.pTone);
        case Probe::recoveryPlate: return ch.pre.voltage (ch.pPlate3);
        case Probe::eqOut: return ch.eq.voltage (ch.qOut);
        case Probe::phaseInverterGrid: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateB);
        case Probe::driverPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wDrvPlateA);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPowerGridA);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double MesaBass400PlusStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void MesaBass400PlusStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void MesaBass400PlusStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    // The drawing has no global feedback loop: nothing to open.
    feedbackOverride = ohms;
}

double MesaBass400PlusStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double MesaBass400PlusStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void MesaBass400PlusStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = voicings::bassAmp;
        bmVoicing.cabLpHz = 8000.0;  // fitted: the drawing's netlist keeps more top than the shared bass-cab voicing
        bmVoicing.cabAirHz = 10000.0;
        bmVoicing.asym = 0.0; // even-order content already lives in the knee's dcShift law
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
    }

    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());
    smoothedBass.reset (newSampleRate, 0.02);
    smoothedBass.setCurrentAndTargetValue (bassParam->get());
    smoothedMiddle.reset (newSampleRate, 0.02);
    smoothedMiddle.setCurrentAndTargetValue (middleParam->get());
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

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + idleBufferCurrent + idlePreampCurrent;
    appliedSpeaker = 1;
    Knobs k {};
    k.input = 1.0;
    k.volume = volumeParam->get();
    k.bright = brightParam->get();
    k.bass = bassParam->get();
    k.middle = middleParam->get();
    k.treble = trebleParam->get();
    k.master = masterParam->get();
    k.trebleShift = trebleShiftParam->get();
    k.bassShift = bassShiftParam->get();
    for (int i = 0; i < eqBands; ++i)
        k.eq[i] = eqParam[i]->get();
    k.powerDrive = powerParam->get();
    k.bias = biasParam->get();
    k.tubeFeel = tubeFeelParam->get();
    k.speaker = juce::roundToInt (speakerParam->get());
    updatePots (k);

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        double ip = idlePlateCurrent, is = idleScreenCurrent;
        for (int pass = 0; pass < 6; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -(ip + is));
            idleSupplyCurrent = ip + is + idleBufferCurrent + idlePreampCurrent;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance
                                                * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
            bool passOk = ch.supply.prepare (supplyRate);
            ch.vScreen = ch.supply.voltage (ch.sA);
            ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sC));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            passOk = ch.eq.prepare (newSampleRate) && passOk;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sB));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - screenResistor * 0.5 * is);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - screenResistor * 0.5 * is);
            }
            passOk = ch.power.prepare (newSampleRate) && passOk;
            ch.power.solveSample();
            double isA = 0.5 * is, isB = 0.5 * is;
            if (! reducedOrder)
            {
                double ipA = 0.0, ipB = 0.0;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ip = ipA + ipB;
                is = isA + isB;
            }
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
            if (pass == 5)
                dcOk = passOk && dcOk;
        }
        ch.pre.saveDynamicState (ch.preRest);
        ch.eq.saveDynamicState (ch.eqRest);
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

void MesaBass400PlusStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const double brightOn = brightParam->get() > 0.5f ? 1.0 : 0.0;
    const double trebleShiftOn = trebleShiftParam->get() > 0.5f ? 1.0 : 0.0;
    const double bassShiftOn = bassShiftParam->get() > 0.5f ? 1.0 : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float mi = smoothedMiddle.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ma = smoothedMaster.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            Knobs k {};
            k.input = 1.0;
            k.volume = vo;
            k.bright = brightOn;
            k.bass = ba;
            k.middle = mi;
            k.treble = tr;
            k.master = ma;
            k.trebleShift = trebleShiftOn;
            k.bassShift = bassShiftOn;
            for (int b = 0; b < eqBands; ++b)
                k.eq[b] = eqParam[b]->get();
            k.powerDrive = pw;
            k.bias = bi;
            k.tubeFeel = fe;
            k.speaker = speakerChoice;
            updatePots (k);
        }

        // Master (250k, after the effects loop and the EQ, as the manual says) and Power Drive into V3.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw)) * pots::audio ((double) ma);
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

            ch.eq.setSource (ch.qSrcIn, ch.pre.voltage (ch.pOut));
            ch.eq.solveSample(); // linear block: cannot diverge, always one pass
            ch.power.setSource (ch.wSrcPre, masterGain * ch.eq.voltage (ch.qOut));
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
            }
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
                    ch.eq.saveDynamicState (ch.eqRest);
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

void MesaBass400PlusStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
