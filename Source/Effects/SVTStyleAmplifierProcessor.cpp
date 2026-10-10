#include "SVTStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/AmpegSVT.md, "What the SVT-CL actually is") ----
    constexpr double railPlatesNominal = 690.0;   // output-transformer centre tap (published range 660-695)
    constexpr double railScreensNominal = 365.0;  // +365 V: screens (via 220 ohm) and the two 12AU7 drivers
    constexpr double railPreampNominal = 345.0;   // +345 V: preamp and the phase splitter's plates
    constexpr double rectifierResistance = 70.0;  // the big SVT sags less than a small amp; ~7% at full drive
    constexpr double rectifierResistance2 = 200.0; // the 365 V rail's own winding (series resistance)
    constexpr double screenResistor = 220.0;      // per bank, schematic R7-R12 220 ohm shared per side
    constexpr double idlePlateCurrent = 0.33;     // six 6550 at ~55 mA
    constexpr double idleScreenCurrent = 0.020;
    constexpr double idleDriverCurrent = 0.010;   // two 12AU7 driver stages at ~5 mA each... combined
    constexpr double idlePreampCurrent = 0.0024;  // the marked +345 V is the LOADED rail: (365-345)/8.2k
    constexpr double railDriverReturn = -180.0;   // regulated-ish negative winding the level-shifters return to

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7()
    {
        return {}; // Koren's ECC83 set
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

    /** The PI's own 12AX7, kg1 softened 24x -- the same cure the AC30, JCM800, SLO100 and Deluxe
        Reverb references needed. At the published fit this power block flops rail-to-rail in a
        ~2 Hz limit cycle on silence: the level-shifter path is DC-coupled, so forward gain does
        not collapse at LF the way a cap-coupled amp's does, and any whisper through the NFB
        return re-triggers the hop (opening the loop is the only thing that silences it --
        measured rms ~0.4 closed vs ~0.0003 open). Audible as rumble/"DC" at Master = 0 and a
        thump when the knob moves. The LTP is the metastable element, and its kg1 softening is
        free: the pair is tail-fed and self-biased, so the DC points hold while the loop's
        incremental gain drops below what sustains the hop -- most of the fix; the rest is
        keeping the loop's LF poles sub-Hz so the feedback stays degenerative at the flop
        frequency (see C8/C11 below). The 6550s keep the published set --
        they are fixed-bias at -45 V, so kg1 softening would starve the ~0.3 A idle they owe,
        and Gg/kp moved the flop the wrong way. */
    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        p.kg1 *= 24.0;
        return p;
    }

    KorenPentode::Parameters pentode6550Triple()
    {
        // Koren's published 6550 set (mu 7.9, Ex 1.35, Kg1 890, Kg2 4200, Kp 60, Kvb 24). Three identical
        // tubes on the same nodes are one tube with three times the current: Kg1, Kg2 (and grid current)
        // divide by three -- same pattern as JCM800StyleAmplifierProcessor's pentodeEL34Pair().
        KorenPentode::Parameters p;
        p.mu = 7.9;
        p.ex = 1.35;
        p.kg1 = 890.0 / 3.0;
        p.kg2 = 4200.0 / 3.0;
        p.kp = 60.0;
        p.kvb = 24.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate Miller capacitance

    // Output transformer: ~600 ohm plate-to-plate, 4 ohm secondary (estimate -- see the doc's "Honest
    // simplifications"). Two half-primaries + secondary, wound as in the Bassman's OT.
    constexpr double primaryHalfInductance = 15.0;        // H per half (60 H plate to plate -- a 300 W core)
    constexpr double halfToSecondaryTurns = 6.12;         // sqrt(150/4): one half-primary is 150 ohm, secondary 4
    constexpr double couplingHalves = 0.9995;
    constexpr double couplingSecondary = 0.9990;
    constexpr double primaryHalfResistance = 15.0;
    constexpr double secondaryResistance = 0.05;
    constexpr double feedbackResistor = 150.0e3;          // estimate: ~8 dB loop (marked "FEEDBACK" on the
                                                        // schematic, value illegible -- see the doc)

    // Mid trap: the tapped inductor L1 (800/300/100 mH) with a different series capacitor per position,
    // reproducing the published 220 / 800 / 3000 Hz resonances.
    constexpr double midInductance[3] = { 0.800, 0.300, 0.100 };
    constexpr double midCapacitance[3] = { 0.68e-6, 0.15e-6, 0.033e-6 };

    constexpr double speakerNominal[3] = { 2.0, 4.0, 8.0 };

    constexpr double powerTheta = 0.9; // same reasoning as the other amps: damped integrator for the stiff power block
}

SVTStyleAmplifierProcessor::SVTStyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "svt_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "0 dB" : "-15 dB");
        }));
    auto gain = std::make_unique<juce::AudioParameterFloat> (
        "svt_gain", "Gain", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto ultraLo = std::make_unique<juce::AudioParameterFloat> (
        "svt_ultra_lo", "Ultra Lo", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto ultraHi = std::make_unique<juce::AudioParameterFloat> (
        "svt_ultra_hi", "Ultra Hi", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (juce::roundToInt (v) == 0 ? "Off" : "On");
        }));
    auto bass = std::make_unique<juce::AudioParameterFloat> (
        "svt_bass", "Bass", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto middle = std::make_unique<juce::AudioParameterFloat> (
        "svt_middle", "Middle", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto midFreq = std::make_unique<juce::AudioParameterFloat> (
        "svt_mid_freq", "Mid Freq", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            switch (juce::roundToInt (v)) { case 0: return juce::String ("220 Hz"); case 2: return juce::String ("3000 Hz"); default: return juce::String ("800 Hz"); }
        }));
    auto treble = std::make_unique<juce::AudioParameterFloat> (
        "svt_treble", "Treble", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto master = std::make_unique<juce::AudioParameterFloat> (
        "svt_master", "Master", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "svt_output", "Output", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto power = std::make_unique<juce::AudioParameterFloat> (
        "svt_power", "Power Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto bias = std::make_unique<juce::AudioParameterFloat> (
        "svt_bias", "Bias", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto feel = std::make_unique<juce::AudioParameterFloat> (
        "svt_tube_feel", "Tube Feel", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "svt_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 1.0f,
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
        "svt", "SVT-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (gain));
    group->addChild (std::move (ultraLo));
    group->addChild (std::move (ultraHi));
    group->addChild (std::move (bass));
    group->addChild (std::move (middle));
    group->addChild (std::move (midFreq));
    group->addChild (std::move (treble));
    group->addChild (std::move (master));
    // Page 2 (a sub-group: see EffectProcessor::getParameterPages()).
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("svt_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcVcc = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0, rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0;
        int rMasterTop = 0, rMasterBot = 0, rMid = 0, rUltraLoA = 0, rUltraLoB = 0, capUltraHi = 0;
        int grpMidL = 0, capMidC = 0;
        NodalCircuit::Node plate1 = 0, plate2 = 0, tone = 0, plate3 = 0, mid = 0, out = 0;
    };

    /** The whole tube preamp: two gain stages around the Gain pot and Ultra-Lo network, the Baxandall-style
        bass/treble section, the recovery stage, the tapped-inductor mid trap, the Master and the output
        cathode follower. Netlist from the SVT-CL preamp schematic (07S519); the NE5532 buffer is left to
        inputLimit() (see the doc's simplifications). */
    PreampBuild buildPreamp (NodalCircuit& c, double vcc0)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc = c.addNode(), in = c.addNode();
        b.srcVcc = c.addSource (vcc, vcc0);
        b.srcIn = c.addSource (in, 0.0);

        // V1:B -- first gain stage (plate ~235 V on the schematic).
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, g1, 22.0e3 + 100.0e3);       // R1 22k + R3 100k in series
        c.addResistor (g1, gnd, 3.3e6);                  // R4
        c.addTriode (p1, g1, k1, triode12AX7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vcc, p1, 100.0e3);                // R7
        c.addResistor (k1, gnd, 1.5e3);                  // R8
        c.addCapacitor (k1, gnd, 22.0e-6);               // C2
        c.setInitialGuess (g1, 0.0);
        c.setInitialGuess (k1, 1.4);
        c.setInitialGuess (p1, 235.0);

        // V1:B plate -> C3 -> the Ultra-Lo / bright network -> the Gain pot.
        const auto na = c.addNode(), u = c.addNode(), gt = c.addNode(), gw = c.addNode();
        c.addCapacitor (p1, na, 0.1e-6);                 // C3
        c.addResistor (na, gnd, 220.0e3);                // R19
        c.addCapacitor (na, gnd, 56.0e-12);              // C4
        c.addResistor (na, gt, 100.0e3);                 // R11
        b.rUltraLoA = c.addResistor (na, u, 1.0e9);      // SW1:B open: R20 330k when engaged
        b.rUltraLoB = c.addResistor (u, gt, 1.0e9);      // R21 220k when engaged
        c.addCapacitor (u, gnd, 0.0047e-6);              // C8
        c.addResistor (u, gnd, 3.3e6);                   // R22
        b.rGainTop = c.addResistor (gt, gw, 500.0e3);    // P1 1M audio
        b.rGainBot = c.addResistor (gw, gnd, 500.0e3);
        c.setInitialGuess (na, 0.0);

        // V1:A -- second gain stage (plate ~240 V).
        const auto g2 = c.addNode(), p2 = c.addNode(), k2 = c.addNode(), kb = c.addNode();
        c.addCapacitor (gw, g2, 0.1e-6);                 // C9
        c.addResistor (g2, gnd, 1.0e6);                  // R26
        c.addTriode (p2, g2, k2, triode12AX7());
        c.addCapacitor (g2, p2, cgp);
        c.addResistor (vcc, p2, 100.0e3);                // R27
        c.addResistor (k2, kb, 1.5e3);                   // R28
        c.addResistor (kb, gnd, 1.5e3);                  // R29
        c.addCapacitor (kb, gnd, 0.033e-6);              // C10
        c.setInitialGuess (k2, 1.4);
        c.setInitialGuess (p2, 240.0);

        // Baxandall-style bass/treble section: C11 into the network, P2 bass leg, C23 treble feed, P4 treble
        // pot whose wiper is the output (docs/circuits/AmpegSVT.md has the ASCII netlist).
        const auto ti = c.addNode(), t1 = c.addNode(), w1 = c.addNode(), nb = c.addNode(),
                   ns = c.addNode(), t2 = c.addNode(), wt = c.addNode(), tb = c.addNode();
        c.addCapacitor (p2, ti, 0.1e-6);                 // C11
        c.addResistor (ti, t1, 220.0e3);                 // R48
        b.rBassTop = c.addResistor (t1, w1, 500.0e3);    // P2 1M audio: top half
        b.rBassBot = c.addResistor (w1, nb, 500.0e3);    // bottom half
        c.addResistor (nb, gnd, 22.0e3);                 // R37
        c.addCapacitor (w1, ns, 0.001e-6);               // C20
        b.capUltraHi = c.addCapacitor (w1, ns, 1.0e-12); // C21 .01, switched in by ULTRA HIGH (off = ~open)
        c.addResistor (ns, t2, 100.0e3);                 // R30
        c.addCapacitor (ti, t2, 470.0e-12);              // C23 treble feed
        b.rTrebleTop = c.addResistor (t2, wt, 500.0e3);  // P4 1M audio
        b.rTrebleBot = c.addResistor (wt, tb, 500.0e3);
        c.addCapacitor (tb, gnd, 0.0047e-6);             // C27
        b.tone = wt;
        c.setInitialGuess (ti, 0.0);

        // V2:B -- recovery gain after the (lossy) tone section (plate ~240 V).
        const auto g3 = c.addNode(), p3 = c.addNode(), k3 = c.addNode(), kb3 = c.addNode(), n16 = c.addNode();
        c.addResistor (wt, n16, 100.0e3);                // R32
        c.addCapacitor (n16, g3, 0.1e-6);                // C16
        c.addResistor (g3, gnd, 470.0e3);                // R40
        c.addTriode (p3, g3, k3, triode12AX7());
        c.addCapacitor (g3, p3, cgp);
        c.addResistor (vcc, p3, 100.0e3);                // R43
        c.addResistor (k3, kb3, 1.5e3);                  // R44
        c.addResistor (kb3, gnd, 2.2e3);                 // R45
        c.addCapacitor (kb3, gnd, 10.0e-6);              // C12
        c.setInitialGuess (k3, 1.2);
        c.setInitialGuess (p3, 240.0);
        b.plate3 = p3;

        // The mid trap: C17 into a series (P3 Mid) -> C_sel -> L_sel -> ground branch off the plate node,
        // plus the R12/R13 bias string the schematic marks "8V".
        const auto nm = c.addNode(), mt = c.addNode(), mtp = c.addNode(), nmc = c.addNode(),
                   nmb = c.addNode(), mtop = c.addNode();
        c.addCapacitor (p3, nm, 0.1e-6);                 // C5
        c.addResistor (vcc, nm, 100.0e3);                // R12
        c.addResistor (nm, nmb, 8.2e3);                  // R13
        c.addCapacitor (nm, mt, 0.033e-6);               // C17
        c.addResistor (mt, gnd, 470.0e3);                // trap node DC leak (the 470k legs on the schematic)
        b.rMid = c.addResistor (mt, mtp, 25.0e3);        // P3 50k linear, rheostat
        b.capMidC = c.addCapacitor (mtp, nmc, midCapacitance[1]);
        b.grpMidL = c.addCoupledInductors ({ { nmc, gnd } }, { midInductance[1] });
        b.mid = nm;
        c.setInitialGuess (nm, 200.0);

        // Master: nm -> R13 -> R36 470 -> P5 top; wiper drives V2:A's grid through C6.
        const auto mw = c.addNode(), g4 = c.addNode();
        c.addResistor (nmb, mtop, 470.0);                // R36
        b.rMasterTop = c.addResistor (mtop, mw, 25.0e3); // P5 50k linear
        b.rMasterBot = c.addResistor (mw, gnd, 25.0e3);
        c.addCapacitor (mw, g4, 0.1e-6);                 // C6

        // V2:A -- output cathode follower (plate to rail, cathode ~100 V into R15+R16). R14 returns
        // to the kx tap, not ground: bootstrapped self-bias holds the grid a couple volts under the
        // cathode. Returned to ground the stage idles with the cathode near 5 V instead of ~100 V,
        // parked on the grid-current knee.
        const auto p4 = c.addNode(), k4 = c.addNode(), kx = c.addNode(), po = c.addNode();
        c.addResistor (g4, kx, 220.0e3);                 // R14
        c.addTriode (p4, g4, k4, triode12AX7());
        c.addCapacitor (g4, p4, cgp);
        c.addResistor (vcc, p4, 1.0e-3);                 // plate straight to +345 V
        c.addResistor (k4, kx, 4.7e3);                   // R15
        c.addResistor (kx, gnd, 220.0e3);                // R16
        c.addCapacitor (k4, po, 0.68e-6);                // C26
        c.addResistor (po, gnd, 1.0e6);                  // R33
        c.setInitialGuess (p4, 345.0);
        c.setInitialGuess (k4, 100.0);
        b.out = po;

        b.plate1 = p1;
        b.plate2 = p2;
        return b;
    }
}

void SVTStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        const auto vo2 = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();

        const double dcDrop = rectifierResistance * (idlePlateCurrent + idleScreenCurrent);
        ch.srcVoc = c.addSource (vo, railPlatesNominal + dcDrop);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 110.0e-6);           // the two big cans at the plate node

        const double dcDrop2 = rectifierResistance2 * (idleScreenCurrent + idleDriverCurrent + idlePreampCurrent);
        ch.srcVoc2 = c.addSource (vo2, railScreensNominal + dcDrop2);
        ch.rRect2 = c.addResistor (vo2, ch.sB, rectifierResistance2);
        c.addCapacitor (ch.sB, gnd, 100.0e-6);

        c.addResistor (ch.sB, ch.sC, 8.2e3);             // R49 8.2k 15W drop to the +345 preamp/PI rail
        c.addCapacitor (ch.sC, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -idlePlateCurrent);
        ch.iB = c.addCurrentSource (ch.sB, -(idleScreenCurrent + idleDriverCurrent));
        ch.iC = c.addCurrentSource (ch.sC, -idlePreampCurrent);
        c.setInitialGuess (vo, railPlatesNominal + dcDrop);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (vo2, railScreensNominal + dcDrop2);
        c.setInitialGuess (ch.sB, railScreensNominal);
        c.setInitialGuess (ch.sC, railPreampNominal);
    }

    // ================================================================ preamp
    {
        auto b = buildPreamp (ch.pre, railPreampNominal);
        ch.pSrcVcc = b.srcVcc;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.rBassTop = b.rBassTop;
        ch.rBassBot = b.rBassBot;
        ch.rTrebleTop = b.rTrebleTop;
        ch.rTrebleBot = b.rTrebleBot;
        ch.rMasterTop = b.rMasterTop;
        ch.rMasterBot = b.rMasterBot;
        ch.rMid = b.rMid;
        ch.rUltraLoA = b.rUltraLoA;
        ch.rUltraLoB = b.rUltraLoB;
        ch.capUltraHi = b.capUltraHi;
        ch.grpMidL = b.grpMidL;
        ch.capMidC = b.capMidC;
        ch.pPlate1 = b.plate1;
        ch.pPlate2 = b.plate2;
        ch.pTone = b.tone;
        ch.pPlate3 = b.plate3;
        ch.pMid = b.mid;
        ch.pFollower = b.out;
    }

    // ================================================================ power amp: phase splitter, drivers,
    // six 6550s as two triples, output transformer, speaker, global negative feedback.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cin = c.addNode();
        const auto vpi = c.addNode(), vdr = c.addNode(), ct = c.addNode(), neg = c.addNode();
        ch.wSrcPre = c.addSource (cin, 0.0);
        ch.wSrcPi = c.addSource (vpi, railPreampNominal);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcNeg = c.addSource (neg, railDriverReturn);
        ch.wSrcVdr = c.addSource (vdr, railScreensNominal);

        // Phase splitter: 12AX7 long-tailed pair, cathodes -> R14 220 -> bn -> R13 47k -> -180 V;
        // grid leaks return to bn (the tail's DC reference). Plates ~200/250 V on the schematic.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(),
                   k = c.addNode(), bn = c.addNode();
        // The preamp feeds the first grid through a coupling cap -- without it the 1k stopper would hold
        // g1 at the source's 0 V while g2 floats to bn through its leak, unbalancing the pair on silence.
        const auto g7 = c.addNode();
        c.addCapacitor (cin, g7, 0.1e-6);                // coupling cap
        c.addResistor (g7, g1, 1.0e3);                   // R7 grid stopper
        c.addResistor (g1, bn, 470.0e3);                 // R8 leak (returns to the tail node)
        c.addResistor (g2, bn, 470.0e3);
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 100.0e3);                // R12
        c.addResistor (vpi, pb, 68.0e3);                 // R15
        c.addResistor (k, bn, 220.0);                    // R14
        c.addResistor (bn, neg, 47.0e3);                 // R13 to the -180 V rail
        c.setInitialGuess (pa, 200.0);
        c.setInitialGuess (pb, 250.0);
        c.setInitialGuess (k, -42.0);
        c.setInitialGuess (bn, -43.0);
        c.setInitialGuess (g1, -43.0);
        c.setInitialGuess (g2, -43.0);
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = bn;

        // Drivers: 12AU7 common-cathode stages, plates at ~+220 V through 47k/5W to +365, cathodes 1.8k
        // with C9 between them. Each plate feeds a ~600k resistive level-shifter whose tap lands the
        // output grids at ~-45 V -- the bias IS the divider's DC operating point.
        const auto gd1 = c.addNode(), gd2 = c.addNode(), pd1 = c.addNode(), pd2 = c.addNode(),
                   kd1 = c.addNode(), kd2 = c.addNode(), m1 = c.addNode(), m2 = c.addNode(),
                   nab = c.addNode(), nbb = c.addNode(),
                   gs1 = c.addNode(), gs2 = c.addNode();
        // C8/C11 are much larger than the schematic estimate for a reason, not fidelity to
        // the printed value: every high-pass pole in the feedback loop adds up to +90 deg of
        // phase LEAD below its corner, and a stack of them at audio-band corners flips the
        // global NFB positive at the ~0.25 Hz the motorboat relaxation runs at. Pushing all
        // the loop's LF poles far below the output transformer's own LF corner keeps the
        // feedback degenerative down there, so the ~1 s kick from a driven input decays
        // instead of re-triggering. (Real SVTs ship electrolytics here, not film caps.)
        c.addCapacitor (pa, gs1, 2.2e-6);               // C8
        c.addCapacitor (pb, gs2, 2.2e-6);               // C11
        // Grid stoppers on the driver grids (same value the 6550s get): they limit how hard
        // a kick from the PI can slam the 12AU7 grids into conduction and charge C8/C11
        // asymmetrically -- one of the hysteresis paths the relaxation can ride on.
        c.addResistor (gs1, gd1, 47.0e3);
        c.addResistor (gs2, gd2, 47.0e3);
        c.addResistor (gd1, gnd, 470.0e3);               // R19
        c.addResistor (gd2, gnd, 470.0e3);               // R27
        c.addTriode (pd1, gd1, kd1, triode12AU7());
        c.addTriode (pd2, gd2, kd2, triode12AU7());
        c.addCapacitor (gd1, pd1, 2.2e-12);
        c.addCapacitor (gd2, pd2, 2.2e-12);
        c.addResistor (vdr, pd1, 47.0e3);                // R21 5W
        c.addResistor (vdr, pd2, 47.0e3);                // R29 5W
        c.addResistor (kd1, gnd, 1.8e3);                 // R20
        c.addResistor (kd2, gnd, 1.8e3);                 // R28
        c.addCapacitor (kd1, kd2, 1.0e-6);               // C9
        c.setInitialGuess (pd1, 220.0);
        c.setInitialGuess (pd2, 220.0);
        c.setInitialGuess (kd1, 5.5);
        c.setInitialGuess (kd2, 5.5);
        ch.wDrvPlateA = pd1;

        // Level-shifters: plate -> 300k -> 120k -> tap -> trim+tails -> -180 V. With the driver plates
        // near +170-220 V the divider wants ~265k below the tap to land the 6550 grids at the marked ~-45 V.
        c.addResistor (pd1, m1, 300.0e3);                // R22 + R23
        c.addResistor (m1, nab, 120.0e3);                // R24
        ch.rBiasTapA = c.addResistor (nab, neg, 265.0e3); // P2 trim + R25 + R32 (the tap point: ~-45 V)
        c.addResistor (pd2, m2, 300.0e3);                // R30 + R31 side
        c.addResistor (m2, nbb, 120.0e3);                // R33-side
        ch.rBiasTapB = c.addResistor (nbb, neg, 265.0e3);
        // A 0.1 uF bypass ON THE TAP (an earlier Newton aid, justified as the bias network's
        // electrolytics) is wrong: at audio it is a ~10k shunt that collapses the divider's ~0.31
        // transfer to ~0.02 -- -33 dB on the output grids, i.e. the amp's missing level. The real
        // amp's electrolytics sit on the bias supply, an ideal source here (already AC ground).
        c.setInitialGuess (m1, 100.0);
        c.setInitialGuess (m2, 100.0);
        c.setInitialGuess (nab, -45.0);
        c.setInitialGuess (nbb, -45.0);

        // Output stage: each bank of three 6550s is one composite pentode; the grid stoppers (47k per tube,
        // schematic tube-board R1-R6) collapse to one 47k per bank in the composite model.
        const auto g3 = c.addNode(), g4 = c.addNode(), pp1 = c.addNode(), pp2 = c.addNode(),
                   a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (nab, g3, 47.0e3);
        c.addResistor (nbb, g4, 47.0e3);
        // The capacitance that does belong in this signal path is the 6550s' input capacitance on
        // the grid side of the 47k stoppers: ~3 x 27 pF + socket strays. It also keeps a linear
        // capacitor state on the node where grid-current onset actually lands, serving the same
        // Newton-smoothing role as the misplaced tap bypass did.
        c.addCapacitor (g3, gnd, 330.0e-12);
        c.addCapacitor (g4, gnd, 330.0e-12);
        ch.wPowerGridA = g3;
        ch.penA = c.addPentode (pp1, g3, gnd, pentode6550Triple(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4, gnd, pentode6550Triple(), railScreensNominal);

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
        // Secondary polarity is chosen so the global feedback returned to the second PI grid is
        // NEGATIVE. With the opposite winding sense the 150k NFB path becomes positive feedback:
        // through the 0.1uF feedback cap the loop sustains a ~9 Hz relaxation oscillation (classic
        // motorboating) that buries the output under low rumble -- measured on a zero-signal input
        // 2026-10-09 and fixed by this sign choice.
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

        // Negative feedback: from the speaker terminal into the second PI grid (no presence control on the
        // SVT faceplate -- a fixed network), with the same stray-capacitance pole as the other amps.
        const auto fp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        c.addCapacitor (fp, g2, 2.2e-6);               // same "poles well below the OT corner" rule as C8/C11
        c.addCapacitor (fp, gnd, 1.5e-9);
        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (g3, -45.0);
        c.setInitialGuess (g4, -45.0);
        c.setInitialGuess (nab, -45.0);
        c.setInitialGuess (nbb, -45.0);
    }
}

void SVTStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
{
    const auto sm = speakerModel (speakerNominal[juce::jlimit (0, 2, index)]);
    ch.power.setResistance (ch.rSpkRe, sm.re);
    ch.power.setResistance (ch.rSpkRp, sm.rp);
    ch.power.setCapacitance (ch.capSpkCp, sm.cp);
    ch.power.setInductorInverse (ch.grpSpkLe, 1.0 / sm.le);
    ch.power.setInductorInverse (ch.grpSpkLp, 1.0 / sm.lp);
}

void SVTStyleAmplifierProcessor::applyMidFreq (Channel& ch, int index) const
{
    const int i = juce::jlimit (0, 2, index);
    ch.pre.setInductorInverse (ch.grpMidL, 1.0 / midInductance[i]);
    ch.pre.setCapacitance (ch.capMidC, midCapacitance[i]);
}

void SVTStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Gain pot P1 1M audio; Bass P2 and Treble P4 are 1M audio used as dividers; Mid P3 is a 50k linear
    // rheostat in the trap; Master P5 is 50k linear.
    const double gainBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
    const double bassBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.treble));
    const double midR = juce::jmax (1.0, 50.0e3 * k.middle);
    const double masterBot = juce::jmax (1.0, 50.0e3 * k.master);

    // Ultra-Lo engages the R20 330k / R21 220k scoop divider; Ultra-Hi adds C21 .01 across the bass wiper leg.
    const double ulA = k.ultraLo > 0.5 ? 330.0e3 : 1.0e9;
    const double ulB = k.ultraLo > 0.5 ? 220.0e3 : 1.0e9;
    const double uhC = k.ultraHi > 0.5 ? 0.01e-6 : 1.0e-12;

    // Bias: the level-shifter tap resistor; ~265k lands the grids near -45 V. The knob sweeps the bias
    // trims' range: more resistance pulls the tap toward the driver plates = hotter (less negative).
    const double biasR = 180.0e3 + 170.0e3 * k.bias; // 180k (cold) .. 350k (hot), noon ~265k -> ~-45 V

    // Tube Feel: the supply's series resistance (sag) and the feedback amount. 1 = the real amp.
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double rectifier2 = rectifierResistance2 * (0.05 + 0.95 * k.tubeFeel);
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
        ch.pre.setResistance (ch.rMid, midR);
        ch.pre.setResistance (ch.rMasterTop, juce::jmax (1.0, 50.0e3 - masterBot));
        ch.pre.setResistance (ch.rMasterBot, masterBot);
        ch.pre.setResistance (ch.rUltraLoA, ulA);
        ch.pre.setResistance (ch.rUltraLoB, ulB);
        ch.pre.setCapacitance (ch.capUltraHi, uhC);
        ch.power.setResistance (ch.rBiasTapA, biasR);
        ch.power.setResistance (ch.rBiasTapB, biasR);
        ch.power.setResistance (ch.rFeedback, feedbackR);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setResistance (ch.rRect2, rectifier2);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        if (k.midFreq != appliedMidFreq)
            applyMidFreq (ch, k.midFreq);
    }
    appliedSpeaker = k.speaker;
    appliedMidFreq = k.midFreq;
    // Heavier loads take fewer volts; compensate so 2 / 4 / 8 ohm changes the sound, not the loudness.
    speakerGain = std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / 4.0, -0.8);
}

void SVTStyleAmplifierProcessor::recover (Channel& ch) const
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

void SVTStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    // Six 6550s can draw over an amp at full drive; a solver excursion must not wreck the shared rails.
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 2.0, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.4, ch.sumScreen / n + idleDriverCurrent));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    ch.power.setSource (ch.wSrcCt, rail (ch.sA, 800.0));
    ch.power.setSource (ch.wSrcPi, rail (ch.sC, 420.0));
    ch.power.setSource (ch.wSrcVdr, rail (ch.sB, 420.0));
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sC, 420.0));
    ch.vScreen = rail (ch.sB, 420.0);
}

double SVTStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
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
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return ch.power.voltage (ch.wTail);
        case Probe::driverPlateA: return ch.power.voltage (ch.wDrvPlateA);
        case Probe::powerGridA: return ch.power.voltage (ch.wPowerGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return ch.power.voltage (ch.wOut);
    }
    return 0.0;
}

double SVTStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

void SVTStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void SVTStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double SVTStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double SVTStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void SVTStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
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

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + idleDriverCurrent + idlePreampCurrent;
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
        // The supply first (idle currents), then the amplifier on its rails; then the supply again with the
        // amplifier's own idle currents so the whole thing starts settled.
        const double supplyRate = newSampleRate / (double) supplyInterval;
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sC));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;

        ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sB));
        ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
        ch.vScreen = ch.supply.voltage (ch.sB);
        ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.0);
        ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.0);
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.power.solveSample();

        double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
        ch.power.pentodeCurrents (ch.penA, ipA, isA);
        ch.power.pentodeCurrents (ch.penB, ipB, isB);
        ch.supply.setCurrentSource (ch.iA, -(ipA + ipB));
        ch.supply.setCurrentSource (ch.iB, -(isA + isB + idleDriverCurrent));
        idleSupplyCurrent = ipA + ipB + isA + isB + idleDriverCurrent + idlePreampCurrent;
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * (0.05 + 0.95 * (double) tubeFeelParam->get()) * idleSupplyCurrent);
        dcOk = ch.supply.prepare (supplyRate) && dcOk;
        ch.screenDropA = screenResistor * isA;
        ch.screenDropB = screenResistor * isB;
        ch.vScreen = ch.supply.voltage (ch.sB);
        ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
        ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        ch.power.setSource (ch.wSrcVdr, ch.supply.voltage (ch.sB));
        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sC));
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

void SVTStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
    const double inputGain = juce::roundToInt (inputParam->get()) == 0 ? 1.0 : 0.1778; // 0 dB / -15 dB jacks
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

        // Power Drive: a master volume between the preamp's follower output and the power amp input.
        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

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
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
            const bool ok2 = ch.power.solveSample();
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            double ipA, ipB, isA, isB;
            ch.power.pentodeCurrents (ch.penA, ipA, isA);
            ch.power.pentodeCurrents (ch.penB, ipB, isB);
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

            // Same sanity gate as the other amps: a converged solve can still land on a state no amplifier
            // reaches (a speaker terminal at hundreds of volts); such a sample is a failure and the output
            // holds its last value instead of printing the excursion.
            const double speakerVolts = ch.power.voltage (ch.wOut);
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

    if (useShortcut)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

void SVTStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
