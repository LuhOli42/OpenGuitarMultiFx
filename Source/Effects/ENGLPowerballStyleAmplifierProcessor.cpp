#include "ENGLPowerballStyleAmplifierProcessor.h"
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

    // ---- supply (ENGL Powerball 645-1 schematic, page 1/2)
    // Stand By Off: 460V, On: 440V. Solid-state rectification (BY509 bridge).
    // +430V after bridge → R135=220K bleeder + CE8=47µF/500V → R136=470R → CE9=47µF (screens) →
    // R137=2.2K → PI/post-preamp → R138=2.2K → CE10=22µF (preamp, CE3=10µF/450V)
    constexpr double railPlatesNominal = 460.0;
    constexpr double rectifierResistance = 50.0;
    constexpr double bleeder = 220.0e3;

    // Sag table scaled from Twin Reverb data (497V → 460V)
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 460.0, 460.0, 460.0, 460.0, 459.99, 459.98, 459.96, 459.91,
                                                 459.83, 459.58, 459.21, 458.40, 457.26, 455.18, 451.32, 444.82,
                                                 436.84, 420.88, 406.09, 402.21 };

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

    // ---- tubes ----
    KorenTriode::Parameters triode12AX7() { return {}; }

    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        p.kg1 *= 40.0;
        return p;
    }

    KorenPentode::Parameters pentode6L6Pair()
    {
        KorenPentode::Parameters p;
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    KorenPentode::Parameters pentode6L6PairPower()
    {
        auto p = pentode6L6Pair();
        p.kg1 *= 40.0;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // OT: 4x6L6GC, ~120W class — same as other high-gain amps
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    // NFB: R56=22K from OT secondary to PI grid B. Model taps 16 ohm → scale to 8 ohm matched: 22K * (16/8) = 44K.
    constexpr double feedbackResistor = 44.0e3;

    constexpr double biasSupplyVolts = -55.0;
    constexpr double screenResistor = 470.0;

    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1;
    constexpr double powerTheta = 0.9;

    // U6A plate drives tone stack directly — no cathode follower in the Powerball.
    // Output impedance ≈ R20(330K) || ra(62.5K) ≈ 52.6K
    constexpr double plateOutputImpedance = 52.0e3;

    // ---- preamp: U5A → P10 gain → U5B → U6A (Hi Lead path, 3 stages) ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV1 = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node plateU5a = 0, plateU5b = 0, plateU6a = 0;
    };

    PreampBuild buildPreamp (NodalCircuit& c, double v1Guess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vccV1 = c.addNode(), in = c.addNode();
        b.srcV1 = c.addSource (vccV1, v1Guess);
        b.srcIn = c.addSource (in, 0.0);

        // U5A: R3=100K plate, R1=1M grid leak, R2=1K cathode, CE1=22µF bypass
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plateU5a = c.addNode();
        c.addResistor (in, g1, 10.0e3);                  // grid stopper (estimated)
        c.addResistor (g1, gnd, 1.0e6);                  // R1 grid leak
        c.addTriode (b.plateU5a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateU5a, cgp);
        c.addResistor (vccV1, b.plateU5a, 100.0e3);      // R3
        c.addResistor (k1, gnd, 1.0e3);                  // R2
        c.addCapacitor (k1, gnd, 22.0e-6);                // CE1
        c.setInitialGuess (b.plateU5a, 200.0);
        c.setInitialGuess (k1, 1.2);

        // CH2 Gain: U5A plate → coupling → P10=1MA (audio taper)
        const auto gainIn = c.addNode(), gainWiper = c.addNode();
        c.addCapacitor (b.plateU5a, gainIn, 47.0e-9);    // coupling from U5A plate
        c.addResistor (gainIn, gnd, 470.0e3);             // bias reference
        b.rGainTop = c.addResistor (gainIn, gainWiper, 1.0e6);
        b.rGainBot = c.addResistor (gainWiper, gnd, 1.0e6);

        // U5B: R9=100K plate, R7=1M grid leak, R8=1.5K cathode, CE2=22µF bypass
        const auto g2 = c.addNode(), k2 = c.addNode();
        b.plateU5b = c.addNode();
        c.addResistor (gainWiper, g2, 10.0e3);            // series from gain wiper
        c.addResistor (g2, gnd, 1.0e6);                   // R7 grid leak
        c.addTriode (b.plateU5b, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateU5b, cgp);
        c.addResistor (vccV1, b.plateU5b, 100.0e3);       // R9
        c.addResistor (k2, gnd, 1.5e3);                   // R8
        c.addCapacitor (k2, gnd, 22.0e-6);                 // CE2
        c.setInitialGuess (b.plateU5b, 230.0);
        c.setInitialGuess (k2, 1.5);

        // U6A: R20=330K plate (high-impedance for extra gain), R14=470K grid leak,
        // R19=3.3K cathode, CE4=1µF bypass
        const auto g3 = c.addNode(), k3 = c.addNode(), coup3 = c.addNode();
        b.plateU6a = c.addNode();
        c.addCapacitor (b.plateU5b, coup3, 47.0e-9);      // coupling from U5B plate
        c.addResistor (coup3, g3, 100.0e3);               // series stopper
        c.addResistor (g3, gnd, 470.0e3);                  // R14 grid leak
        c.addTriode (b.plateU6a, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateU6a, cgp);
        c.addResistor (vccV1, b.plateU6a, 330.0e3);       // R20 (high impedance = extra gain + early clipping)
        c.addResistor (k3, gnd, 3.3e3);                   // R19
        c.addCapacitor (k3, gnd, 1.0e-6);                 // CE4
        c.setInitialGuess (b.plateU6a, 180.0);
        c.setInitialGuess (k3, 2.0);

        return b;
    }
}

ENGLPowerballStyleAmplifierProcessor::ENGLPowerballStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto gain = make ("engl_pb_gain", "Gain", 0.5f);
    auto treble = make ("engl_pb_treble", "Treble", 0.5f);
    auto mid = make ("engl_pb_mid", "Mid", 0.5f);
    auto bass = make ("engl_pb_bass", "Bass", 0.5f);
    auto presence = make ("engl_pb_presence", "Presence", 0.3f);
    auto depth = make ("engl_pb_depth", "Depth", 0.3f);
    auto master = make ("engl_pb_master", "Master", 0.5f);
    auto output = make ("engl_pb_output", "Output", 0.5f);
    auto power = make ("engl_pb_power", "Power Drive", 0.5f);
    auto bias = make ("engl_pb_bias", "Bias", 0.5f);
    auto feel = make ("engl_pb_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "engl_pb_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    depthParam = depth.get();
    masterParam = master.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "engl_powerball", "Powerball-Style Amplifier", "|", std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (depth));
    group->addChild (std::move (master));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("engl_pb_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void ENGLPowerballStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        ch.sB = c.addNode();
        ch.sC = c.addNode();
        ch.sD = c.addNode();

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 47.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);
        c.addResistor (ch.sA, ch.sB, 470.0);
        c.addCapacitor (ch.sB, gnd, 47.0e-6);
        c.addResistor (ch.sB, ch.sC, 2.2e3);
        c.addCapacitor (ch.sC, gnd, 22.0e-6);
        c.addResistor (ch.sC, ch.sD, 2.2e3);
        c.addCapacitor (ch.sD, gnd, 10.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);
        ch.iB = c.addCurrentSource (ch.sB, -0.012);
        ch.iC = c.addCurrentSource (ch.sC, -0.006);
        ch.iD = c.addCurrentSource (ch.sD, -0.006);
        for (auto n : { ch.sA, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 430.0);
        c.setInitialGuess (ch.sD, 400.0);
    }

    // ================================================================ preamp (U5A → U5B → U6A)
    {
        auto b = buildPreamp (ch.pre, 400.0);
        ch.pSrcV1 = b.srcV1;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.pPlateU5a = b.plateU5a;
        ch.pPlateU5b = b.plateU5b;
        ch.pPlateU6a = b.plateU6a;
    }

    // ================================================================ tone block: tone stack + U6B + U7A + U7B + master
    {
        auto& c = ch.tone;
        c.setIntegrationTheta (powerTheta);

        // AC input from preamp U6A plate
        const auto pre = c.addNode();
        ch.tSrcPre = c.addSource (pre, 0.0);

        // VCC for post-tonestack triodes (fed from sC rail)
        const auto vccPost = c.addNode();
        ch.tSrcVcc = c.addSource (vccPost, 430.0);

        // Lead tone stack (FMV style): R32=330K slope, P14=250KB treble, P11=1MA bass, P15=250KB focused mid.
        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.tToneIn = ti;
        const auto postNode = c.addNode();
        ch.tTone = c.addNode();
        c.addResistor (pre, ti, plateOutputImpedance);
        c.addCapacitor (ti, top, 470.0e-12);
        c.addResistor (ti, nB, 330.0e3);
        ch.rTrebleTop = c.addResistor (top, postNode, 125.0e3);
        ch.rTrebleBottom = c.addResistor (postNode, nT, 125.0e3);
        c.addResistor (nT, nB, 22.0e3);
        c.addCapacitor (nB, nT, 0.22e-6);
        ch.rBass = c.addResistor (nT, nM, 500.0e3);
        ch.rMidTop = c.addResistor (nM, nMw, 125.0e3);
        ch.rMidBottom = c.addResistor (nMw, gnd, 125.0e3);
        c.addCapacitor (nB, nMw, 0.047e-6);
        ch.rMaster = c.addResistor (postNode, ch.tTone, 125.0e3);

        // ---- post-tonestack gain stages: U6B → U7A → U7B ----

        // U6B: R24=100K plate, R22=1M grid leak, R23=1.5K cathode, CE5=22µF bypass
        const auto gU6B = c.addNode(), kU6B = c.addNode();
        ch.tPlateU6b = c.addNode();
        c.addCapacitor (ch.tTone, gU6B, 100.0e-9);
        c.addResistor (gU6B, gnd, 1.0e6);
        c.addTriode (ch.tPlateU6b, gU6B, kU6B, triode12AX7());
        c.addResistor (vccPost, ch.tPlateU6b, 100.0e3);
        c.addResistor (kU6B, gnd, 1.5e3);
        c.addCapacitor (kU6B, gnd, 22.0e-6);
        c.setInitialGuess (ch.tPlateU6b, 250.0);
        c.setInitialGuess (kU6B, 1.5);

        // U7A: R43=100K plate, C17=10nF coupling, R42=1K cathode, CE6=4.7µF bypass
        const auto gU7A = c.addNode(), kU7A = c.addNode();
        ch.tPlateU7a = c.addNode();
        c.addCapacitor (ch.tPlateU6b, gU7A, 10.0e-9);
        c.addResistor (gU7A, gnd, 1.0e6);
        c.addTriode (ch.tPlateU7a, gU7A, kU7A, triode12AX7());
        c.addResistor (vccPost, ch.tPlateU7a, 100.0e3);
        c.addResistor (kU7A, gnd, 1.0e3);
        c.addCapacitor (kU7A, gnd, 4.7e-6);
        c.setInitialGuess (ch.tPlateU7a, 250.0);
        c.setInitialGuess (kU7A, 1.2);

        // U7B: R49=100K plate, C18=100nF coupling, R48=1.5K cathode, CE7=22µF bypass
        const auto gU7B = c.addNode(), kU7B = c.addNode();
        ch.tPlateU7b = c.addNode();
        c.addCapacitor (ch.tPlateU7a, gU7B, 100.0e-9);
        c.addResistor (gU7B, gnd, 1.0e6);
        c.addTriode (ch.tPlateU7b, gU7B, kU7B, triode12AX7());
        c.addResistor (vccPost, ch.tPlateU7b, 100.0e3);
        c.addResistor (kU7B, gnd, 1.5e3);
        c.addCapacitor (kU7B, gnd, 22.0e-6);
        c.setInitialGuess (ch.tPlateU7b, 250.0);
        c.setInitialGuess (kU7B, 1.5);

        // Master wiper: coupling from U7B plate + 1M rheostat to ground
        ch.tMasterWiper = c.addNode();
        c.addCapacitor (ch.tPlateU7b, ch.tMasterWiper, 100.0e-9);
        c.addResistor (ch.tMasterWiper, gnd, 1.0e6);
    }

    // ================================================================ power block: PI + power amp + OT + speaker + NFB (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);

        // AC input from tone block master wiper
        const auto masterIn = c.addNode();
        ch.wSrcMaster = c.addSource (masterIn, 0.0);

        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 430.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // PI: U8A/U8B LTP. R61=82K plate A, R62=100K plate B. R53=100K cross-coupling.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (masterIn, g1, 100.0e-9);            // C19: coupling from master to PI
        c.addResistor (g1, nm, 1.0e6);                      // R52 grid leak
        c.addResistor (g2, fp, 150.0e3);                    // R59 grid leak / NFB return
        c.addResistor (pb, g1, 100.0e3);                    // R53 cross-coupling
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addCapacitor (pa, pb, 75.0e-12);
        c.addResistor (k, nm, 1.5e3);
        c.addResistor (nm, gnd, 4.7e3);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 25.0);
        c.setInitialGuess (nm, 22.0);
        c.setInitialGuess (g1, 22.0);
        c.setInitialGuess (g2, 22.0);
        c.setInitialGuess (fp, 2.0);

        // Power: 4 × 6L6GC as two push-pull pairs
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.047e-6);
        c.addCapacitor (pb, g4, 0.047e-6);
        c.addResistor (g3, nb, 330.0e3);
        c.addResistor (g4, nb, 330.0e3);
        c.addResistor (g3, g3s, 2.2e3);
        c.addResistor (g4, g4s, 2.2e3);
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6L6PairPower(), railPlatesNominal - 24.0);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6L6PairPower(), railPlatesNominal - 24.0);

        c.addCapacitor (pp1, pp2, 400.0e-12);
        c.addResistor (pp1, pp2, 20.0e3);
        {
            const auto sn = c.addNode();
            c.addResistor (pp1, sn, 2.0e3);
            c.addCapacitor (sn, pp2, 3.0e-9);
        }
        c.addCapacitor (pp1, gnd, 400.0e-12);
        c.addCapacitor (pp2, gnd, 400.0e-12);
        c.addResistor (ct, a1, primaryHalfResistance);
        c.addResistor (ct, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
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

        // NFB: R56=22K (scaled to 44K) from speaker to PI grid B, with Presence P8=50KC + C25=100nF.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 25.0e3);
        ch.rPresBottom = c.addResistor (wp, gnd, 25.0e3);
        c.addCapacitor (fp, wp, 100.0e-9);
        c.addCapacitor (fp, gnd, 1.5e-9);

        // Depth: P9=1MA + C27=10nF from speaker to feedback point
        ch.rDepthPot = c.addResistor (ch.wOut, fp, 1.0e6);
        c.addCapacitor (ch.wOut, fp, 10.0e-9);

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -50.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void ENGLPowerballStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 250.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 250.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 50.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 50.0e3 - presBottom);
    const double masterR = juce::jmax (1.0, 250.0e3 * pots::audio (k.master));
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));
    const double depthR = juce::jmax (1.0, 1.0e6 * (1.0 - pots::audio (k.depth)));

    for (auto& ch : channels)
    {
        const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
        ch.pre.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBottom));
        ch.tone.setResistance (ch.rTrebleTop, trebleTop);
        ch.tone.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.tone.setResistance (ch.rBass, bassR);
        ch.tone.setResistance (ch.rMidTop, midTop);
        ch.tone.setResistance (ch.rMidBottom, midBottom);
        ch.tone.setResistance (ch.rMaster, masterR);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresTop, presTop);
            ch.power.setResistance (ch.rPresBottom, presBottom);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setResistance (ch.rBiasTrim, trim);
            ch.power.setResistance (ch.rDepthPot, depthR);
        }
        if (k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void ENGLPowerballStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void ENGLPowerballStyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.tone.restoreDynamicState (ch.toneRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
    ch.mwDcPrev = ch.tone.voltage (ch.tMasterWiper);
    ch.mwDcOut = 0.0;
    ch.u7bDcPrev = ch.tone.voltage (ch.tPlateU7b) - ch.plateDcU7b;
    ch.u7bDcOut = 0.0;
    ch.mwServo = 0.0;
}

void ENGLPowerballStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
    ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 520.0));
    }
    ch.vScreen = rail (ch.sB, 560.0);
    // preamp and tone VCC NOT updated here — the 230,000x open-loop gain
    // of the tone block's 3 cascaded triodes turns any supply ripple into
    // runaway DC drift via the supply→preamp plate→acU6a→tone feedback path
}

double ENGLPowerballStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (toneVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    ch.bmRail = sagRailLookup (ch.bmEnvelope);

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double u = absDrive / juce::jmax (1.0e-9, k);
    const double y = bmYmax * u / std::pow (1.0 + std::pow (u, bmKneeN), 1.0 / bmKneeN);
    const double raw = std::copysign (y * ch.bmRail, toneVoltage);

    const double shelfCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmShelfHz / juce::jmax (1.0, sampleRate));
    ch.bmToneState += shelfCoeff * (raw - ch.bmToneState);
    ch.bmOutput = ch.bmToneState + bmShelfHfGain * (raw - ch.bmToneState);
    return ch.bmOutput;
}

double ENGLPowerballStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::u5aPlate: return ch.pre.voltage (ch.pPlateU5a);
        case Probe::u5bPlate: return ch.pre.voltage (ch.pPlateU5b);
        case Probe::u6aPlate: return ch.pre.voltage (ch.pPlateU6a);
        case Probe::u6bPlate: return ch.tone.voltage (ch.tPlateU6b);
        case Probe::u7aPlate: return ch.tone.voltage (ch.tPlateU7a);
        case Probe::u7bPlate: return ch.tone.voltage (ch.tPlateU7b);
        case Probe::toneStackOut: return ch.tone.voltage (ch.tTone);
        case Probe::phaseInverterGrid: return reducedOrder ? 0.0 : ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return reducedOrder ? 0.0 : ch.power.voltage (ch.wTail);
        case Probe::powerPlateA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPP2);
        case Probe::powerGridA: return reducedOrder ? 0.0 : ch.power.voltage (ch.wPowerGridA);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wBias);
        case Probe::feedbackNode: return reducedOrder ? 0.0 : ch.power.voltage (ch.wFeedback);
    }
    return 0.0;
}

void ENGLPowerballStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double seconds)
    {
        s.reset (newSampleRate, seconds);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMid, midParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedDepth, depthParam, 0.02);
    setup (smoothedMaster, masterParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), depthParam->get(), masterParam->get(),
                  powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iPiRun = 0.006, iPreRun = 0.006, ipRun = 0.16, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sD));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.plateDcU6a = ch.pre.voltage (ch.pPlateU6a);

            ch.tone.setSource (ch.tSrcPre, 0.0);
            ch.tone.setSource (ch.tSrcVcc, ch.supply.voltage (ch.sC));
            passOk = ch.tone.prepare (newSampleRate) && passOk;
            ch.tone.solveSample();
            ch.plateDcU7b = ch.tone.voltage (ch.tPlateU7b);

            ch.vScreen = ch.supply.voltage (ch.sB);
            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0, iPi = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcMaster, 0.0);
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                const double vPi = ch.supply.voltage (ch.sC);
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
            }
            // Post-tonestack stages draw from sC
            const double vPost = ch.supply.voltage (ch.sC);
            const double iPost = (vPost - ch.tone.voltage (ch.tPlateU6b)) / 100.0e3
                               + (vPost - ch.tone.voltage (ch.tPlateU7a)) / 100.0e3
                               + (vPost - ch.tone.voltage (ch.tPlateU7b)) / 100.0e3;
            const double vPre = ch.supply.voltage (ch.sD);
            const double iPre = (vPre - ch.pre.voltage (ch.pPlateU5a)) / 100.0e3
                              + (vPre - ch.pre.voltage (ch.pPlateU5b)) / 100.0e3
                              + (vPre - ch.pre.voltage (ch.pPlateU6a)) / 330.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi + iPost - iPiRun);
            iPreRun += 0.5 * (iPre - iPreRun);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iPreRun);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iPreRun + (ch.vScreen / bleeder);
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
        }
        ch.tone.setSource (ch.tSrcVcc, ch.supply.voltage (ch.sC));
        ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sD));

        ch.pre.solveSample();
        ch.plateDcU6a = ch.pre.voltage (ch.pPlateU6a);
        ch.tone.setSource (ch.tSrcPre, 0.0);
        ch.tone.solveSample();
        ch.plateDcU7b = ch.tone.voltage (ch.tPlateU7b);
        if (! reducedOrder)
            ch.power.solveSample();

        ch.mwTarget = ch.tone.voltage (ch.tMasterWiper);
        ch.mwServo = 0.0;

        ch.mwDcPrev = ch.mwTarget;
        ch.mwDcOut = 0.0;
        ch.u7bDcPrev = ch.tone.voltage (ch.tPlateU7b) - ch.plateDcU7b;
        ch.u7bDcOut = 0.0;

        ch.pre.saveDynamicState (ch.preRest);
        ch.tone.saveDynamicState (ch.toneRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
    }
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void ENGLPowerballStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedGain.setTargetValue (gainParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedMid.setTargetValue (midParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    smoothedMaster.setTargetValue (masterParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMid.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float dp = smoothedDepth.getNextValue();
        const float ms = smoothedMaster.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, dp, ms, pw, bi, fe, speakerChoice });
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

            // AC-coupled from U6A plate to tone block, scaled by Power Drive
            const double acU6a = ch.pre.voltage (ch.pPlateU6a) - ch.plateDcU6a;
            ch.tone.setSource (ch.tSrcPre, masterGain * acU6a - ch.mwServo);
            const bool okTone = ch.tone.solveSample();

            // DC servo: slow integrator that feeds back the master wiper's DC
            // error to the tone input, counteracting the 230,000x open-loop drift
            constexpr double servoGain = 1.0e-3;
            {
                const double mw = ch.tone.voltage (ch.tMasterWiper);
                ch.mwServo += servoGain * (mw - ch.mwTarget);
            }

            // DC-block inter-block signals
            constexpr double dcR = 0.999935; // HPF fc ≈ 0.5 Hz at 48 kHz
            {
                const double mw = ch.tone.voltage (ch.tMasterWiper);
                ch.mwDcOut = dcR * ch.mwDcOut + mw - ch.mwDcPrev;
                ch.mwDcPrev = mw;
            }
            {
                const double u7b = ch.tone.voltage (ch.tPlateU7b) - ch.plateDcU7b;
                ch.u7bDcOut = dcR * ch.u7bDcOut + u7b - ch.u7bDcPrev;
                ch.u7bDcPrev = u7b;
            }

            bool ok = okPre && okTone;

            if (! reducedOrder)
            {
                // AC-coupled from tone block master wiper to power block
                ch.power.setSource (ch.wSrcMaster, ch.mwDcOut);
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - ch.screenDropA);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - ch.screenDropB);
                const bool okPower = ch.power.solveSample();
                ok = ok && okPower;

                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.screenDropA += 0.3 * (screenResistor * isA - ch.screenDropA);
                ch.screenDropB += 0.3 * (screenResistor * isB - ch.screenDropB);
                ch.sumPlate += ipA + ipB;
                ch.sumScreen += isA + isB;
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            const double speakerVolts = reducedOrder
                ? behavioralPowerStage (ch, ch.u7bDcOut)
                : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));

            if (ok)
            {
                ch.failStreak = 0;
                if (++ch.restRefreshCounter >= restRefreshInterval)
                {
                    ch.restRefreshCounter = 0;
                    ch.pre.saveDynamicState (ch.preRest);
                    ch.tone.saveDynamicState (ch.toneRest);
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

    shortcut.end (buffer);
}

void ENGLPowerballStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
