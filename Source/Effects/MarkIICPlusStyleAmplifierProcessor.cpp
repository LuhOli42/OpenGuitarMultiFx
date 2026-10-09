#include "MarkIICPlusStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/MarkIICPlus.md, "Power supply"). The Mark IIC+ uses solid-state rectification.
    // Real Mark IIC+ runs at ~470V plates from a large transformer; the DIY-Fever clone's 300-0-300V specs are
    // for a preamp-only build. Preamp decoupling: B+1 → 10K → B+2 → 1K → B+3 → 1K → B+4. ----
    constexpr double railPlatesNominal = 470.0;
    constexpr double railScreensNominal = 465.0;
    constexpr double rectifierResistance = 60.0;
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 220.0e3;
    constexpr double screenResistor = 475.0;

    // ---- reduced-order power stage sag table (Twin Reverb's data scaled from 497V to 470V) ----
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 470.0, 470.0, 470.0, 470.0, 469.99, 469.98, 469.96, 469.91,
                                                 469.83, 469.58, 469.22, 468.43, 467.33, 465.32, 461.57, 455.26,
                                                 447.48, 431.67, 417.12, 413.16 };

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
        // kg1 softened 40x for closed-loop stability (split with the power pentode below).
        // Same proven pattern as SLO-100 and JCM800 -- start at 40x, adjust only if the silence
        // test fails or the reference gain is unreasonably collapsed.
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

    // Output transformer: 4x6L6GC, ~100 W. Same class as the SLO-100.
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    // NFB: slightly less than SLO-100 (150K vs 175K) for a more aggressive lead character.
    constexpr double feedbackResistor = 150.0e3;

    constexpr double biasSupplyVolts = -55.0;

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2;
    constexpr double powerTheta = 0.9;

    // ---- preamp helper ----
    struct PreampBuild
    {
        NodalCircuit& c;
        int srcV1 = 0, srcV2 = 0, srcV3 = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0, rBrightSeries = 0, rV1bSeries = 0;
        NodalCircuit::Node plateV1a = 0, plateV1b = 0, plateV2a = 0, plateV2b = 0, plateV3a = 0, follower = 0, gainWiper = 0;
        NodalCircuit::Node nodeV1 = 0, nodeV2 = 0, nodeV3 = 0;
    };

    /** The Mark IIC+ Lead preamp: FIVE cascaded 12AX7 gain stages (V1a -> V1b -> V2a -> V2b -> V3a) plus a
        V3b cathode follower driving the tone stack.

        Key voicing elements:
        - 1nF coupling from V1a to the Gain pot (treble-forward character from the first stage)
        - 120pF enhanced Miller cap on V2a (deliberate HF roll-off in the high-gain stages)
        - 68K/3.3K voltage divider between V2a and V2b (precise gain structuring)
        - 87K unbypassed cathode on V2b (very low gain, tone-shaping stage)
        - 250pF enhanced Miller on V2b (further HF roll-off before the 547pF coupling)
        - 547pF silver mica coupling V2b→V3a (THE famous IIC+ cap -- only treble reaches V3a)

        Supply taps: B+4 (v1Guess) for V1a, B+3 (v2Guess) for V1b/V2a/V2b, B+2 (v3Guess) for V3a/V3b. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double v1Guess, double v2Guess, double v3Guess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b { c };

        const auto vcc1 = c.addNode(), vcc2 = c.addNode(), vcc3 = c.addNode(), in = c.addNode();
        b.nodeV1 = vcc1;
        b.nodeV2 = vcc2;
        b.nodeV3 = vcc3;
        b.srcV1 = c.addSource (vcc1, v1Guess);
        b.srcV2 = c.addSource (vcc2, v2Guess);
        b.srcV3 = c.addSource (vcc3, v3Guess);
        b.srcIn = c.addSource (in, 0.0);

        // V1a: 150K plate (B+4), 1M grid leak, 1.5K/0.47µF cathode. The first gain stage -- full cathode
        // bypass for maximum gain, feeding the 1nF treble-forward coupling into the Gain pot.
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plateV1a = c.addNode();
        c.addResistor (in, g1, 68.0e3);              // grid stopper
        c.addResistor (g1, gnd, 1.0e6);              // grid leak
        c.addTriode (b.plateV1a, g1, k1, triode12AX7());
        c.addCapacitor (g1, b.plateV1a, cgp);
        c.addResistor (vcc1, b.plateV1a, 150.0e3);
        c.addResistor (k1, gnd, 1.5e3);
        c.addCapacitor (k1, gnd, 0.47e-6);
        c.setInitialGuess (b.plateV1a, 250.0);
        c.setInitialGuess (k1, 1.5);

        // Gain (Lead Drive, 1MA audio taper): V1a plate → 1nF → Gain pot.
        // The 1nF is THE defining voicing element -- it rolls off bass from the first stage, creating the
        // Mark series' characteristic treble-forward lead tone at all gain settings.
        const auto gainIn = c.addNode(), gainWiper = c.addNode();
        c.addCapacitor (b.plateV1a, gainIn, 1.0e-9); // 1nF ceramic -- the Mark series character cap
        b.rGainTop = c.addResistor (gainIn, gainWiper, 1.0e6);
        b.rGainBot = c.addResistor (gainWiper, gnd, 1.0e6);
        {
            const auto brightMid = c.addNode();
            b.rBrightSeries = c.addResistor (gainIn, brightMid, 100.0e6);
            c.addCapacitor (brightMid, gainWiper, 470.0e-12);
        }

        // V1b: 100K plate (B+3), grid from gain wiper through 100K series, 3.3M grid leak, 1.5K/6.8µF cathode.
        const auto g2 = c.addNode(), k2 = c.addNode();
        b.plateV1b = c.addNode();
        b.gainWiper = gainWiper;
        b.rV1bSeries = c.addResistor (gainWiper, g2, 100.0e3); // switchable: 100M for Clean
        c.addResistor (g2, gnd, 3.3e6);              // 3.3M grid leak
        c.addTriode (b.plateV1b, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV1b, cgp);
        c.addResistor (vcc2, b.plateV1b, 100.0e3);
        c.addResistor (k2, gnd, 1.5e3);
        c.addCapacitor (k2, gnd, 6.8e-6);
        c.setInitialGuess (b.plateV1b, 300.0);
        c.setInitialGuess (k2, 1.5);

        // V2a: 82K plate (B+3), 680K series from 47nF coupling, 470K grid leak, 120pF enhanced Miller (deliberate
        // HF roll-off in this high-gain stage), 1.5K/2.2µF cathode.
        const auto g3 = c.addNode(), k3 = c.addNode(), coup2 = c.addNode();
        b.plateV2a = c.addNode();
        c.addCapacitor (b.plateV1b, coup2, 47.0e-9); // 47nF coupling from V1b
        c.addResistor (coup2, g3, 680.0e3);          // 680K series into V2a grid
        c.addResistor (g3, gnd, 470.0e3);            // 470K grid leak
        c.addTriode (b.plateV2a, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateV2a, 120.0e-12);  // 120pF enhanced Miller cap
        c.addResistor (vcc2, b.plateV2a, 82.0e3);
        c.addResistor (k3, gnd, 1.5e3);
        c.addCapacitor (k3, gnd, 2.2e-6);
        c.setInitialGuess (b.plateV2a, 320.0);
        c.setInitialGuess (k3, 1.5);

        // V2a → V2b coupling: 68K/3.3K voltage divider attenuates V2a's signal to ~4.6% before V2b.
        // This is deliberate gain structuring -- V2b is a tone-shaping stage, not a gain stage.
        const auto attenNode = c.addNode();
        c.addResistor (b.plateV2a, attenNode, 68.0e3);  // 68K series from V2a plate
        c.addResistor (attenNode, gnd, 3.3e3);           // 3.3K to ground

        // V2b: 270K plate (B+3), 470K grid leak, 87K unbypassed cathode (the stage's defining element --
        // very low gain ~2.5x, mostly tone shaping), 250pF enhanced Miller.
        const auto g4 = c.addNode(), k4 = c.addNode();
        b.plateV2b = c.addNode();
        c.addCapacitor (attenNode, g4, 22.0e-9);     // 22nF coupling from attenuator
        c.addResistor (g4, gnd, 470.0e3);            // 470K grid leak
        c.addTriode (b.plateV2b, g4, k4, triode12AX7());
        c.addCapacitor (g4, b.plateV2b, 250.0e-12);  // 250pF enhanced Miller
        c.addResistor (vcc2, b.plateV2b, 270.0e3);
        c.addResistor (k4, gnd, 87.0e3);             // 87K UNBYPASSED -- V2b barely amplifies
        c.setInitialGuess (b.plateV2b, 415.0);       // near supply due to extremely low current (~25µA)
        c.setInitialGuess (k4, 2.0);

        // V3a: 100K plate (B+2), 220K grid leak, 1.5K unbypassed cathode.
        // The famous 547pF silver mica is the ONLY coupling between V2b and V3a -- it passes only treble
        // (HP corner ~1.3 kHz with 220K grid leak), creating the tight, articulate lead tone.
        const auto g5 = c.addNode(), k5 = c.addNode();
        b.plateV3a = c.addNode();
        c.addCapacitor (b.plateV2b, g5, 547.0e-12);  // 547pF silver mica -- THE famous IIC+ cap
        c.addResistor (g5, gnd, 220.0e3);            // 220K grid leak
        c.addTriode (b.plateV3a, g5, k5, triode12AX7());
        c.addCapacitor (g5, b.plateV3a, cgp);
        c.addResistor (vcc3, b.plateV3a, 100.0e3);
        c.addResistor (k5, gnd, 1.5e3);              // unbypassed
        c.setInitialGuess (b.plateV3a, 300.0);
        c.setInitialGuess (k5, 1.5);

        // V3b: cathode follower (same 12AX7) -- drives the tone stack through a low-impedance output.
        b.follower = c.addNode();
        c.addFollower (b.plateV3a, b.follower, followerDrop);
        return b;
    }
}

MarkIICPlusStyleAmplifierProcessor::MarkIICPlusStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        "mk2c_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("Clean") : juce::String ("Lead"); }));
    auto gain = make ("mk2c_gain", "Gain", 0.5f);
    auto makeSw = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            { return v < 0.5f ? juce::String ("Off") : juce::String ("On"); }));
    };
    auto pullBright = makeSw ("mk2c_pull_bright", "Pull Bright");
    auto pullDeep = makeSw ("mk2c_pull_deep", "Pull Deep");
    auto pullShift = makeSw ("mk2c_pull_shift", "Pull Shift");
    auto treble = make ("mk2c_treble", "Treble", 0.5f);
    auto mid = make ("mk2c_mid", "Mid", 0.5f);
    auto bass = make ("mk2c_bass", "Bass", 0.5f);
    auto presence = make ("mk2c_presence", "Presence", 0.3f);
    auto master = make ("mk2c_master", "Master", 0.5f);
    auto output = make ("mk2c_output", "Output", 0.5f);
    auto geqMake = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                const float db = (v - 0.5f) * 24.0f;
                return (db >= 0.0f ? juce::String ("+") : juce::String()) + juce::String (db, 1) + " dB";
            }));
    };
    auto geq80 = geqMake ("mk2c_geq80", "80 Hz");
    auto geq240 = geqMake ("mk2c_geq240", "240 Hz");
    auto geq750 = geqMake ("mk2c_geq750", "750 Hz");
    auto geq2200 = geqMake ("mk2c_geq2200", "2200 Hz");
    auto geq6600 = geqMake ("mk2c_geq6600", "6600 Hz");
    auto power = make ("mk2c_power", "Power Drive", 0.5f);
    auto bias = make ("mk2c_bias", "Bias", 0.5f);
    auto feel = make ("mk2c_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "mk2c_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    channelParam = channel.get();
    gainParam = gain.get();
    pullBrightParam = pullBright.get();
    pullDeepParam = pullDeep.get();
    pullShiftParam = pullShift.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    masterParam = master.get();
    outputParam = output.get();
    geqParams[0] = geq80.get();
    geqParams[1] = geq240.get();
    geqParams[2] = geq750.get();
    geqParams[3] = geq2200.get();
    geqParams[4] = geq6600.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "mk2cplus", "Mark IIC+-Style Amplifier", "|", std::move (channel));
    group->addChild (std::move (gain));
    group->addChild (std::move (pullBright));
    group->addChild (std::move (pullDeep));
    group->addChild (std::move (pullShift));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (master));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("mk2c_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    group->addChild (std::move (page2));
    auto page3 = std::make_unique<juce::AudioProcessorParameterGroup> ("mk2c_geq", "Graphic EQ", "|",
        std::move (geq80));
    page3->addChild (std::move (geq240));
    page3->addChild (std::move (geq750));
    page3->addChild (std::move (geq2200));
    page3->addChild (std::move (geq6600));
    group->addChild (std::move (page3));
    parameters = std::move (group);
}

void MarkIICPlusStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // Solid-state rectification → choke-filtered RC chain. The preamp supply follows the DIY-Fever schematic:
    // B+1 (16µF, 220K bleeder) → 10K → B+2 (10µF) → 1K → B+3 (10µF) → 1K → B+4 (10µF).
    // B+4 feeds V1a, B+3 feeds V1b/V2a/V2b, B+2 feeds V3a/V3b. B+1/PI supply is upstream.
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~470V)
        const auto nl = c.addNode();
        ch.sB = c.addNode();                           // screens rail (~465V)
        ch.sC = c.addNode();                           // B+1 / PI supply (~430V)
        ch.sD = c.addNode();                           // B+2 / V3 (~420V)
        ch.sE = c.addNode();                           // B+3 / V1b, V2a, V2b (~418V)
        ch.sF = c.addNode();                           // B+4 / V1a (~416V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 200.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 200.0e-6);
        c.addResistor (ch.sB, gnd, bleeder);
        c.addResistor (ch.sB, ch.sC, 10.0e3);         // screens → B+1
        c.addCapacitor (ch.sC, gnd, 16.0e-6);          // 16µF (from schematic)
        c.addResistor (ch.sC, ch.sD, 10.0e3);          // B+1 → B+2
        c.addCapacitor (ch.sD, gnd, 10.0e-6);
        c.addResistor (ch.sD, ch.sE, 1.0e3);           // B+2 → B+3
        c.addCapacitor (ch.sE, gnd, 10.0e-6);
        c.addResistor (ch.sE, ch.sF, 1.0e3);           // B+3 → B+4
        c.addCapacitor (ch.sF, gnd, 10.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);     // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.012);    // power tubes screen current
        ch.iC = c.addCurrentSource (ch.sC, -0.003);    // PI current draw
        ch.iD = c.addCurrentSource (ch.sD, -0.002);    // V3a/V3b preamp current
        ch.iE = c.addCurrentSource (ch.sE, -0.003);    // V1b/V2a/V2b preamp current
        ch.iF = c.addCurrentSource (ch.sF, -0.001);    // V1a preamp current
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 430.0);
        c.setInitialGuess (ch.sD, 420.0);
        c.setInitialGuess (ch.sE, 418.0);
        c.setInitialGuess (ch.sF, 416.0);
    }

    // ================================================================ preamp (two-pass for cathode follower DC offset)
    {
        auto probe = buildPreamp (ch.pre, 0.0, 416.0, 418.0, 420.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plateV3a);
        const double drop = plate - cathodeFollowerDc (420.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop, 416.0, 418.0, 420.0);
        ch.pSrcV1 = b.srcV1;
        ch.pSrcV2 = b.srcV2;
        ch.pSrcV3 = b.srcV3;
        ch.pSrcIn = b.srcIn;
        ch.rGainTop = b.rGainTop;
        ch.rGainBot = b.rGainBot;
        ch.rBrightSeries = b.rBrightSeries;
        ch.rV1bSeries = b.rV1bSeries;
        ch.pGainWiper = b.gainWiper;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV1b = b.plateV1b;
        ch.pPlateV2a = b.plateV2a;
        ch.pPlateV2b = b.plateV2b;
        ch.pPlateV3a = b.plateV3a;
        ch.pFollower = b.follower;
    }

    // ================================================================ tone stack (Fender TMB, always built and solved)
    // Mark IIC+ tone stack: 47K slope, 250pF treble, 22nF bass, 22nF mid. Treble 250K, Mid 25K, Bass 250K (linear).
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        const auto ti = c.addNode(), top = c.addNode(), nB = c.addNode(), nT = c.addNode(), nM = c.addNode(), nMw = c.addNode();
        ch.wToneIn = ti;
        ch.wTone = c.addNode();
        c.addResistor (cf, ti, cathodeFollowerImpedance);
        c.addCapacitor (ti, top, 250.0e-12);             // 250pF treble coupling cap
        c.addResistor (ti, nB, 47.0e3);                  // 47K slope resistor
        ch.rTrebleTop = c.addResistor (top, ch.wTone, 125.0e3);   // Treble 250K linear, split
        ch.rTrebleBottom = c.addResistor (ch.wTone, nT, 125.0e3);
        ch.capMidBass = c.addCapacitor (nB, nT, 22.0e-9);  // 22nF bass coupling cap (Pull Shift: 4.7nF)
        ch.rBass = c.addResistor (nT, nM, 125.0e3);      // Bass 250K linear (rheostat)
        ch.rMidTop = c.addResistor (nM, nMw, 12.5e3);    // Mid 25K linear, split
        ch.rMidBottom = c.addResistor (nMw, gnd, 12.5e3);
        ch.capMidMid = c.addCapacitor (nB, nMw, 22.0e-9); // 22nF mid coupling cap (Pull Shift: 4.7nF)

        // Master: 250KA rheostat from tone stack output to ground, before the PI coupling cap.
        ch.rMaster = c.addResistor (ch.wTone, gnd, 125.0e3);
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 430.0);            // PI supply from B+1
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter: 12AX7 long-tailed pair. .022µF from tone stack out into grid 1 (10K series + 1M
        // grid leak); 470Ω tail; 82K/81K plate loads; 47pF between plates; 1M grid leak on g2 from NFB node.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nm = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nm;
        ch.wFeedback = fp;
        c.addCapacitor (ch.wTone, g1, 0.022e-6);
        c.addResistor (g1, nm, 1.0e6);
        c.addResistor (g2, fp, 1.0e6);
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 81.0e3);
        c.addCapacitor (pa, pb, 47.0e-12);
        c.addResistor (k, nm, 470.0);
        c.addResistor (nm, gnd, 10.0e3);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 30.0);
        c.setInitialGuess (nm, 27.0);
        c.setInitialGuess (g1, 27.0);
        c.setInitialGuess (g2, 27.0);
        c.setInitialGuess (fp, 2.0);

        // Power amplifier: 4 × 6L6GC as two push-pull pairs. .047µF couplings, 1.1K grid stoppers (2.2K/pair),
        // 220K grid leaks to the fixed bias rail.
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
        c.addResistor (g3, nb, 220.0e3);
        c.addResistor (g4, nb, 220.0e3);
        c.addResistor (g3, g3s, 1.1e3);
        c.addResistor (g4, g4s, 1.1e3);
        c.addResistor (vc18, nb, 15.0e3);
        c.addCapacitor (nb, gnd, 10.0e-6);
        c.addResistor (nb, nbt, 100.0e3);
        ch.rBiasTrim = c.addResistor (nbt, gnd, 220.0e3);
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6L6PairPower(), railScreensNominal);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6L6PairPower(), railScreensNominal);

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

        // NFB: from speaker terminal through 150K to PI tail, with Presence 25K shunt and stray cap.
        const auto wp = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, fp, feedbackResistor);
        ch.rPresTop = c.addResistor (fp, wp, 12.5e3);
        ch.rPresBottom = c.addResistor (wp, gnd, 12.5e3);
        c.addCapacitor (fp, wp, 0.1e-6);
        c.addCapacitor (fp, gnd, 1.5e-9);             // stray for HF stability
        {
            const auto deepMid = c.addNode();
            ch.rDeepSeries = c.addResistor (fp, deepMid, 100.0e6);
            c.addCapacitor (deepMid, gnd, 820.0e-12);
        }

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -50.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void MarkIICPlusStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 250.0e3 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
    const double presBottom = juce::jmax (1.0, 25.0e3 * (1.0 - k.presence));
    const double presTop = juce::jmax (1.0, 25.0e3 - presBottom);
    const double masterR = juce::jmax (1.0, 250.0e3 * pots::audio (k.master));
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
        ch.pre.setResistance (ch.rGainBot, gainBottom);
        ch.pre.setResistance (ch.rGainTop, juce::jmax (1.0, 1.0e6 - gainBottom));
        ch.power.setResistance (ch.rTrebleTop, trebleTop);
        ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.power.setResistance (ch.rBass, bassR);
        ch.power.setResistance (ch.rMidTop, midTop);
        ch.power.setResistance (ch.rMidBottom, midBottom);
        ch.power.setResistance (ch.rMaster, masterR);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresTop, presTop);
            ch.power.setResistance (ch.rPresBottom, presBottom);
            ch.power.setResistance (ch.rFeedback, feedbackR);
            ch.power.setResistance (ch.rBiasTrim, trim);
        }
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void MarkIICPlusStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void MarkIICPlusStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void MarkIICPlusStyleAmplifierProcessor::recover (Channel& ch) const
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

void MarkIICPlusStyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! supplyCurrentFrozen)
    {
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 1.6, ch.sumPlate / n));
        ch.supply.setCurrentSource (ch.iB, -juce::jlimit (0.0, 0.3, ch.sumScreen / n));
    }
    ch.supply.solveSample();
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
    {
        ch.power.setSource (ch.wSrcCt, rail (ch.sA, 560.0));
        ch.power.setSource (ch.wSrcPi, rail (ch.sC, 520.0));
    }
    ch.pre.setSource (ch.pSrcV3, rail (ch.sD, 480.0));   // V3a/V3b from B+2
    ch.pre.setSource (ch.pSrcV2, rail (ch.sE, 480.0));   // V1b/V2a/V2b from B+3
    ch.pre.setSource (ch.pSrcV1, rail (ch.sF, 480.0));   // V1a from B+4
    ch.vScreen = rail (ch.sB, 560.0);
}

double MarkIICPlusStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double MarkIICPlusStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
        case Probe::v1bPlate: return ch.pre.voltage (ch.pPlateV1b);
        case Probe::v2aPlate: return ch.pre.voltage (ch.pPlateV2a);
        case Probe::v2bPlate: return ch.pre.voltage (ch.pPlateV2b);
        case Probe::v3aPlate: return ch.pre.voltage (ch.pPlateV3a);
        case Probe::followerOut: return ch.pre.voltage (ch.pFollower);
        case Probe::toneStackOut: return ch.power.voltage (ch.wTone);
        case Probe::phaseInverterGrid: return ch.power.voltage (ch.wGridA);
        case Probe::phaseInverterPlateA: return ch.power.voltage (ch.wPlateA);
        case Probe::phaseInverterPlateB: return ch.power.voltage (ch.wPlateB);
        case Probe::phaseInverterTail: return ch.power.voltage (ch.wTail);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::powerGridA: return ch.power.voltage (ch.wPowerGridA);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::biasNode: return ch.power.voltage (ch.wBias);
        case Probe::feedbackNode: return ch.power.voltage (ch.wFeedback);
    }
    return 0.0;
}

double MarkIICPlusStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int MarkIICPlusStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void MarkIICPlusStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double MarkIICPlusStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double MarkIICPlusStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void MarkIICPlusStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedMaster, masterParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), masterParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iPiRun = 0.003, iV3Run = 0.002, iV2Run = 0.003, iV1Run = 0.001, ipRun = 0.16, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sD));
            ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sE));
            ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sF));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.followerDc = ch.pre.voltage (ch.pFollower);
            ch.preampTapDc[0] = ch.pre.voltage (ch.pGainWiper);
            ch.preampTapDc[1] = ch.followerDc;

            ch.power.setSource (ch.wSrcCf, ch.followerDc);
            ch.power.setInitialGuess (ch.wToneIn, ch.followerDc);
            ch.vScreen = ch.supply.voltage (ch.sB);
            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0, iPi = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
                ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
                ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
                ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
            }
            passOk = ch.power.prepare (newSampleRate) && passOk;
            ch.power.solveSample();

            if (! reducedOrder)
            {
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                const double vPi = ch.supply.voltage (ch.sC);
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 81.0e3;
            }
            // Preamp current draw computation: V3 (V3a from B+2), V2 (V1b/V2a/V2b from B+3), V1 (V1a from B+4)
            const double vV3 = ch.supply.voltage (ch.sD);
            const double iV3 = (vV3 - ch.pre.voltage (ch.pPlateV3a)) / 100.0e3 + ch.followerDc / 100.0e3;
            const double vV2 = ch.supply.voltage (ch.sE);
            const double iV2 = (vV2 - ch.pre.voltage (ch.pPlateV1b)) / 100.0e3
                             + (vV2 - ch.pre.voltage (ch.pPlateV2a)) / 82.0e3
                             + (vV2 - ch.pre.voltage (ch.pPlateV2b)) / 270.0e3;
            const double vV1 = ch.supply.voltage (ch.sF);
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateV1a)) / 150.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV3Run += 0.5 * (iV3 - iV3Run);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iV3Run);
            ch.supply.setCurrentSource (ch.iE, -iV2Run);
            ch.supply.setCurrentSource (ch.iF, -iV1Run);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iV3Run + iV2Run + iV1Run + (ch.vScreen / bleeder);
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
        ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sD));
        ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sE));
        ch.pre.setSource (ch.pSrcV1, ch.supply.voltage (ch.sF));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmToneState = 0.0;
    }
    updatePots (lastKnobs);

    for (int b = 0; b < geqBands; ++b)
    {
        geqCoeffs[b] = {};
        geqState[0][b] = {};
        geqState[1][b] = {};
        lastGeqSliders[b] = 0.5f;
    }
    geqUpdateCounter = 0;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void MarkIICPlusStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
        const float ms = smoothedMaster.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        const int channelSel = juce::roundToInt (channelParam->get()) >= 1 ? 1 : 0;
        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, ms, pw, bi, fe, speakerChoice });
            const bool brightOn = pullBrightParam->get() >= 0.5f;
            const bool deepOn = pullDeepParam->get() >= 0.5f;
            const bool shiftOn = pullShiftParam->get() >= 0.5f;
            for (auto& ch : channels)
            {
                ch.pre.setResistance (ch.rV1bSeries, channelSel == 0 ? 100.0e6 : 100.0e3);
                ch.pre.setResistance (ch.rBrightSeries, brightOn ? 1.0 : 100.0e6);
                if (! reducedOrder)
                    ch.power.setResistance (ch.rDeepSeries, deepOn ? 1.0 : 100.0e6);
                ch.power.setCapacitance (ch.capMidBass, shiftOn ? 4.7e-9 : 22.0e-9);
                ch.power.setCapacitance (ch.capMidMid, shiftOn ? 4.7e-9 : 22.0e-9);
            }
        }

        if (++geqUpdateCounter >= controlInterval)
        {
            geqUpdateCounter = 0;
            bool needUpdate = false;
            for (int b = 0; b < geqBands; ++b)
            {
                const float v = geqParams[b]->get();
                if (v != lastGeqSliders[b])
                {
                    lastGeqSliders[b] = v;
                    needUpdate = true;
                }
            }
            if (needUpdate)
            {
                constexpr double Q = 1.5;
                for (int b = 0; b < geqBands; ++b)
                {
                    const double dB = (lastGeqSliders[b] - 0.5) * 24.0;
                    if (std::abs (dB) < 0.01)
                    {
                        geqCoeffs[b] = {};
                        continue;
                    }
                    const double A = std::pow (10.0, dB / 40.0);
                    const double w0 = 2.0 * juce::MathConstants<double>::pi * geqFreqs[b] / sampleRate;
                    const double sinW = std::sin (w0);
                    const double cosW = std::cos (w0);
                    const double alpha = sinW / (2.0 * Q);
                    const double a0 = 1.0 + alpha / A;
                    geqCoeffs[b].b0 = (1.0 + alpha * A) / a0;
                    geqCoeffs[b].b1 = (-2.0 * cosW) / a0;
                    geqCoeffs[b].b2 = (1.0 - alpha * A) / a0;
                    geqCoeffs[b].a1 = (-2.0 * cosW) / a0;
                    geqCoeffs[b].a2 = (1.0 - alpha / A) / a0;
                }
            }
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

            const NodalCircuit::Node preNode = channelSel == 0 ? ch.pGainWiper : ch.pFollower;
            const double preDc = ch.preampTapDc[channelSel];
            const double preAc = ch.pre.voltage (preNode) - preDc;
            ch.power.setSource (ch.wSrcCf, preDc + masterGain * cathodeFollowerGain * preAc);
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
                ++ch.sumCount;
            }
            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            double toneOut = ch.power.voltage (ch.wTone);
            for (int b = 0; b < geqBands; ++b)
            {
                const auto& c = geqCoeffs[b];
                auto& s = geqState[chIdx][b];
                const double y = c.b0 * toneOut + s.s1;
                s.s1 = c.b1 * toneOut - c.a1 * y + s.s2;
                s.s2 = c.b2 * toneOut - c.a2 * y;
                toneOut = y;
            }
            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, toneOut) : ch.power.voltage (ch.wOut);
            constexpr double saneLimit = 150.0;
            const bool sane = std::isfinite (speakerVolts) && std::abs (speakerVolts) < saneLimit;
            ok = ok && sane;
            if (chIdx == 0 && sane)
                worstSaneVolts = juce::jmax (worstSaneVolts, std::abs (speakerVolts));
            if (chIdx == 0 && ! sane && okPre && ok2)
                ++sanityRejects;

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
                lastSampleOk = ok;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void MarkIICPlusStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
