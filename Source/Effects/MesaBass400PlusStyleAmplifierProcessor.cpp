#include "MesaBass400PlusStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/MesaBass400Plus.md, "What the Bass 400+ actually is") ----
    constexpr double railPlatesNominal = 500.0;   // OT centre tap for the twelve 6L6GCs
    constexpr double railScreensNominal = 400.0;  // screens and the driver stage
    constexpr double railPreampNominal = 300.0;   // preamp and the phase splitter's plates
    constexpr double rectifierResistance = 80.0;  // solid-state rectified into a large PT
    constexpr double rectifierResistance2 = 150.0; // the 400 V rail's own series resistance
    constexpr double screenResistor = 150.0;      // shared screen stopper per sextet
    constexpr double idlePlateCurrent = 0.42;     // twelve 6L6GCs at ~35 mA
    constexpr double idleScreenCurrent = 0.030;
    constexpr double idleDriverCurrent = 0.010;
    constexpr double idlePreampCurrent = 0.003;   // the marked ~300 V preamp rail is the LOADED rail
    constexpr double railDriverReturn = -180.0;   // negative winding the level-shifters return to

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

    // Output transformer: ~1.2k plate-to-plate for twelve 6L6GCs at ~500 V, 4 ohm secondary.
    constexpr double primaryHalfInductance = 15.0;
    constexpr double halfToSecondaryTurns = 8.66;         // sqrt(300/4): one half-primary is 300 ohm
    constexpr double couplingHalves = 0.9996;
    constexpr double couplingSecondary = 0.9990;
    constexpr double primaryHalfResistance = 20.0;
    constexpr double secondaryResistance = 0.05;
    constexpr double feedbackResistor = 100.0e3;

    // Graphic EQ bands (Hz) and the series inductor of each L-C trap; C computed per band in build.
    constexpr int eqBands = 7;
    constexpr double eqFreq[eqBands] = { 40.0, 80.0, 160.0, 320.0, 750.0, 2200.0, 6600.0 };
    constexpr double eqInductance[eqBands] = { 1.0, 1.0, 0.5, 0.2, 0.05, 0.01, 0.001 };

    constexpr double speakerNominal[3] = { 2.0, 4.0, 8.0 };

    constexpr double powerTheta = 0.9;
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

    volumeParam = volume.get();
    brightParam = bright.get();
    bassParam = bass.get();
    middleParam = middle.get();
    trebleParam = treble.get();
    masterParam = master.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "mesa400p", "Bass 400+-Style Amplifier", "|", std::move (volume));
    group->addChild (std::move (bright));
    group->addChild (std::move (bass));
    group->addChild (std::move (middle));
    group->addChild (std::move (treble));
    group->addChild (std::move (master));

    const char* eqNames[eqBands] = { "40 Hz", "80 Hz", "160 Hz", "320 Hz", "750 Hz", "2.2 kHz", "6.6 kHz" };
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("mesa400p_page2", "Page 2", "|",
        std::make_unique<juce::AudioParameterFloat> ("mesa400p_eq1", eqNames[0],
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    eqParam[0] = (juce::AudioParameterFloat*) page2->getParameters()[0];
    for (int i = 1; i < eqBands; ++i)
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (
            juce::String ("mesa400p_eq") + juce::String (i + 1), eqNames[i],
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
        eqParam[i] = p.get();
        page2->addChild (std::move (p));
    }
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
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void MesaBass400PlusStyleAmplifierProcessor::buildChannel (Channel& ch)
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
        c.addCapacitor (ch.sA, gnd, 180.0e-6);

        const double dcDrop2 = rectifierResistance2 * (idleScreenCurrent + idleDriverCurrent + idlePreampCurrent);
        ch.srcVoc2 = c.addSource (vo2, railScreensNominal + dcDrop2);
        ch.rRect2 = c.addResistor (vo2, ch.sB, rectifierResistance2);
        c.addCapacitor (ch.sB, gnd, 100.0e-6);

        c.addResistor (ch.sB, ch.sC, 6.8e3);             // dropper to the +300 V preamp/PI rail
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
    // Two 12AX7 stages around the Volume pot (pull-Bright), the passive tone section, recovery, Master.
    {
        auto& c = ch.pre;
        c.setIntegrationTheta (0.5);
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, railPreampNominal);
        ch.pSrcIn = c.addSource (in, 0.0);

        // V1a -- first gain stage.
        const auto g1 = c.addNode(), p1 = c.addNode(), k1 = c.addNode();
        c.addResistor (in, g1, 68.0e3);
        c.addResistor (g1, gnd, 1.0e6);
        c.addTriode (p1, g1, k1, triode12AX7());
        c.addCapacitor (g1, p1, cgp);
        c.addResistor (vcc, p1, 100.0e3);
        c.addResistor (k1, gnd, 1.5e3);
        c.addCapacitor (k1, gnd, 22.0e-6);
        c.setInitialGuess (g1, 0.0);
        c.setInitialGuess (k1, 1.4);
        c.setInitialGuess (p1, 200.0);

        // Volume pot (1M audio) with the pull-Bright cap across its top leg.
        const auto na = c.addNode(), vt = c.addNode(), vw = c.addNode();
        c.addCapacitor (p1, na, 0.047e-6);
        c.addResistor (na, gnd, 470.0e3);
        c.addResistor (na, vt, 100.0e3);
        ch.rVolumeTop = c.addResistor (vt, vw, 500.0e3);
        ch.rVolumeBot = c.addResistor (vw, gnd, 500.0e3);
        ch.capBright = c.addCapacitor (vt, vw, 1.0e-12); // Bright on: ~470 pF treble bleed
        c.setInitialGuess (na, 0.0);

        // V1b -- second gain stage.
        const auto g2 = c.addNode(), p2 = c.addNode(), k2 = c.addNode();
        c.addCapacitor (vw, g2, 0.047e-6);
        c.addResistor (g2, gnd, 1.0e6);
        c.addTriode (p2, g2, k2, triode12AX7());
        c.addCapacitor (g2, p2, cgp);
        c.addResistor (vcc, p2, 100.0e3);
        c.addResistor (k2, gnd, 1.5e3);
        c.addCapacitor (k2, gnd, 22.0e-6);
        c.setInitialGuess (k2, 1.4);
        c.setInitialGuess (p2, 200.0);
        ch.pPlate2 = p2;

        // Passive bass/middle/treble section (Ampeg-family network, Mesa-voiced values).
        const auto ti = c.addNode(), t1 = c.addNode(), w1 = c.addNode(), nb = c.addNode(),
                   ns = c.addNode(), t2 = c.addNode(), wt = c.addNode(), tb = c.addNode();
        c.addCapacitor (p2, ti, 0.047e-6);
        c.addResistor (ti, t1, 220.0e3);
        ch.rBassTop = c.addResistor (t1, w1, 500.0e3);   // Bass 1M audio
        ch.rBassBot = c.addResistor (w1, nb, 500.0e3);
        c.addResistor (nb, gnd, 22.0e3);
        c.addCapacitor (w1, ns, 0.001e-6);
        c.addResistor (ns, t2, 100.0e3);
        c.addCapacitor (ti, t2, 470.0e-12);              // treble feed
        ch.rTrebleTop = c.addResistor (t2, wt, 500.0e3); // Treble 1M audio
        ch.rTrebleBot = c.addResistor (wt, tb, 500.0e3);
        c.addCapacitor (tb, gnd, 0.0047e-6);
        ch.pTone = wt;
        c.setInitialGuess (ti, 0.0);

        // V2a -- recovery stage; its plate carries the Middle pot's fixed mid trap
        // (~400 Hz, the 400+'s Middle control's effective band).
        const auto g3 = c.addNode(), p3 = c.addNode(), k3 = c.addNode(), mt = c.addNode(),
                   mtp = c.addNode(), mc = c.addNode();
        c.addCapacitor (wt, g3, 0.047e-6);
        c.addResistor (g3, gnd, 470.0e3);
        c.addTriode (p3, g3, k3, triode12AX7());
        c.addCapacitor (g3, p3, cgp);
        c.addResistor (vcc, p3, 100.0e3);
        c.addResistor (k3, gnd, 1.5e3);
        c.addCapacitor (k3, gnd, 22.0e-6);
        c.addCapacitor (p3, mt, 0.047e-6);
        c.addResistor (mt, gnd, 470.0e3);
        ch.rMid = c.addResistor (mt, mtp, 25.0e3);       // Middle: rheostat on the trap
        c.addCapacitor (mtp, mc, 0.22e-6);
        c.addCoupledInductors ({ { mc, gnd } }, { 0.3 }); // ~0.62 kHz trap (see the doc)
        c.setInitialGuess (k3, 1.4);
        c.setInitialGuess (p3, 200.0);
        c.setInitialGuess (mt, 0.0);
        ch.pPlate3 = p3;

        // Master pot -> out node.
        const auto mtop = c.addNode(), mw = c.addNode(), po = c.addNode();
        c.addResistor (mt, mtop, 470.0);
        ch.rMasterTop = c.addResistor (mtop, mw, 25.0e3); // Master 50k linear
        ch.rMasterBot = c.addResistor (mw, gnd, 25.0e3);
        c.addCapacitor (mw, po, 0.047e-6);
        c.addResistor (po, gnd, 1.0e6);
        c.setInitialGuess (mw, 0.0);
        ch.pOut = po;
        ch.pPlate1 = p1;
    }

    // ================================================================ graphic EQ (linear block):
    // a finite-gain inverting stage; each slider's pot straddles the input/output buses with a
    // series L-C trap on its wiper -- the amp's own feedback-EQ topology.
    {
        auto& c = ch.eq;
        c.setIntegrationTheta (0.5);
        const auto cin = c.addNode(), ei = c.addNode(), sn = c.addNode();
        ch.qOut = c.addNode();
        ch.qSrcIn = c.addSource (cin, 0.0);
        c.addCapacitor (cin, ei, 0.47e-6);
        c.addResistor (ei, gnd, 1.0e6);
        c.addResistor (ei, sn, 68.0e3);                  // input leg
        c.addResistor (ch.qOut, sn, 68.0e3);             // feedback leg -> unity gain
        c.addFiniteGainOpAmp (gnd, sn, ch.qOut, -1.0e5); // virtual-earth summing node
        for (int i = 0; i < eqBands; ++i)
        {
            const auto w = c.addNode(), x = c.addNode();
            ch.rEqTop[i] = c.addResistor (ei, w, 25.0e3);
            ch.rEqBot[i] = c.addResistor (w, ch.qOut, 25.0e3);
            c.addCoupledInductors ({ { w, x } }, { eqInductance[i] });
            const double cap = 1.0 / (39.478 * eqFreq[i] * eqFreq[i] * eqInductance[i]);
            c.addCapacitor (x, gnd, cap);
        }
        c.setInitialGuess (ei, 0.0);
    }

    // ================================================================ power amp: 12AX7 LTP, 12AU7
    // drivers with level-shifters (~-68 V taps), twelve 6L6GCs as two sextets, OT, speaker, NFB.
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

        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(),
                   k = c.addNode(), bn = c.addNode();
        const auto g7 = c.addNode();
        c.addCapacitor (cin, g7, 0.1e-6);
        c.addResistor (g7, g1, 1.0e3);
        c.addResistor (g1, bn, 470.0e3);
        c.addResistor (g2, bn, 470.0e3);
        c.addTriode (pa, g1, k, triode12AX7());
        c.addTriode (pb, g2, k, triode12AX7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 100.0e3);
        c.addResistor (vpi, pb, 68.0e3);
        c.addResistor (k, bn, 220.0);
        c.addResistor (bn, neg, 47.0e3);
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

        const auto gd1 = c.addNode(), gd2 = c.addNode(), pd1 = c.addNode(), pd2 = c.addNode(),
                   kd1 = c.addNode(), kd2 = c.addNode(), m1 = c.addNode(), m2 = c.addNode(),
                   nab = c.addNode(), nbb = c.addNode();
        c.addCapacitor (pa, gd1, 0.047e-6);
        c.addCapacitor (pb, gd2, 0.047e-6);
        c.addResistor (gd1, gnd, 470.0e3);
        c.addResistor (gd2, gnd, 470.0e3);
        c.addTriode (pd1, gd1, kd1, triode12AU7());
        c.addTriode (pd2, gd2, kd2, triode12AU7());
        c.addCapacitor (gd1, pd1, 2.2e-12);
        c.addCapacitor (gd2, pd2, 2.2e-12);
        c.addResistor (vdr, pd1, 47.0e3);
        c.addResistor (vdr, pd2, 47.0e3);
        c.addResistor (kd1, gnd, 1.8e3);
        c.addResistor (kd2, gnd, 1.8e3);
        c.addCapacitor (kd1, kd2, 1.0e-6);
        c.setInitialGuess (pd1, 220.0);
        c.setInitialGuess (pd2, 220.0);
        c.setInitialGuess (kd1, 5.5);
        c.setInitialGuess (kd2, 5.5);
        ch.wDrvPlateA = pd1;

        c.addResistor (pd1, m1, 300.0e3);
        c.addResistor (m1, nab, 120.0e3);
        ch.rBiasTapA = c.addResistor (nab, neg, 175.0e3); // tap lands ~-68 V at idle
        c.addResistor (pd2, m2, 300.0e3);
        c.addResistor (m2, nbb, 120.0e3);
        ch.rBiasTapB = c.addResistor (nbb, neg, 175.0e3);
        c.setInitialGuess (m1, 100.0);
        c.setInitialGuess (m2, 100.0);
        c.setInitialGuess (nab, -68.0);
        c.setInitialGuess (nbb, -68.0);

        const auto g3 = c.addNode(), g4 = c.addNode(), pp1 = c.addNode(), pp2 = c.addNode(),
                   a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (nab, g3, 47.0e3);
        c.addResistor (nbb, g4, 47.0e3);
        c.addCapacitor (g3, gnd, 660.0e-12);             // six 6L6s' input capacitance per bank
        c.addCapacitor (g4, gnd, 660.0e-12);
        ch.wPowerGridA = g3;
        ch.penA = c.addPentode (pp1, g3, gnd, pentode6L6Sextuple(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4, gnd, pentode6L6Sextuple(), railScreensNominal);

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
        // Secondary sense chosen so feedback into the second PI grid is NEGATIVE.
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

        const auto fp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        c.addCapacitor (fp, g2, 0.1e-6);
        c.addCapacitor (fp, gnd, 1.5e-9);
        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        c.setInitialGuess (g3, -68.0);
        c.setInitialGuess (g4, -68.0);
        c.setInitialGuess (nab, -68.0);
        c.setInitialGuess (nbb, -68.0);
    }
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
    const double bassBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double trebleBot = juce::jmax (1.0, 1.0e6 * pots::audio (k.treble));
    const double midR = juce::jmax (1.0, 50.0e3 * k.middle);
    const double masterBot = juce::jmax (1.0, 50.0e3 * k.master);
    const double brightC = k.bright > 0.5 ? 470.0e-12 : 1.0e-12;
    const double biasR = 110.0e3 + 130.0e3 * k.bias; // noon ~175k -> taps ~-68 V
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double rectifier2 = rectifierResistance2 * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride
                                                  : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolumeTop, juce::jmax (1.0, 1.0e6 - volumeBot));
        ch.pre.setResistance (ch.rVolumeBot, volumeBot);
        ch.pre.setCapacitance (ch.capBright, brightC);
        ch.pre.setResistance (ch.rBassTop, juce::jmax (1.0, 1.0e6 - bassBot));
        ch.pre.setResistance (ch.rBassBot, bassBot);
        ch.pre.setResistance (ch.rTrebleTop, juce::jmax (1.0, 1.0e6 - trebleBot));
        ch.pre.setResistance (ch.rTrebleBot, trebleBot);
        ch.pre.setResistance (ch.rMid, midR);
        ch.pre.setResistance (ch.rMasterTop, juce::jmax (1.0, 50.0e3 - masterBot));
        ch.pre.setResistance (ch.rMasterBot, masterBot);
        for (int i = 0; i < eqBands; ++i)
        {
            const double slider = juce::jlimit (0.0, 1.0, k.eq[i]);
            ch.eq.setResistance (ch.rEqTop[i], juce::jmax (1.0, 50.0e3 * (1.0 - slider)));
            ch.eq.setResistance (ch.rEqBot[i], juce::jmax (1.0, 50.0e3 * slider));
        }
        ch.power.setResistance (ch.rBiasTapA, biasR);
        ch.power.setResistance (ch.rBiasTapB, biasR);
        ch.power.setResistance (ch.rFeedback, feedbackR);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setResistance (ch.rRect2, rectifier2);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
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
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void MesaBass400PlusStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 2.0, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.4, ch.sumScreen / n + idleDriverCurrent));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    ch.power.setSource (ch.wSrcCt, rail (ch.sA, 700.0));
    ch.power.setSource (ch.wSrcPi, rail (ch.sC, 420.0));
    ch.power.setSource (ch.wSrcVdr, rail (ch.sB, 500.0));
    ch.pre.setSource (ch.pSrcVcc, rail (ch.sC, 420.0));
    ch.vScreen = rail (ch.sB, 500.0);
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
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::driverPlateA: return ch.power.voltage (ch.wDrvPlateA);
        case Probe::powerGridA: return ch.power.voltage (ch.wPowerGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return ch.power.voltage (ch.wOut);
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
    feedbackOverride = ohms;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double MesaBass400PlusStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double MesaBass400PlusStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
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

    idleSupplyCurrent = idlePlateCurrent + idleScreenCurrent + idleDriverCurrent + idlePreampCurrent;
    appliedSpeaker = 1;
    Knobs k {};
    k.input = 1.0;
    k.volume = volumeParam->get();
    k.bright = brightParam->get();
    k.bass = bassParam->get();
    k.middle = middleParam->get();
    k.treble = trebleParam->get();
    k.master = masterParam->get();
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
        dcOk = ch.supply.prepare (supplyRate) && dcOk;

        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sC));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        dcOk = ch.eq.prepare (newSampleRate) && dcOk;

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
            for (int b = 0; b < eqBands; ++b)
                k.eq[b] = eqParam[b]->get();
            k.powerDrive = pw;
            k.bias = bi;
            k.tubeFeel = fe;
            k.speaker = speakerChoice;
            updatePots (k);
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

            ch.eq.setSource (ch.qSrcIn, masterGain * ch.pre.voltage (ch.pOut));
            ch.eq.solveSample(); // linear block: cannot diverge, always one pass
            ch.power.setSource (ch.wSrcPre, ch.eq.voltage (ch.qOut));
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

            const double speakerVolts = ch.power.voltage (ch.wOut);
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

void MesaBass400PlusStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
