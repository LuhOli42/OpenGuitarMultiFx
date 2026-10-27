#include "EVH5150StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/EVH5150.md). Factory Peavey EVH 5150 schematic.
    // Rails: A=465V plates, B=~441V screens (through R210=400Ω), C=~435V (PI/V3), D=~420V (V2), E=~410V (V1).
    // Chain: Rect → A (220µF, 100K bleeder) → R210=400Ω → B (220µF, screens) → 4.7K → C (30µF, PI/V3) →
    //        10K → D (30µF, V2) → 22K → E (30µF, V1). ----
    constexpr double railPlatesNominal = 465.0;
    constexpr double rectifierResistance = 60.0;
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 100.0e3;
    constexpr double screenResistor = 50.0;  // R207/R209 (R203/R205): 100 ohm per tube, two tubes per modelled pair

    // ---- reduced-order power stage sag table (Twin Reverb's data scaled from 497V to 465V) ----
    constexpr int bmSagPoints = 20;
    constexpr double bmSagDrive[bmSagPoints] = { 0.010814, 0.026337, 0.052278, 0.104273, 0.208386, 0.364564, 0.520879, 0.781423,
                                                  1.041984, 1.563152, 2.084242, 2.865622, 3.646097, 4.685028, 6.238472, 8.290907,
                                                  10.236657, 14.372645, 21.062285, 30.624787 };
    constexpr double bmSagRail[bmSagPoints] = { 465.0, 465.0, 465.0, 465.0, 464.99, 464.98, 464.96, 464.91,
                                                 464.83, 464.58, 464.21, 463.40, 462.26, 460.18, 456.32, 449.82,
                                                 441.84, 425.88, 411.09, 407.21 };

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

        return p;
    }

    constexpr double cgp = 1.7e-12;

    // Output transformer: 4x6L6GC, ~120 W. Same class as the SLO-100 / Mark IIC+ / Dual Rectifier.
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    constexpr double biasSupplyVolts = -52.0; // 6L6GC bias

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1; // 8 ohm matched (J1=8 ohm on schematic)
    constexpr double powerTheta = 0.9;

    // NFB: R56 39K from the 8-ohm tap (T2's "W" lead, the J1 jack) to the Presence/Resonance network.
    constexpr double feedbackResistor = 39.0e3;
}

/** Block A: input (R22 68K / R25 1M), V1A (220K, 1.82K || 1uF), C6 .022 into BOTH pre networks -- ULTRA: R29 470K ||
    C17 .0022 series, R41 1M down, VR2 1M with C19 .001 || R40 1M across its top leg; CLEAN: R19 470K || C51 270p, R21 1M,
    VA1 1M with C27 180p across its top leg -- K2A picking one wiper into R82 470K and V1B (100K, 1.82K || 1uF).
    C2 .022 -> R6 470K -> R9 1M is block B's input network, carried here as V1B's load (V2A draws no grid current
    behind its 39K cathode). Simplified: K4A's bright/drive voicing paths (C13/R43, C10/R75, R23/C63) and V1B's
    C26 22uF + R18 47K leg (in series with 47K, it changes nothing above a few hertz). */
void EVH5150StyleAmplifierProcessor::buildPreA (Channel& ch) const
{
    auto& c = ch.preA;
    const auto gnd = NodalCircuit::ground;
    const auto v1 = c.addNode(), in = c.addNode();
    ch.aSrcV1 = c.addSource (v1, 330.0);
    ch.aSrcIn = c.addSource (in, 0.0);

    const auto g1 = c.addNode(), k1 = c.addNode();
    ch.aPlateV1a = c.addNode();
    c.addResistor (in, g1, 68.0e3);                       // R22
    c.addResistor (g1, gnd, 1.0e6);                       // R25
    c.addTriode (ch.aPlateV1a, g1, k1, triode12AX7());
    c.addCapacitor (g1, ch.aPlateV1a, cgp);
    c.addResistor (v1, ch.aPlateV1a, 220.0e3);            // R2
    c.addResistor (k1, gnd, 1.82e3);                      // R12
    c.addCapacitor (k1, gnd, 1.0e-6);                     // C3
    c.setInitialGuess (ch.aPlateV1a, 170.0);
    c.setInitialGuess (k1, 1.5);

    const auto na = c.addNode();
    c.addCapacitor (ch.aPlateV1a, na, 22.0e-9);           // C6

    // ULTRA pre network
    const auto nb = c.addNode(), wu = c.addNode();
    c.addResistor (na, nb, 470.0e3);                      // R29
    c.addCapacitor (na, nb, 2.2e-9);                      // C17
    c.addResistor (nb, gnd, 1.0e6);                       // R41
    ch.rUltraTop = c.addResistor (nb, wu, 500.0e3);       // VR2 ULTRA PRE
    ch.rUltraBot = c.addResistor (wu, gnd, 500.0e3);
    c.addResistor (nb, wu, 1.0e6);                        // R40
    c.addCapacitor (nb, wu, 1.0e-9);                      // C19

    // CLEAN pre network
    const auto nd = c.addNode(), ne = c.addNode(), wc = c.addNode();
    c.addResistor (na, nd, 470.0e3);                      // R19
    c.addCapacitor (na, nd, 270.0e-12);                   // C51
    c.addResistor (nd, ne, 1.0e6);                        // R21
    ch.rCleanTop = c.addResistor (ne, wc, 500.0e3);       // VA1 CLEAN PRE
    ch.rCleanBot = c.addResistor (wc, gnd, 500.0e3);
    c.addCapacitor (ne, wc, 180.0e-12);                   // C27

    // K2A
    const auto k2 = c.addNode();
    ch.rSelUltra = c.addResistor (wu, k2, 1.0);
    ch.rSelClean = c.addResistor (wc, k2, 1.0e9);

    const auto g2 = c.addNode(), kk2 = c.addNode();
    ch.aPlateV1b = c.addNode();
    c.addResistor (k2, g2, 470.0e3);                      // R82
    c.addTriode (ch.aPlateV1b, g2, kk2, triode12AX7());
    c.addCapacitor (g2, ch.aPlateV1b, cgp);
    c.addResistor (v1, ch.aPlateV1b, 100.0e3);            // R3
    c.addResistor (kk2, gnd, 1.82e3);                     // R17
    c.addCapacitor (kk2, gnd, 1.0e-6);                    // C4
    c.setInitialGuess (ch.aPlateV1b, 200.0);
    c.setInitialGuess (kk2, 1.5);

    // block B's input network as the load
    const auto lc = c.addNode(), lx = c.addNode();
    c.addCapacitor (ch.aPlateV1b, lc, 22.0e-9);           // C2
    c.addResistor (lc, lx, 470.0e3);                      // R6
    c.addResistor (lx, gnd, 1.0e6);                       // R9
}

/** Block B: driven by V1B's plate. C2 .022 -> R6 470K -> V2A grid (R9 1M down; R20 220K down too on Rhythm, K1B with
    Crunch off) -> V2A (100K, 39K unbypassed: the cold clipper) -> C1 .022 -> C48 .001 || R11 330K down -> R7 220K ->
    V2B (220K, 1.82K || 1uF). R32 1M -> C57 .022 -> R101 100K is block C's input network, carried as V2B's load. */
void EVH5150StyleAmplifierProcessor::buildPreB (Channel& ch) const
{
    auto& c = ch.preB;
    const auto gnd = NodalCircuit::ground;
    const auto v1 = c.addNode(), v2 = c.addNode(), in = c.addNode();
    ch.bSrcV1 = c.addSource (v1, 330.0);
    ch.bSrcV2 = c.addSource (v2, 380.0);
    ch.bSrcIn = c.addSource (in, 200.0);

    const auto nc = c.addNode(), g3 = c.addNode(), k3 = c.addNode();
    ch.bPlateV2a = c.addNode();
    c.addCapacitor (in, nc, 22.0e-9);                     // C2
    c.addResistor (nc, g3, 470.0e3);                      // R6
    c.addResistor (g3, gnd, 1.0e6);                       // R9
    ch.rCrunchShunt = c.addResistor (g3, gnd, 1.0e9);     // R20 via K1B
    c.addTriode (ch.bPlateV2a, g3, k3, triode12AX7());
    c.addCapacitor (g3, ch.bPlateV2a, cgp);
    c.addResistor (v1, ch.bPlateV2a, 100.0e3);            // R1
    c.addResistor (k3, gnd, 39.0e3);                      // R15
    c.setInitialGuess (ch.bPlateV2a, 290.0);
    c.setInitialGuess (k3, 1.2);

    const auto ny = c.addNode(), g4 = c.addNode(), k4 = c.addNode();
    ch.bPlateV2b = c.addNode();
    c.addCapacitor (ch.bPlateV2a, ny, 22.0e-9);           // C1
    c.addCapacitor (ny, gnd, 1.0e-9);                     // C48
    c.addResistor (ny, gnd, 330.0e3);                     // R11
    c.addResistor (ny, g4, 220.0e3);                      // R7
    c.addTriode (ch.bPlateV2b, g4, k4, triode12AX7());
    c.addCapacitor (g4, ch.bPlateV2b, cgp);
    c.addResistor (v2, ch.bPlateV2b, 220.0e3);            // R4
    c.addResistor (k4, gnd, 1.82e3);                      // R13
    c.addCapacitor (k4, gnd, 1.0e-6);                     // C5
    c.setInitialGuess (ch.bPlateV2b, 220.0);
    c.setInitialGuess (k4, 1.0);

    const auto la = c.addNode(), lb = c.addNode();
    c.addResistor (ch.bPlateV2b, la, 1.0e6);              // R32
    c.addCapacitor (la, lb, 22.0e-9);                     // C57
    c.addResistor (lb, gnd, 100.0e3);                     // R101
}

/** Block C: driven by V2B's plate. R32 1M -> C57 .022 -> V5B grid (R101 100K down) -> V5B (220K, 2.2K unbypassed).
    The load is block D's input as V5B sees it: R87 1M -> C56 .022 into V5A's grid, which its 1M plate-to-grid
    feedback holds near ~40K (1M / (1 + the stage's gain of ~23), || R91 1M). */
void EVH5150StyleAmplifierProcessor::buildPreC (Channel& ch) const
{
    auto& c = ch.preC;
    const auto gnd = NodalCircuit::ground;
    const auto v2 = c.addNode(), in = c.addNode();
    ch.cSrcV2 = c.addSource (v2, 380.0);
    ch.cSrcIn = c.addSource (in, 220.0);

    const auto na = c.addNode(), g5 = c.addNode(), k5 = c.addNode();
    ch.cPlateV5b = c.addNode();
    c.addResistor (in, na, 1.0e6);                        // R32
    c.addCapacitor (na, g5, 22.0e-9);                     // C57
    c.addResistor (g5, gnd, 100.0e3);                     // R101
    c.addTriode (ch.cPlateV5b, g5, k5, triode12AX7());
    c.addCapacitor (g5, ch.cPlateV5b, cgp);
    c.addResistor (v2, ch.cPlateV5b, 220.0e3);            // R96
    c.addResistor (k5, gnd, 2.2e3);                       // R102
    c.setInitialGuess (ch.cPlateV5b, 230.0);
    c.setInitialGuess (k5, 1.3);

    const auto la = c.addNode(), lb = c.addNode();
    c.addResistor (ch.cPlateV5b, la, 1.0e6);              // R87
    c.addCapacitor (la, lb, 22.0e-9);                     // C56
    c.addResistor (lb, gnd, 40.0e3);                      // V5A's grid as seen through its feedback
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412, &tubeamp::cabs::alnico212 };
}

EVH5150StyleAmplifierProcessor::EVH5150StyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto channel = std::make_unique<juce::AudioParameterFloat> (
        "evh_channel", "Channel", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v < 0.5f ? juce::String ("Rhythm") : juce::String ("Lead");
        }));
    auto gain = make ("evh_gain", "Gain", 0.5f);
    auto treble = make ("evh_treble", "High", 0.5f);
    auto mid = make ("evh_mid", "Mid", 0.5f);
    auto bass = make ("evh_bass", "Low", 0.5f);
    auto presence = make ("evh_presence", "Presence", 0.3f);
    auto resonance = make ("evh_resonance", "Resonance", 0.3f);
    auto post = make ("evh_post", "Volume", 0.5f);
    auto output = make ("evh_output", "Output", 0.5f);
    auto power = make ("evh_power", "Power Drive", 1.0f); // 1 = the real amp (no such control)
    auto bias = make ("evh_bias", "Bias", 0.5f);
    auto feel = make ("evh_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "evh_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    channelParam = channel.get();
    gainParam = gain.get();
    trebleParam = treble.get();
    midParam = mid.get();
    bassParam = bass.get();
    presenceParam = presence.get();
    resonanceParam = resonance.get();
    postParam = post.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "evh5150", "5150", "|", std::move (channel));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (resonance));
    group->addChild (std::move (post));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("evh_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("evh_cab", "4x12 EVH", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);

    // 5150 III: Gain / Low / Mid / High / Volume are per channel; Presence and Resonance are shared.
    channelMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { gainParam, trebleParam, midParam, bassParam, postParam });
}

std::unique_ptr<juce::XmlElement> EVH5150StyleAmplifierProcessor::getState() const
{
    auto xml = EffectProcessor::getState();
    channelMemory->writeState (*xml);
    return xml;
}

void EVH5150StyleAmplifierProcessor::setState (const juce::XmlElement& state)
{
    {
        const ChannelKnobMemory::ScopedSuspend suspend (*channelMemory);
        EffectProcessor::setState (state);
    }
    channelMemory->readState (state);
}

void EVH5150StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // Solid-state rectification → resistor-filtered RC chain.
    // A (220µF, 100K bleeder) → R210=400Ω → B (220µF, screens) → 4.7K → C (30µF, PI/V3) →
    // 10K → D (30µF, V2) → 22K → E (30µF, V1)
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~465V)
        ch.sB = c.addNode();                           // screens rail (~441V)
        ch.sC = c.addNode();                           // PI/V3 supply (~435V)
        ch.sD = c.addNode();                           // V2 rail (~420V)
        ch.sE = c.addNode();                           // V1 rail (~410V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 220.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);              // 100K bleeder across main bank
        c.addResistor (ch.sA, ch.sB, 400.0);              // R210: 400Ω between plates and screens
        c.addCapacitor (ch.sB, gnd, 220.0e-6);
        c.addResistor (ch.sB, ch.sC, 4.7e3);              // decoupling to PI/V3 rail
        c.addCapacitor (ch.sC, gnd, 30.0e-6);
        c.addResistor (ch.sC, ch.sD, 10.0e3);             // decoupling to V2 rail
        c.addCapacitor (ch.sD, gnd, 30.0e-6);
        c.addResistor (ch.sD, ch.sE, 22.0e3);             // decoupling to V1 rail
        c.addCapacitor (ch.sE, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);        // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.012);       // power tubes screen current
        ch.iC = c.addCurrentSource (ch.sC, -0.003);       // PI + V3 current draw
        ch.iD = c.addCurrentSource (ch.sD, -0.003);       // V2B/V5B/V5A preamp current
        ch.iE = c.addCurrentSource (ch.sE, -0.003);       // V1A/V1B/V2A preamp current
        for (auto n : { ch.sA, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 435.0);
        c.setInitialGuess (ch.sD, 420.0);
        c.setInitialGuess (ch.sE, 410.0);
    }

    buildPreA (ch);
    buildPreB (ch);
    buildPreC (ch);

    // ================================================================ V5A
    // A gain stage with its plate fed back to its grid through C58 and R88 1M. Its load is R89 470K into the tone
    // stack's input node (R24 33K, C59 .01 + R94 47K and the stack itself hold that node at a few tens of kilohms),
    // carried here as R89 + R24 + C59/R94; the stack block is then driven from C58's output.
    {
        auto& c = ch.driver;
        const auto in = c.addNode(), v2 = c.addNode();
        ch.dSrcIn = c.addSource (in, 230.0);              // V5B's plate (block C)
        ch.dSrcV2 = c.addSource (v2, 380.0);
        const auto na = c.addNode(), gw = c.addNode(), k6 = c.addNode(), st = c.addNode(), m = c.addNode();
        ch.dPlateV5a = c.addNode();
        ch.dOut = c.addNode();
        c.addResistor (in, na, 1.0e6);                    // R87
        c.addCapacitor (na, gw, 22.0e-9);                 // C56
        c.addResistor (gw, gnd, 1.0e6);                   // R91
        c.addTriode (ch.dPlateV5a, gw, k6, triode12AX7());
        c.addCapacitor (gw, ch.dPlateV5a, cgp);
        c.addResistor (v2, ch.dPlateV5a, 100.0e3);        // R86
        c.addResistor (k6, gnd, 2.2e3);                   // R93
        c.addCapacitor (ch.dPlateV5a, ch.dOut, 22.0e-9);  // C58
        c.addResistor (ch.dOut, gw, 1.0e6);               // R88
        c.addResistor (ch.dOut, st, 470.0e3);             // R89
        c.addResistor (st, gnd, 33.0e3);                  // R24
        c.addCapacitor (st, m, 0.01e-6);                  // C59
        c.addResistor (m, gnd, 47.0e3);                   // R94
        c.setInitialGuess (ch.dPlateV5a, 220.0);
        c.setInitialGuess (k6, 1.6);
    }

    // ================================================================ tone stack, posts, effects loop (linear)
    {
        auto& c = ch.stack;
        const auto q = c.addNode();
        ch.sSrcIn = c.addSource (q, 0.0);                 // C58's output (V5A block)
        const auto st = c.addNode(), m = c.addNode();
        ch.wStackIn = st;
        c.addResistor (q, st, 470.0e3);                   // R89
        c.addResistor (st, gnd, 33.0e3);                  // R24
        c.addCapacitor (st, m, 0.01e-6);                  // C59
        c.addResistor (m, gnd, 47.0e3);                   // R94

        // TMB: C14 470p || C60 100p to HIGH (250K lin); R28 47K slope; C18 .022 bass cap to HIGH's bottom / LOW (1M audio,
        // wired as a rheostat); MID 50K lin with C21 .022 on its wiper; R85 47K from LOW's bottom to ground. The schematic
        // draws a line across C18 -- read as a drafting slip, a shorted bass cap would leave LOW without its capacitor.
        const auto t1 = c.addNode(), tw = c.addNode(), tb = c.addNode(), n = c.addNode(), pm = c.addNode(), mw = c.addNode();
        c.addCapacitor (st, t1, 570.0e-12);               // C14 + C60
        ch.rTrebleTop = c.addResistor (t1, tw, 125.0e3);
        ch.rTrebleBottom = c.addResistor (tw, tb, 125.0e3);
        c.addResistor (st, n, 47.0e3);                    // R28
        c.addCapacitor (n, tb, 0.022e-6);                 // C18
        ch.rBass = c.addResistor (tb, pm, 500.0e3);       // VR3
        ch.rMidTop = c.addResistor (pm, mw, 25.0e3);      // VR4
        ch.rMidBottom = c.addResistor (mw, gnd, 25.0e3);
        c.addCapacitor (n, mw, 0.022e-6);                 // C21
        c.addResistor (pm, gnd, 47.0e3);                  // R85 (47K)

        // ULTRA POST (VR7) and CLEAN POST (VR6), both 1M audio from HIGH's wiper; K2B picks one.
        const auto up = c.addNode(), cp = c.addNode();
        ch.wPost = c.addNode();
        ch.rUltraPostTop = c.addResistor (tw, up, 500.0e3);
        ch.rUltraPostBot = c.addResistor (up, gnd, 500.0e3);
        ch.rCleanPostTop = c.addResistor (tw, cp, 500.0e3);
        ch.rCleanPostBot = c.addResistor (cp, gnd, 500.0e3);
        ch.rSelPostUltra = c.addResistor (up, ch.wPost, 1.0);
        ch.rSelPostClean = c.addResistor (cp, ch.wPost, 1.0e9);
        c.addResistor (ch.wPost, gnd, 2.2e6);             // R81
        // Effects loop switched off: C53 .33 / C65 .33 (a ~3 Hz high-pass, left out) with R103 100K, R33 100K and
        // R85 220K down, then C11 .047 into V3A's grid leak R26 1M.
        c.addResistor (ch.wPost, gnd, 100.0e3);           // R103
        c.addResistor (ch.wPost, gnd, 100.0e3);           // R33
        c.addResistor (ch.wPost, gnd, 220.0e3);           // R85 (220K)
        ch.sOut = c.addNode();
        c.addCapacitor (ch.wPost, ch.sOut, 0.047e-6);     // C11
        c.addResistor (ch.sOut, gnd, 1.0e6);              // R26
    }

    // ================================================================ V3A (+ PI and power stage in the full reference)
    // V3A's grid is driven from the stack block: it draws no grid current until the Post is far up.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto g7 = c.addNode(), v2 = c.addNode(), k7 = c.addNode();
        ch.wSrcIn = c.addSource (g7, 0.0);
        ch.wSrcV2 = c.addSource (v2, 380.0);
        ch.wPlateV3a = c.addNode();
        c.addTriode (ch.wPlateV3a, g7, k7, triode12AX7());
        c.addCapacitor (g7, ch.wPlateV3a, cgp);
        c.addResistor (v2, ch.wPlateV3a, 68.0e3);         // R5
        c.addResistor (k7, gnd, 3.3e3);                   // R14
        c.addCapacitor (k7, gnd, 4.7e-9);                 // C61
        c.setInitialGuess (ch.wPlateV3a, 250.0);
        c.setInitialGuess (k7, 1.8);
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 435.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // C49 .022 -> Power Drive (a 1M pot standing in for R50 1M, so at full it IS R50) -> R48 100K -> V4B, the
        // driven half. Tail: R52 470 -> R51 10K -> R57 4.7K; V4A's grid sits on R53 1M with C31 .047 to the R51/R57 node.
        const auto n0 = c.addNode(), n1 = c.addNode(), g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(),
                   k = c.addNode(), t = c.addNode(), u = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = t;
        ch.wFeedback = u;
        c.addCapacitor (ch.wPlateV3a, n0, 0.022e-6);      // C49
        ch.rDriveTop = c.addResistor (n0, n1, 1.0);
        ch.rDriveBot = c.addResistor (n1, t, 1.0e6);      // R50
        c.addResistor (n1, g1, 100.0e3);                  // R48
        c.addResistor (g2, t, 1.0e6);                     // R53
        c.addCapacitor (g2, u, 0.047e-6);                 // C31
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);                  // R46
        c.addResistor (vpi, pb, 100.0e3);                 // R58
        c.addResistor (k, t, 470.0);                      // R52
        c.addResistor (t, u, 10.0e3);                     // R51
        c.addResistor (u, gnd, 4.7e3);                    // R57
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 245.0);
        c.setInitialGuess (k, 40.0);
        c.setInitialGuess (t, 40.0);
        c.setInitialGuess (u, 13.0);
        c.setInitialGuess (g1, 40.0);
        c.setInitialGuess (g2, 40.0);

        // Power amplifier: 4 × 6L6GC as two push-pull pairs. .047µF couplings, 2.2K grid stoppers (per pair: 2.2K each half),
        // 220K grid leaks to the fixed bias rail.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto sw = c.addNode();
        ch.wOut = c.addNode();
        c.addCapacitor (pa, g3, 0.047e-6);                  // C29 (V4B plate)
        c.addCapacitor (pb, g4, 0.047e-6);                  // C28 (V4A plate)
        c.addResistor (g3, nb, 220.0e3);                    // R55
        c.addResistor (g4, nb, 220.0e3);                    // R54
        c.addResistor (g3, g3s, 2.2e3);                     // R206 grid stopper
        c.addResistor (g4, g4s, 2.2e3);                     // R202 grid stopper
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

        // NFB: R56 39K -> [C12 .0068 || RESONANCE 1M] -> C30 22uF into the R51/R57 node; from the same point
        // C8 .1 || C62 .033 -> PRESENCE 10K to ground.
        const auto nf = c.addNode(), r = c.addNode(), pq = c.addNode();
        ch.rFeedback = c.addResistor (ch.wOut, nf, feedbackResistor);
        c.addCapacitor (nf, r, 6.8e-9);                   // C12
        ch.rResonance = c.addResistor (nf, r, 500.0e3);   // VR8
        c.addCapacitor (r, u, 22.0e-6);                   // C30
        c.addCapacitor (r, pq, 0.133e-6);                 // C8 + C62
        ch.rPresence = c.addResistor (pq, gnd, 5.0e3);    // VR9

        c.setInitialGuess (pp1, railPlatesNominal);
        c.setInitialGuess (pp2, railPlatesNominal);
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
        for (auto n : { g3, g4, g3s, g4s, nb })
            c.setInitialGuess (n, -50.0);
        c.setInitialGuess (nbt, -1.0);
    }
}

void EVH5150StyleAmplifierProcessor::applyChannel (Channel& ch, int channelChoice) const
{
    const bool lead = channelChoice != 0;
    ch.preA.setResistance (ch.rSelUltra, lead ? 1.0 : 1.0e9);
    ch.preA.setResistance (ch.rSelClean, lead ? 1.0e9 : 1.0);
    // K1B: R20 220K on V2A's grid with the Rhythm channel's Crunch off.
    ch.preB.setResistance (ch.rCrunchShunt, lead ? 1.0e9 : 220.0e3);
    ch.stack.setResistance (ch.rSelPostUltra, lead ? 1.0 : 1.0e9);
    ch.stack.setResistance (ch.rSelPostClean, lead ? 1.0e9 : 1.0);
}

void EVH5150StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
    const double gainTop = juce::jmax (1.0, 1.0e6 - gainBottom);
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 50.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 50.0e3 - midBottom);
    const double postBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.post));
    const double postTop = juce::jmax (1.0, 1.0e6 - postBottom);
    // Presence: turning it up takes VR9 toward 0, shunting the feedback's top end to ground. Resonance: turning it up
    // raises VR8, leaving the low end of the feedback only C12 .0068.
    const double presence = juce::jmax (1.0, 10.0e3 * (1.0 - pots::audio (k.presence)));
    const double resonance = juce::jmax (1.0, 1.0e6 * pots::audio (k.resonance));
    driveFraction = juce::jmax (0.002, pots::audio (k.powerDrive));
    const double driveBottom = juce::jmax (1.0, 1.0e6 * driveFraction);
    const double driveTop = juce::jmax (1.0, 1.0e6 - driveBottom);
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    const double feedbackR = feedbackOverride > 0.0 ? feedbackOverride : feedbackResistor / (1.0 + 1.5 * (1.0 - k.tubeFeel));

    for (auto& ch : channels)
    {
        ch.preA.setResistance (ch.rUltraTop, gainTop);
        ch.preA.setResistance (ch.rUltraBot, gainBottom);
        ch.preA.setResistance (ch.rCleanTop, gainTop);
        ch.preA.setResistance (ch.rCleanBot, gainBottom);
        ch.stack.setResistance (ch.rTrebleTop, trebleTop);
        ch.stack.setResistance (ch.rTrebleBottom, trebleBottom);
        ch.stack.setResistance (ch.rBass, bassR);
        ch.stack.setResistance (ch.rMidTop, midTop);
        ch.stack.setResistance (ch.rMidBottom, midBottom);
        ch.stack.setResistance (ch.rUltraPostTop, postTop);
        ch.stack.setResistance (ch.rUltraPostBot, postBottom);
        ch.stack.setResistance (ch.rCleanPostTop, postTop);
        ch.stack.setResistance (ch.rCleanPostBot, postBottom);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rDriveTop, driveTop);
            ch.power.setResistance (ch.rDriveBot, driveBottom);
            ch.power.setResistance (ch.rPresence, presence);
            ch.power.setResistance (ch.rResonance, resonance);
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

void EVH5150StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void EVH5150StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void EVH5150StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.preA.restoreDynamicState (ch.restA);
    ch.preB.restoreDynamicState (ch.restB);
    ch.preC.restoreDynamicState (ch.restC);
    ch.driver.restoreDynamicState (ch.restD);
    ch.stack.restoreDynamicState (ch.restS);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
}

void EVH5150StyleAmplifierProcessor::updateSupply (Channel& ch) const
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
    const double v1 = rail (ch.sE, 480.0), v2 = rail (ch.sD, 480.0);
    ch.preA.setSource (ch.aSrcV1, v1);
    ch.preB.setSource (ch.bSrcV1, v1);
    ch.preB.setSource (ch.bSrcV2, v2);
    ch.preC.setSource (ch.cSrcV2, v2);
    ch.driver.setSource (ch.dSrcV2, v2);
    ch.power.setSource (ch.wSrcV2, v2);
    ch.vScreen = rail (ch.sB, 560.0);
}

bool EVH5150StyleAmplifierProcessor::solvePreamp (Channel& ch, double x) const
{
    ch.preA.setSource (ch.aSrcIn, x);
    bool ok = ch.preA.solveSample();
    ch.preB.setSource (ch.bSrcIn, ch.preA.voltage (ch.aPlateV1b));
    ok = ch.preB.solveSample() && ok;
    ch.preC.setSource (ch.cSrcIn, ch.preB.voltage (ch.bPlateV2b));
    ok = ch.preC.solveSample() && ok;
    ch.driver.setSource (ch.dSrcIn, ch.preC.voltage (ch.cPlateV5b));
    ok = ch.driver.solveSample() && ok;
    ch.stack.setSource (ch.sSrcIn, ch.driver.voltage (ch.dOut));
    ok = ch.stack.solveSample() && ok;
    ch.power.setSource (ch.wSrcIn, ch.stack.voltage (ch.sOut));
    return ok;
}

void EVH5150StyleAmplifierProcessor::preparePreamp (Channel& ch, double rate, bool& ok) const
{
    const double v1 = ch.supply.voltage (ch.sE), v2 = ch.supply.voltage (ch.sD);
    ch.preA.setSource (ch.aSrcV1, v1);
    ok = ch.preA.prepare (rate) && ok;
    ch.preB.setSource (ch.bSrcV1, v1);
    ch.preB.setSource (ch.bSrcV2, v2);
    ch.preB.setSource (ch.bSrcIn, ch.preA.voltage (ch.aPlateV1b));
    ok = ch.preB.prepare (rate) && ok;
    ch.preC.setSource (ch.cSrcV2, v2);
    ch.preC.setSource (ch.cSrcIn, ch.preB.voltage (ch.bPlateV2b));
    ok = ch.preC.prepare (rate) && ok;
    ch.driver.setSource (ch.dSrcV2, v2);
    ch.driver.setSource (ch.dSrcIn, ch.preC.voltage (ch.cPlateV5b));
    ok = ch.driver.prepare (rate) && ok;
    ch.stack.setSource (ch.sSrcIn, ch.driver.voltage (ch.dOut));
    ok = ch.stack.prepare (rate) && ok;
    ch.power.setSource (ch.wSrcV2, v2);
    ch.power.setSource (ch.wSrcIn, ch.stack.voltage (ch.sOut));
}

double EVH5150StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (toneVoltage);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
    // reducedOrder folds the page-2 controls that only exist in the full power netlist into the
    // behavioural stage: Tube Feel scales the available sag depth, Bias shifts the saturator's
    // operating point the way the full model's bias trim does.
    const double feel = juce::jlimit (0.0, 1.0, (double) lastKnobs.tubeFeel);
    const double bias = juce::jlimit (0.0, 1.0, (double) lastKnobs.bias);
    ch.bmRail = bmSagRail[0] - feel * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

    const double k = ch.bmRail * bmYmax / bmGain0;
    const double shift = 0.6 * (bias - 0.5) * k;
    const auto sat = [k, rail = ch.bmRail] (double xx)
    {
        const double ux = std::abs (xx) / juce::jmax (1.0e-9, k);
        const double yx = bmYmax * ux / std::pow (1.0 + std::pow (ux, bmKneeN), 1.0 / bmKneeN);
        return std::copysign (yx * rail, xx);
    };
    const double raw = sat (toneVoltage + shift) - sat (shift);

    // The frequency response the removed stages used to provide: two fixed biquads fitted to this netlist's
    // own measured transfer (resonant high-pass LF bump + zero/pole top section) plus a one-pole low cut.
    const double lf = bmAB0 * raw + bmAB1 * ch.bmAX1 + bmAB2 * ch.bmAX2
                    - bmAA1 * ch.bmAY1 - bmAA2 * ch.bmAY2;
    ch.bmAX2 = ch.bmAX1; ch.bmAX1 = raw;
    ch.bmAY2 = ch.bmAY1; ch.bmAY1 = lf;
    const double top = bmBB0 * lf + bmBB1 * ch.bmBX1 + bmBB2 * ch.bmBX2
                     - bmBA1 * ch.bmBY1 - bmBA2 * ch.bmBY2;
    ch.bmBX2 = ch.bmBX1; ch.bmBX1 = lf;
    ch.bmBY2 = ch.bmBY1; ch.bmBY1 = top;
    // Presence/Resonance were dead in reducedOrder (their NFB legs live in the full power block
    // only): presence becomes a first-order HF shelf on the fitted output -- flat at the 0.3
    // default; resonance/depth leans the output toward the pre-top-section LF bump.
    const double presAlpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 4500.0 / juce::jmax (1.0, sampleRate));
    ch.bmPresState += presAlpha * (top - ch.bmPresState);
    const double presShelf = ch.bmPresState + (0.64 + 1.2 * juce::jlimit (0.0, 1.0, (double) lastKnobs.presence)) * (top - ch.bmPresState);
    const double effTop = presShelf + (juce::jlimit (0.0, 1.0, (double) lastKnobs.resonance) - 0.3) * 0.8 * (lf - raw);
    const double notch = bmCB0 * effTop + bmCB1 * ch.bmCX1 + bmCB2 * ch.bmCX2
                       - bmCA1 * ch.bmCY1 - bmCA2 * ch.bmCY2;
    ch.bmCX2 = ch.bmCX1; ch.bmCX1 = effTop;
    ch.bmCY2 = ch.bmCY1; ch.bmCY1 = notch;
    const double cut = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmCutHz / juce::jmax (1.0, sampleRate));
    ch.bmCutState += cut * (notch - ch.bmCutState);
    // physical ceiling: the stages this replaces clip at the rail -- the fitted filters' resonance can overshoot
    // the knee's bound by ~2x on saturated LF, which would otherwise trip the pluck peak and sanity bounds.
    ch.bmOutput = juce::jlimit (-bmOutMax, bmOutMax, ch.bmTail.process ((notch - ch.bmCutState) * bmLevelTrim));
    return ch.bmOutput;
}

void EVH5150StyleAmplifierProcessor::designPowerFilters()
{
    // Bilinear transform (s = c(1-z^-1)/(1+z^-1), c = 2*fs) of an analog biquad n2 s^2 + n1 s + n0 over d2 s^2 + d1 s + d0.
    const auto bilinear = [] (double n2, double n1, double n0, double d2, double d1, double d0, double fs,
                              double& b0, double& b1, double& b2, double& a1, double& a2)
    {
        const double c = 2.0 * fs;
        const double A0 = d2 * c * c + d1 * c + d0;
        a1 = 2.0 * (d0 - d2 * c * c) / A0;
        a2 = (d2 * c * c - d1 * c + d0) / A0;
        b0 = (n2 * c * c + n1 * c + n0) / A0;
        b1 = 2.0 * (n0 - n2 * c * c) / A0;
        b2 = (n2 * c * c - n1 * c + n0) / A0;
    };
    const double wb = 2.0 * juce::MathConstants<double>::pi * bmBumpHz;
    // resonant high-pass: peaks +20log10(Qp) dB at bmBumpHz, -12 dB/oct below, unity above
    bilinear (1.0 / (wb * wb), 0.0, 0.0,
              1.0 / (wb * wb), 1.0 / (bmBumpQp * wb), 1.0,
              sampleRate, bmAB0, bmAB1, bmAB2, bmAA1, bmAA2);
    const double wz = 2.0 * juce::MathConstants<double>::pi * bmTopZHz, wp = 2.0 * juce::MathConstants<double>::pi * bmTopPHz;
    bilinear (0.0, 1.0 / wz, 1.0,
              1.0 / (wp * wp), 1.0 / (bmTopQp * wp), 1.0,
              sampleRate, bmBB0, bmBB1, bmBB2, bmBA1, bmBA2);
    const double wn = 2.0 * juce::MathConstants<double>::pi * bmNotchHz;
    bilinear (1.0 / (wn * wn), 0.0, 1.0,
              1.0 / (wn * wn), 1.0 / (bmNotchQ * wn), 1.0,
              sampleRate, bmCB0, bmCB1, bmCB2, bmCA1, bmCA2);
}

double EVH5150StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.preA.voltage (ch.aPlateV1a);
        case Probe::v1bPlate: return ch.preA.voltage (ch.aPlateV1b);
        case Probe::v2aPlate: return ch.preB.voltage (ch.bPlateV2a);
        case Probe::v2bPlate: return ch.preB.voltage (ch.bPlateV2b);
        case Probe::v5bPlate: return ch.preC.voltage (ch.cPlateV5b);
        case Probe::v5aPlate: return ch.driver.voltage (ch.dPlateV5a);
        case Probe::toneStackOut: return ch.stack.voltage (ch.wPost);
        case Probe::recoveryPlate: return ch.power.voltage (ch.wPlateV3a);
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

double EVH5150StyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    const auto& ch = channels[0];
    switch (block)
    {
        case 0: return ch.preA.averageIterations();
        case 1: return ch.preB.averageIterations();
        case 2: return ch.preC.averageIterations();
        case 3: return ch.driver.averageIterations();
        default: return ch.power.averageIterations();
    }
}

int EVH5150StyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void EVH5150StyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double EVH5150StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double EVH5150StyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void EVH5150StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, voicings::highGainEVH, 1.0 / outputScale);
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
    setup (smoothedResonance, resonanceParam, 0.02);
    setup (smoothedPost, postParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ gainParam->get(), trebleParam->get(), midParam->get(), bassParam->get(),
                  presenceParam->get(), resonanceParam->get(), postParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    const int chSel = juce::roundToInt (channelParam->get());
    for (auto& ch : channels)
    {
        applyChannel (ch, chSel);
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iPiRun = 0.0025, iV2Run = 0.004, iV1Run = 0.002, ipRun = 0.16, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);
            preparePreamp (ch, newSampleRate, passOk);
            ch.vScreen = ch.supply.voltage (ch.sB);
            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
            // reducedOrder has no PI in the netlist: the full reference's two plates draw ~2.5 mA from C.
            double iPi = 0.0025;
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
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 100.0e3;
            }
            const double vV2 = ch.supply.voltage (ch.sD), vV1 = ch.supply.voltage (ch.sE);
            const double iV2 = (vV2 - ch.preB.voltage (ch.bPlateV2b)) / 220.0e3
                             + (vV2 - ch.preC.voltage (ch.cPlateV5b)) / 220.0e3
                             + (vV2 - ch.driver.voltage (ch.dPlateV5a)) / 100.0e3
                             + (vV2 - ch.power.voltage (ch.wPlateV3a)) / 68.0e3;
            const double iV1 = (vV1 - ch.preA.voltage (ch.aPlateV1a)) / 220.0e3
                             + (vV1 - ch.preA.voltage (ch.aPlateV1b)) / 100.0e3
                             + (vV1 - ch.preB.voltage (ch.bPlateV2a)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -iV2Run);
            ch.supply.setCurrentSource (ch.iE, -iV1Run);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iV2Run + iV1Run + (ch.vScreen / bleeder);
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * feel * idleSupplyCurrent);
            ch.screenDropA = screenResistor * isA;
            ch.screenDropB = screenResistor * isB;
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        ch.vScreen = ch.supply.voltage (ch.sB);
        // The blocks' last DC solves saw the previous pass's rails; solve them again at the rails they will run on
        // (a rail step through this much gain is a startup thump).
        preparePreamp (ch, newSampleRate, dcOk);
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
        }
        dcOk = ch.power.prepare (newSampleRate) && dcOk;
        ch.preA.saveDynamicState (ch.restA);
        ch.preB.saveDynamicState (ch.restB);
        ch.preC.saveDynamicState (ch.restC);
        ch.driver.saveDynamicState (ch.restD);
        ch.stack.saveDynamicState (ch.restS);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmAX1 = ch.bmAX2 = ch.bmAY1 = ch.bmAY2 = 0.0;
        ch.bmBX1 = ch.bmBX2 = ch.bmBY1 = ch.bmBY2 = 0.0;
        ch.bmCX1 = ch.bmCX2 = ch.bmCY1 = ch.bmCY2 = 0.0;
        ch.bmCutState = 0.0;
        ch.bmPresState = 0.0;
        ch.piCoupling.prepare (newSampleRate, 0.022e-6, 1.0e6, ch.power.voltage (ch.wPlateV3a));
    }
    designPowerFilters();
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void EVH5150StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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
    smoothedResonance.setTargetValue (resonanceParam->get());
    smoothedPost.setTargetValue (postParam->get());
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
        const float rs = smoothedResonance.getNextValue();
        const float ps = smoothedPost.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, rs, ps, pw, bi, fe, speakerChoice });
            const int chSel = juce::roundToInt (channelParam->get());
            for (auto& ch : channels)
                applyChannel (ch, chSel);
        }

        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            const bool okPre = solvePreamp (ch, x);
            bool ok = okPre;

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

            // reducedOrder: C49 into the PI, scaled by Power Drive (the full path's pot in front of R48).
            const double speakerVolts = reducedOrder
                ? behavioralPowerStage (ch, driveFraction * ch.piCoupling.process (ch.power.voltage (ch.wPlateV3a)))
                : ch.power.voltage (ch.wOut);
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
                    ch.preA.saveDynamicState (ch.restA);
                    ch.preB.saveDynamicState (ch.restB);
                    ch.preC.saveDynamicState (ch.restC);
                    ch.driver.saveDynamicState (ch.restD);
                    ch.stack.saveDynamicState (ch.restS);
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

void EVH5150StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
