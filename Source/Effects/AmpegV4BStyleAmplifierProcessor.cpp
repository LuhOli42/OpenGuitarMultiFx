#include "AmpegV4BStyleAmplifierProcessor.h"
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

    // ---- supply, from the factory V-4B drawing (4010267, 5-71; docs/circuits/AmpegV4B.md) ----
    constexpr double railPlatesNominal = 545.0;   // bridge output: OT centre tap
    constexpr double railScreensNominal = 540.0;  // after R55 470 7 W
    constexpr double railDriverNominal = 360.0;   // the long-tailed pair's 47k plates (marked 360 V)
    constexpr double railPreNominal = 325.0;      // 6K11, the 12DW7 (marked 325 V)
    constexpr double railLowNominal = 318.0;      // V1 / V2 below R51 2.2k (not marked; 325 V less ~3 mA x 2.2k)
    constexpr double rectifierResistance = 110.0; // PT + bridge (estimated -- not on the drawing)
    constexpr double screenResistor = 235.0;      // R41 / R42 470 per tube: two in parallel per composite bank
    constexpr double idlePlateCurrent = 0.16;     // four 7027As (initial guess; prepare() measures)
    constexpr double idleScreenCurrent = 0.010;
    constexpr double idleSmallCurrent = 0.025;    // everything on the 360 / 325 V rails, through R53
    constexpr double biasSupplyVolts = -64.0;     // marked -64 V

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7()
    {
        return {}; // Koren's ECC83 set
    }

    KorenTriode::Parameters triode12AX7PI()
    {
        // kg1 NOT softened -- the multiplier that used to sit here collapsed the reference ~40 dB;
        // un-softened measures silence-stable today (see docs/circuits/CircuitFamilies.md).
        KorenTriode::Parameters p;

        return p;
    }

    KorenTriode::Parameters triode12AU7()
    {
        // Koren's published 12AU7 set (Table 1 of the 1996 paper): mu 21.5, Ex 1.3, Kg1 1180, Kp 84, Kvb 300.
        KorenTriode::Parameters p;
        p.mu = 21.5;
        p.ex = 1.3;
        p.kg1 = 1180.0;
        p.kp = 84.0;
        return p;
    }

    KorenPentode::Parameters pentode7027APair()
    {
        // The 7027A is a rated-up 6L6GC: two per bank share their nodes, so Kg1, Kg2 (and grid current)
        // halve, the grid-current table's Gg doubles, arc resistance halves -- the same pattern as
        // DualRectifierStyleAmplifierProcessor's pentode6L6Pair().
        KorenPentode::Parameters p; // Koren's 6L6GC defaults
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate Miller capacitance

    // Output transformer T2 (8950028): ~4k plate-to-plate for the four 7027As, modelled at the 4 ohm tap. The drawing
    // gives only the part number -- an estimate, see the doc's "Honest simplifications".
    constexpr double primaryHalfInductance = 30.0;        // H per half -- heavy bass-amp iron; at 12 H
                                                            // the OT's ~13 Hz HP pole stacked on the two
                                                            // RC poles inside the NFB loop and motorboated
    constexpr double halfToSecondaryTurns = 15.8;         // sqrt(1000/4): one half-primary is 1k, secondary 4
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.9990;
    constexpr double primaryHalfResistance = 25.0;
    constexpr double secondaryResistance = 0.08;
    constexpr double feedbackResistor = 4.7e3 / 1.4142135623730951; // R34, referred to the 4 ohm tap (see buildChannel)
    constexpr double feedbackSense = 1.0; // secondary polarity that makes the R34 loop NEGATIVE feedback

    // L101 + SW3: the wiper's series-resonant leg, tuned to the spec sheet's 300 / 800 / 3000 Hz. The drawing gives
    // C113 .033 and C114 .15; the 300 Hz position's capacitance (both caps) and every inductance / coil resistance are
    // derived from those resonances (estimated).
    constexpr double midCapacitance[3] = { 0.183e-6, 0.15e-6, 0.033e-6 };
    constexpr double midInductance[3] = { 1.54, 0.264, 0.085 };
    constexpr double midCoilR[3] = { 460.0, 80.0, 25.0 };

    constexpr double speakerNominal[3] = { 2.0, 4.0, 8.0 };

    constexpr double powerTheta = 0.9; // same reasoning as the other amps: damped integrator for the stiff power block
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::bass810, &tubeamp::cabs::bass115, &tubeamp::cabs::greenback412 };
}

AmpegV4BStyleAmplifierProcessor::AmpegV4BStyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "0 dB" : "-15 dB");
        }));
    auto gain = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto ultraLo = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_ultra_lo", "Ultra Lo", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto ultraHi = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_ultra_hi", "Ultra Hi", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto middle = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_middle", "Middle", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto midFreq = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_mid_freq", "Mid Freq", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("300 Hz"); case 2: return juce::String ("3000 Hz"); default: return juce::String ("800 Hz"); }
        }));
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto master = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_master", "Master", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto power = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_power", "Power Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto bias = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto feel = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_tube_feel", "Tube Feel", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "ampegv4b_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    gainParam = gain.get();
    ultraLoParam = ultraLo.get();
    ultraHiParam = ultraHi.get();
    bassParam = bass.get();
    middleParam = middle.get();
    midFreqParam = midFreq.get();
    trebleParam = treble.get();
    masterParam = master.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ampegv4b", "V4B", "|", std::move (input));
    group->addChild (std::move (gain));
    group->addChild (std::move (ultraLo));
    group->addChild (std::move (ultraHi));
    group->addChild (std::move (bass));
    group->addChild (std::move (middle));
    group->addChild (std::move (midFreq));
    group->addChild (std::move (treble));
    group->addChild (std::move (master));
    // Page 2 (a sub-group: see EffectProcessor::getParameterPages()).
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ampegv4b_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("ampegv4b_cab", "2x15 V4B", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    struct PreampBuild
    {
        int srcVccLow = 0, srcVcc = 0, srcIn = 0;
        int rVolTop = 0, rVolBot = 0, rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0;
        int rUlDirect = 0, rUlIn = 0, rUlOut = 0, capUltraHi = 0, rUhBypass = 0;
        int rMidA = 0, rMidB = 0, rCoil = 0, grpMidL = 0, capMidC = 0;
        NodalCircuit::Node plate1 = 0, plate2 = 0, tone = 0, midPlate = 0, mid = 0, out = 0;
    };

    /** Channel 1 of the V-4B as drawn (factory drawing 4010267, 5-71): V1a -> Ultra-Lo -> Volume (+ Ultra-Hi) ->
        V1b -> bass/treble network -> the 6K11 active mid stage -> the 12DW7's 12AX7 cathode follower -> the
        Ext Amp jacks. vccLow is the V1 / V2 rail (below R51), vcc the 325 V rail. */
    PreampBuild buildPreamp (NodalCircuit& c, double vccLow0, double vcc0)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b;
        c.setIntegrationTheta (0.5);
        const auto vccLow = c.addNode(), vcc = c.addNode(), in = c.addNode();
        b.srcVccLow = c.addSource (vccLow, vccLow0);
        b.srcVcc = c.addSource (vcc, vcc0);
        b.srcIn = c.addSource (in, 0.0);

        // V1a: J1 straight to the grid (R2 5.6M leak), R6 390k plate, R5 3.3k cathode unbypassed.
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, g1, 1.0e3);                   // source / jack wiring (no stopper on the drawing)
        c.addResistor (g1, gnd, 5.6e6);
        c.addTriode (p1, g1, k1, triode12AX7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vccLow, p1, 390.0e3);
        c.addResistor (k1, gnd, 3.3e3);
        c.setInitialGuess (p1, 155.0);
        c.setInitialGuess (k1, 1.48);

        // C3 .1 -> R101 100k to ground -> the Ultra-Lo switch: OFF passes straight to the Volume; ON routes through
        // C101 .015 to ground and three series C102-C104 .0047 with R102 1.5M / R103 1.2M legs.
        const auto a = c.addNode(), ub = c.addNode(), j1 = c.addNode(), j2 = c.addNode(), uc = c.addNode(),
                   v = c.addNode(), w = c.addNode();
        c.addCapacitor (p1, a, 0.1e-6);
        c.addResistor (a, gnd, 100.0e3);
        b.rUlDirect = c.addResistor (a, v, 1.0);
        b.rUlIn = c.addResistor (a, ub, 1.0e12);
        c.addCapacitor (ub, gnd, 0.015e-6);
        c.addCapacitor (ub, j1, 0.0047e-6);
        c.addResistor (j1, gnd, 1.5e6);
        c.addCapacitor (j1, j2, 0.0047e-6);
        c.addResistor (j2, gnd, 1.2e6);
        c.addCapacitor (j2, uc, 0.0047e-6);
        c.addResistor (uc, gnd, 1.0e9);                  // keeps the switched-out ladder end defined
        b.rUlOut = c.addResistor (uc, v, 1.0e12);
        c.setInitialGuess (a, 0.0);

        // Volume VR101 1M LINEAR; Ultra-Hi switches C105 500 pF from its top to the wiper and lifts V1b's cathode
        // bypass (C106 6.8 uF) behind R107 47k.
        b.rVolTop = c.addResistor (v, w, 500.0e3);
        b.rVolBot = c.addResistor (w, gnd, 500.0e3);
        b.capUltraHi = c.addCapacitor (v, w, 1.0e-12);
        const auto p2 = c.addNode(), k2 = c.addNode(), kc = c.addNode();
        c.addTriode (p2, w, k2, triode12AX7());
        c.addCapacitor (w, p2, cgp);
        c.addResistor (vccLow, p2, 68.0e3);              // R13
        c.addResistor (k2, gnd, 3.3e3);                  // R14
        b.rUhBypass = c.addResistor (k2, kc, 1.0);
        c.addCapacitor (kc, gnd, 6.8e-6);                // C106
        c.setInitialGuess (p2, 230.0);
        c.setInitialGuess (k2, 2.1);
        c.setInitialGuess (kc, 2.1);

        // C7 .1 into the bass / treble network (P.E.C. 6470000 -- its parts are not printed; the same James stack as
        // Ampeg's B-15 P.E.C.: 220k, Bass 1M log with .001 / .01, 22k; .00047 into Treble 1M log, .0047; 120k between
        // the wipers). Channel 2's V2b joins the same node through C22 .03; idle, its plate is ~33k to AC ground.
        const auto t = c.addNode(), n22 = c.addNode(), bt = c.addNode(), bw = c.addNode(), bb = c.addNode(),
                   tt = c.addNode(), x = c.addNode(), tb = c.addNode();
        c.addCapacitor (p2, t, 0.1e-6);
        c.addCapacitor (t, n22, 0.03e-6);
        c.addResistor (n22, gnd, 33.0e3);
        c.addResistor (t, bt, 220.0e3);
        b.rBassTop = c.addResistor (bt, bw, 500.0e3);
        b.rBassBot = c.addResistor (bw, bb, 500.0e3);
        c.addCapacitor (bt, bw, 0.001e-6);
        c.addCapacitor (bb, bw, 0.01e-6);
        c.addResistor (bb, gnd, 22.0e3);
        c.addCapacitor (t, tt, 470.0e-12);
        b.rTrebleTop = c.addResistor (tt, x, 500.0e3);
        b.rTrebleBot = c.addResistor (x, tb, 500.0e3);
        c.addCapacitor (tb, gnd, 0.0047e-6);
        c.addResistor (bw, x, 120.0e3);
        b.tone = x;
        c.setInitialGuess (t, 0.0);

        // The 6K11 mid stage: T1 (R204 220k plate; cathode Q on R202 560 + R203 7.5k, its 1M grid leak bootstrapped
        // to the 560/7.5k joint) -> C202 .01 -> T2 (R207 470k, R206 3.3k) -> T3 cathode follower. The follower feeds
        // back to Q through R208 56k and drives the 47k / 6.8k divider S, which is the stage output. The 50k
        // Mid-Range pot spans Q (via 470 + .33: boost) and S (via 620 + .33: cut); its wiper carries L101 and the
        // selected capacitor to ground.
        const auto g11 = c.addNode(), pq = c.addNode(), pm1 = c.addNode(), q = c.addNode(), g7 = c.addNode(),
                   pm2 = c.addNode(), k6 = c.addNode(), kf = c.addNode(), s = c.addNode();
        c.addCapacitor (x, g11, 0.01e-6);                // C201
        c.addResistor (g11, pq, 1.0e6);                  // R201
        c.addTriode (pm1, g11, q, triode12AX7());
        c.addCapacitor (g11, pm1, cgp);
        c.addResistor (vcc, pm1, 220.0e3);
        c.addResistor (q, pq, 560.0);
        c.addResistor (pq, gnd, 7.5e3);
        c.addCapacitor (pm1, g7, 0.01e-6);               // C202
        c.addResistor (g7, gnd, 1.0e6);                  // R205
        c.addTriode (pm2, g7, k6, triode12AX7());
        c.addCapacitor (g7, pm2, cgp);
        c.addResistor (vcc, pm2, 470.0e3);
        c.addResistor (k6, gnd, 3.3e3);
        c.addTriode (vcc, pm2, kf, triode12AX7());
        c.addResistor (kf, q, 56.0e3);                   // R208
        c.addResistor (kf, s, 47.0e3);                   // R211
        c.addResistor (s, gnd, 6.8e3);                   // R210
        c.setInitialGuess (pm1, 192.0);
        c.setInitialGuess (q, 22.0);
        c.setInitialGuess (pq, 20.5);
        c.setInitialGuess (pm2, 145.0);
        c.setInitialGuess (k6, 1.26);
        c.setInitialGuess (kf, 140.0);
        c.setInitialGuess (s, 17.7);
        const auto ea = c.addNode(), ea2 = c.addNode(), eb = c.addNode(), eb2 = c.addNode(), mw = c.addNode(),
                   ml = c.addNode(), mc = c.addNode();
        c.addResistor (q, ea, 470.0);                    // R209
        c.addCapacitor (ea, ea2, 0.33e-6);               // C203
        c.addResistor (s, eb, 620.0);                    // R212
        c.addCapacitor (eb, eb2, 0.33e-6);               // C204
        b.rMidA = c.addResistor (ea2, mw, 25.0e3);
        b.rMidB = c.addResistor (eb2, mw, 25.0e3);
        b.rCoil = c.addResistor (mw, ml, midCoilR[1]);
        b.grpMidL = c.addCoupledInductors ({ { ml, mc } }, { midInductance[1] });
        b.capMidC = c.addCapacitor (mc, gnd, midCapacitance[1]);
        c.addResistor (mc, gnd, 220.0e3);                // R110 / R111 bleed
        b.midPlate = pm1;
        b.mid = s;

        // S -> C205 .1 (R16 180k) -> C8 .01 -> the 12DW7's 12AU7 half as a cathode follower (its marked 200 V cathode
        // and ~4 mA need the medium-mu section): R19 1k + R20 47k, its
        // R18 1M grid leak bootstrapped to the 1k / 47k joint. C9 .47 -> R22 10k -> the Ext Amp jacks (R21 100k).
        const auto o1 = c.addNode(), g3 = c.addNode(), k3 = c.addNode(), kb = c.addNode(), o2 = c.addNode(),
                   jack = c.addNode();
        c.addCapacitor (s, o1, 0.1e-6);
        c.addResistor (o1, gnd, 180.0e3);
        c.addCapacitor (o1, g3, 0.01e-6);
        c.addResistor (g3, kb, 1.0e6);
        c.addTriode (vcc, g3, k3, triode12AU7());
        c.addResistor (k3, kb, 1.0e3);
        c.addResistor (kb, gnd, 47.0e3);
        c.addCapacitor (k3, o2, 0.47e-6);
        c.addResistor (o2, jack, 10.0e3);
        c.addResistor (jack, gnd, 100.0e3);
        c.setInitialGuess (g3, 197.0);
        c.setInitialGuess (k3, 200.0);
        c.setInitialGuess (kb, 196.0);
        c.setInitialGuess (o1, 0.0);
        c.setInitialGuess (o2, 0.0);
        b.out = jack;

        b.plate1 = p1;
        b.plate2 = p2;
        return b;
    }
}

void AmpegV4BStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // Bridge -> 545 V (OT centre tap, ~150 uF of cans) -> R55 470 7 W -> 540 V (screens, C19 40 uF). The 360 / 325 V
    // rails and the V1 / V2 rail below them are tracked from the screen node (see updateSupply()).
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();

        const double total = idlePlateCurrent + idleScreenCurrent + idleSmallCurrent;
        ch.srcVoc = c.addSource (vo, railPlatesNominal + rectifierResistance * total);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 150.0e-6);
        c.addResistor (ch.sA, gnd, 200.0e3);             // R56 + R57 bleeders
        c.addResistor (ch.sA, ch.sB, 470.0);             // R55
        c.addCapacitor (ch.sB, gnd, 40.0e-6);
        ch.iA = c.addCurrentSource (ch.sA, -idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(idleScreenCurrent + idleSmallCurrent));
        c.setInitialGuess (vo, railPlatesNominal + rectifierResistance * total);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (ch.sB, railScreensNominal);
    }

    // ================================================================ preamp
    {
        auto b = buildPreamp (ch.pre, railLowNominal, railPreNominal);
        ch.pSrcVccLow = b.srcVccLow;
        ch.pSrcVcc = b.srcVcc;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rVolTop;
        ch.rGainBot = b.rVolBot;
        ch.rBassTop = b.rBassTop;
        ch.rBassBot = b.rBassBot;
        ch.rTrebleTop = b.rTrebleTop;
        ch.rTrebleBot = b.rTrebleBot;
        ch.rUlDirect = b.rUlDirect;
        ch.rUlIn = b.rUlIn;
        ch.rUlOut = b.rUlOut;
        ch.capUltraHi = b.capUltraHi;
        ch.rUhBypass = b.rUhBypass;
        ch.rMidA = b.rMidA;
        ch.rMidB = b.rMidB;
        ch.rCoil = b.rCoil;
        ch.grpMidL = b.grpMidL;
        ch.capMidC = b.capMidC;
        ch.pPlate1 = b.plate1;
        ch.pPlate2 = b.plate2;
        ch.pTone = b.tone;
        ch.pPlate3 = b.midPlate;
        ch.pMid = b.mid;
        ch.pFollower = b.out;
    }

    // ================================================================ power amp: V3b gain stage (global NFB on its
    // cathode), the 12AU7 long-tailed pair, four 7027As as two pairs, output transformer, speaker.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cin = c.addNode();
        const auto vpre = c.addNode(), vdrv = c.addNode(), ct = c.addNode(), neg = c.addNode();
        ch.wSrcPre = c.addSource (cin, 0.0);
        ch.wTone = cin;
        c.addResistor (cin, gnd, 1.0e6);

        // FULL reference power stage only -- reducedOrder replaces everything below with
        // behavioralPowerStage(), fitted to this same circuit (see the header + docs/circuits/AmpegV4B.md).
        if (! reducedOrder)
        {
        ch.wSrcPi = c.addSource (vpre, railPreNominal);
        ch.wSrcVdr = c.addSource (vdrv, railDriverNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcNeg = c.addSource (neg, biasSupplyVolts);

        // J5 -> R23 10k into V3b, the 12DW7's 12AX7 half (its marked 205 V plate / 1.8 V cathode is ~0.5 mA: the
        // high-mu section); 19k = R23 + the jack node's ~9k source.
        const auto gv = c.addNode(), pv = c.addNode(), kv = c.addNode(), fb = c.addNode();
        c.addResistor (cin, gv, 19.0e3);
        c.addTriode (pv, gv, kv, triode12AX7());
        c.addCapacitor (gv, pv, cgp);
        c.addResistor (vpre, pv, 220.0e3);               // R24
        c.addResistor (kv, fb, 3.3e3);                   // R25
        c.addResistor (fb, gnd, 33.0);                   // R26: the global NFB lands here
        c.setInitialGuess (pv, 205.0);
        c.setInitialGuess (kv, 1.8);
        c.setInitialGuess (fb, 0.02);
        ch.wDrvPlateA = pv;

        // V4 12AU7 long-tailed pair: C10 .01 into grid A, R29 / R27 1M leaks, grid B held at AC ground by C12 .1;
        // R28 1.5k tail; R30 / R31 47k plates; R33 820k / R32 1M from each plate back to the cathodes.
        const auto ga = c.addNode(), gb = c.addNode(), pa = c.addNode(), pb = c.addNode(), kt = c.addNode();
        c.addCapacitor (pv, ga, 0.01e-6);
        c.addResistor (ga, gnd, 1.0e6);
        c.addResistor (gb, gnd, 1.0e6);
        c.addCapacitor (gb, gnd, 0.1e-6);
        c.addTriode (pa, ga, kt, triode12AU7());
        c.addTriode (pb, gb, kt, triode12AU7());
        c.addCapacitor (ga, pa, 1.5e-12);
        c.addResistor (vdrv, pa, 47.0e3);
        c.addResistor (vdrv, pb, 47.0e3);
        c.addResistor (kt, gnd, 1.5e3);
        c.addResistor (pa, kt, 820.0e3);
        c.addResistor (pb, kt, 1.0e6);
        c.setInitialGuess (pa, 200.0);
        c.setInitialGuess (pb, 200.0);
        c.setInitialGuess (kt, 9.7);
        ch.wGridA = ga;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = kt;

        // C11 / C13 .33 -> R35 / R36 100k to the -64 V bias -> 47k stoppers per tube (23.5k per composite bank).
        const auto na = c.addNode(), nb = c.addNode(), g3 = c.addNode(), g4 = c.addNode(), pp1 = c.addNode(),
                   pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addCapacitor (pa, na, 0.33e-6);
        c.addCapacitor (pb, nb, 0.33e-6);
        c.addResistor (na, neg, 100.0e3);
        c.addResistor (nb, neg, 100.0e3);
        c.addResistor (na, g3, 23.5e3);
        c.addResistor (nb, g4, 23.5e3);
        c.addCapacitor (g3, gnd, 330.0e-12);
        c.addCapacitor (g4, gnd, 330.0e-12);
        ch.wPowerGridA = g3;
        ch.penA = c.addPentode (pp1, g3, gnd, pentode7027APair(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4, gnd, pentode7027APair(), railScreensNominal);

        c.addCapacitor (pp1, pp2, 250.0e-12);            // primary distributed capacitance
        c.addResistor (pp1, pp2, 100.0e3);               // core/copper losses
        c.addCapacitor (pp1, gnd, 300.0e-12);
        c.addCapacitor (pp2, gnd, 300.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12,  feedbackSense * mps,
                                 m12, lh,  -feedbackSense * mps,
                                 feedbackSense * mps, -feedbackSense * mps, ls });
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

        // Global NFB: R34 4.7k || C14 .001 from the 8 ohm tap onto R26. The modelled winding is the 4 ohm tap, whose
        // volts are 1/sqrt(2) of the 8 ohm tap's: the same current comes from R34 / sqrt(2) || C14 * sqrt(2).
        ch.rFeedback = c.addResistor (ch.wOut, fb, feedbackResistor);
        c.addCapacitor (ch.wOut, fb, 0.001e-6 * std::sqrt (2.0));
        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (na, biasSupplyVolts);
        c.setInitialGuess (nb, biasSupplyVolts);
        c.setInitialGuess (g3, biasSupplyVolts);
        c.setInitialGuess (g4, biasSupplyVolts);
        }
    }
}

double AmpegV4BStyleAmplifierProcessor::sagRail (double envelope) const noexcept
{
    // Sag fitted to the reference netlist's supply droop: up to ~10 % into full drive.
    return railPlatesNominal * juce::jlimit (0.80, 1.0, 1.0 - 0.010 * envelope);
}

double AmpegV4BStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage,
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
    ch.bmOutput = ch.bmTail.process (ch.bmToneState + bmShelfHfGain * (rawAc - ch.bmToneState));
    return ch.bmOutput;
}

void AmpegV4BStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 2, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void AmpegV4BStyleAmplifierProcessor::applyMidFreq (Channel& ch, int index) const
{
    const int i = juce::jlimit (0, 2, index);
    ch.pre.setInductorInverse (ch.grpMidL, 1.0 / midInductance[i]);
    ch.pre.setCapacitance (ch.capMidC, midCapacitance[i]);
    ch.pre.setResistance (ch.rCoil, midCoilR[i]);
}

void AmpegV4BStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Volume VR101 1M LINEAR; Bass VR105 / Treble VR103 1M log; Mid-Range VR104 50k linear (up = toward the boost end).
    const double gainBot = juce::jmax (1.0, 1.0e6 * k.gain);
    const double bassBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.treble));
    const double midA = juce::jmax (1.0, 50.0e3 * (1.0 - k.middle));
    const double midB = juce::jmax (1.0, 50.0e3 * k.middle);
    const bool ultraLo = k.ultraLo > 0.5, ultraHi = k.ultraHi > 0.5;

    // Bias: the -64 V rail, -71 (cold) .. -57 V (hot), noon at the drawing's -64.
    const double biasV = -(71.0 - 14.0 * k.bias);

    // Tube Feel: the supply's series resistance (sag) and the feedback amount. 1 = the real amp.
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride
                                                  : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBot));
        ch.pre.setResistance (ch.rGainBot, gainBot);
        ch.pre.setResistance (ch.rBassTop, juce::jmax (1.0, 1.0e6 - bassBot));
        ch.pre.setResistance (ch.rBassBot, bassBot);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 1.0e6 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rMidA, midA);
        ch.pre.setResistance (ch.rMidB, midB);
        ch.pre.setResistance (ch.rUlDirect, ultraLo ? 1.0e12 : 1.0);
        ch.pre.setResistance (ch.rUlIn, ultraLo ? 1.0 : 1.0e12);
        ch.pre.setResistance (ch.rUlOut, ultraLo ? 1.0 : 1.0e12);
        ch.pre.setCapacitance (ch.capUltraHi, ultraHi ? 500.0e-12 : 1.0e-12);
        ch.pre.setResistance (ch.rUhBypass, ultraHi ? 47.0e3 : 1.0);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcNeg, biasV);
            ch.power.setResistance (ch.rFeedback, feedbackR);
        }
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! reducedOrder && ! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        if (k.midFreq != appliedMidFreq)
            applyMidFreq (ch, k.midFreq);
    }
    appliedSpeaker = k.speaker;
    appliedMidFreq = k.midFreq;
    // Heavier loads take fewer volts; compensate so 2 / 4 / 8 ohm changes the sound, not the loudness.
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 4.0, -0.8);
}

void AmpegV4BStyleAmplifierProcessor::recover (Channel& ch) const
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
    trackSmallRails (ch, 0.0);
}

void AmpegV4BStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    // Four 7027As can draw over an amp at full drive; a solver excursion must not wreck the shared rails.
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 2.0, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.4, ch.sumScreen / n + idleSmallCurrent));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    ch.vScreen = rail (ch.sB, 700.0);
    trackSmallRails (ch, (double) supplyInterval / juce::jmax (1.0, sampleRate));
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 800.0));
        ch.power.setSource (ch.wSrcPi, ch.preRail);
        ch.power.setSource (ch.wSrcVdr, ch.driverRail);
    }
    ch.pre.setSource (ch.pSrcVcc, ch.preRail);
    ch.pre.setSource (ch.pSrcVccLow, ch.lowRail);
}

void AmpegV4BStyleAmplifierProcessor::trackSmallRails (Channel& ch, double dt) const
{
    // R53 4.7k -> 360 V -> R52 4.7k -> 325 V -> R51 2.2k -> V1 / V2, each on a 40 uF section: the drawing's marked
    // ratios to the screen node, followed at the RC rate (dt <= 0 snaps them).
    const double scale = ch.vScreen / railScreensNominal;
    const auto follow = [dt] (double& v, double target)
    {
        v = dt <= 0.0 ? target : v + (1.0 - std::exp (-dt / (4.7e3 * 40.0e-6))) * (target - v);
    };
    follow (ch.driverRail, railDriverNominal * scale);
    follow (ch.preRail, railPreNominal * scale);
    follow (ch.lowRail, railLowNominal * scale);
}

double AmpegV4BStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::firstPlate: return ch.pre.voltage (ch.pPlate1);
        case Probe::secondPlate: return ch.pre.voltage (ch.pPlate2);
        case Probe::toneStackOut: return ch.pre.voltage (ch.pTone);
        case Probe::recoveryPlate: return ch.pre.voltage (ch.pPlate3);
        case Probe::midNode: return ch.pre.voltage (ch.pMid);
        case Probe::followerOut: return ch.pre.voltage (ch.pFollower);
        case Probe::phaseInverterGrid: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return reducedOrder ? 0.0 : ch.power.voltage (ch.wTail);
        case Probe::driverPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wDrvPlateA);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPowerGridA);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double AmpegV4BStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void AmpegV4BStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void AmpegV4BStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double AmpegV4BStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double AmpegV4BStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void AmpegV4BStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        auto bmVoicing = voicings::bassAmp;
        bmVoicing.asym = 0.0; // even-order content already lives in the knee's dcShift law
        bmVoicing.cabLpHz = 6000.0;  // fitted against the netlist: the drawing's 6K11 mid stage keeps more top
        bmVoicing.cabAirHz = 8000.0;
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, bmVoicing, 1.0 / outputScale);
    }

    smoothedGain.reset (newSampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (gainParam->get());
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

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + idleSmallCurrent;
    appliedSpeaker = 1;   // the circuits are built with the 4 ohm speaker
    appliedMidFreq = 1;   // ... and the 800 Hz mid tap
    updatePots ({ (double) inputParam->get(), (double) gainParam->get(), (double) ultraLoParam->get(),
                  (double) ultraHiParam->get(), (double) bassParam->get(), (double) middleParam->get(),
                  (double) midFreqParam->get(), (double) trebleParam->get(), (double) masterParam->get(),
                  (double) powerParam->get(), (double) biasParam->get(), (double) tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        // Supply, then the amplifier on its rails, then the supply again with the amplifier's own idle currents.
        const double supplyRate = newSampleRate / (double) supplyInterval;
        double ip = idlePlateCurrent, is = idleScreenCurrent;
        for (int pass = 0; pass < 6; ++pass)
        {
            ch.supply.setCurrentSource (ch.iA, -ip);
            ch.supply.setCurrentSource (ch.iB, -(is + idleSmallCurrent));
            idleSupplyCurrent = ip + is + idleSmallCurrent;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance
                                                * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
            bool passOk = ch.supply.prepare (supplyRate);
            ch.vScreen = ch.supply.voltage (ch.sB);
            trackSmallRails (ch, 0.0);
            ch.pre.setSource (ch.pSrcVcc, ch.preRail);
            ch.pre.setSource (ch.pSrcVccLow, ch.lowRail);
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcPi, ch.preRail);
                ch.power.setSource (ch.wSrcVdr, ch.driverRail);
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

void AmpegV4BStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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

    smoothedGain.setTargetValue (gainParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int midFreqChoice = juce::roundToInt (midFreqParam->get());
    const double inputGain = juce::roundToInt (inputParam->get()) == 0 ? 1.0 : 0.1778; // synthetic 0 / -15 dB pad
    const double ultraLoOn = ultraLoParam->get() > 0.5f ? 1.0 : 0.0;
    const double ultraHiOn = ultraHiParam->get() > 0.5f ? 1.0 : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float ga = smoothedGain.getNextValue();
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
            updatePots ({ inputGain, (double) ga, ultraLoOn, ultraHiOn, (double) ba, (double) mi,
                          (double) midFreqChoice, (double) tr, (double) ma, (double) pw, (double) bi,
                          (double) fe, speakerChoice });
        }

        // Power Drive and Master (synthetic, linear -- the old 50k divider) both scale the Ext Amp jack into V3b.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw)) * (double) ma;

        // Output control: -30 dB .. 0 dB at noon .. +12 dB.
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

            ch.power.setSource (ch.wSrcPre, masterGain * ch.pre.voltage (ch.pFollower));
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

            // Same sanity gate as the other amps: a converged solve can still land on a state no amplifier
            // reaches (a speaker terminal at hundreds of volts); such a sample is a failure and the output
            // holds its last value instead of printing the excursion.
            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.power.voltage (ch.wTone), (double) bi, (double) fe)
                                                     : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 250.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));
            if (chIdx == 0 && ! sane && okPre && ok2)
            {
                ++sanityRejects;
                if (std::isfinite (speakerVolts))
                    worstRejectedVolts = juce::jmax (worstRejectedVolts, std::abs (speakerVolts));
            }

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

void AmpegV4BStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
