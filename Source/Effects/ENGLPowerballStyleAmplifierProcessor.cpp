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

    // ---- supply (645-1, sheet 1): Stand By off 460 V / on 440 V, BY509 bridge. +430 V -> R136 470R -> screens ->
    // R137 2.2K -> PI / V7 -> R138 2.2K -> preamp (CE3 10u).
    constexpr double railPlatesNominal = 460.0;
    constexpr double rectifierResistance = 50.0;
    constexpr double bleeder = 220.0e3;

    // Sag table scaled from the Twin Reverb data (497 V -> 460 V)
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

    KorenTriode::Parameters triode12AX7() { return {}; }

    KorenPentode::Parameters pentode6L6Pair()
    {
        KorenPentode::Parameters p;
        p.kg1 *= 0.5;
        p.kg2 *= 0.5;
        p.grid.Gg *= 2.0;
        p.arcResistance *= 0.5;
        return p;
    }

    constexpr double cgp = 1.7e-12;

    // OT: 4x6L6GC, ~100 W
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;

    constexpr double biasSupplyVolts = -55.0;
    constexpr double screenResistor = 470.0;      // R72..R75

    constexpr double speakerEddyLoss = 150.0;
    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 1;
    constexpr double powerTheta = 0.9;

    // Relay contacts and the pushbuttons: closed 1 ohm, open 100M (an open contact's leakage; also every node keeps a
    // DC path); a change fades in the log domain over relayFadeSeconds. The two gain LDRs (LT30): lit 1K, dark 50M,
    // ldrFadeSeconds.
    constexpr double relayOn = 1.0, relayOff = 100.0e6, relayFadeSeconds = 0.005;
    constexpr double ldrOn = 1.0e3, ldrOff = 50.0e6, ldrFadeSeconds = 0.02;
    constexpr double v6bOutputResistance = 100.0e3 * 62.5e3 / (100.0e3 + 62.5e3); // R24 || ra

    /** C-taper (reverse audio) pot fraction. */
    double reverseAudio (double x) noexcept { return 1.0 - pots::audio (1.0 - x); }
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412, &tubeamp::cabs::alnico212 };
}

ENGLPowerballStyleAmplifierProcessor::ENGLPowerballStyleAmplifierProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto makeSwitch = [] (const char* id, const char* name, std::vector<juce::String> names, float def)
    {
        const float top = (float) names.size() - 1.0f;
        return std::make_unique<juce::AudioParameterFloat> (
            id, name, juce::NormalisableRange<float> (0.0f, top, 1.0f), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([names] (float v, int)
            { return names[(size_t) juce::jlimit (0, (int) names.size() - 1, juce::roundToInt (v))]; }));
    };
    auto channel = makeSwitch ("engl_pb_channel", "Channel", { "Clean", "Crunch", "Lo Lead", "Hi Lead" }, 3.0f);
    auto gain = make ("engl_pb_gain", "Gain", 0.5f);
    auto bass = make ("engl_pb_bass", "Bass", 0.5f);
    auto mid = make ("engl_pb_mid", "Middle", 0.5f);
    auto midFocused = make ("engl_pb_mid_focused", "Middle Focused", 0.5f);
    auto treble = make ("engl_pb_treble", "Treble", 0.5f);
    auto volume = make ("engl_pb_master", "Volume", 0.5f); // the selected mode's own Volume (id kept for presets)
    auto style = makeSwitch ("engl_pb_style", "Style", { "Open", "Focused" }, 0.0f);
    auto bright = makeSwitch ("engl_pb_bright", "Bright", { "Off", "On" }, 0.0f);
    auto bottom = makeSwitch ("engl_pb_bottom", "Bottom", { "Off", "On" }, 0.0f);
    auto presence = make ("engl_pb_presence", "Presence", 0.3f);
    auto depth = make ("engl_pb_depth", "Depth Punch", 0.3f);
    auto masterA = make ("engl_pb_master_a", "Master A", 0.5f);
    auto masterB = make ("engl_pb_master_b", "Master B", 0.5f);
    auto masterSel = makeSwitch ("engl_pb_master_sel", "Master A/B", { "A", "B" }, 0.0f);
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

    channelParam = channel.get();
    gainParam = gain.get();
    bassParam = bass.get();
    midParam = mid.get();
    midFocusedParam = midFocused.get();
    trebleParam = treble.get();
    volumeParam = volume.get();
    styleParam = style.get();
    brightParam = bright.get();
    bottomParam = bottom.get();
    presenceParam = presence.get();
    depthParam = depth.get();
    masterAParam = masterA.get();
    masterBParam = masterB.get();
    masterSelParam = masterSel.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "engl_powerball", "Powerball", "|", std::move (channel), std::move (gain));
    group->addChild (std::move (bass));
    group->addChild (std::move (mid));
    group->addChild (std::move (midFocused));
    group->addChild (std::move (treble));
    group->addChild (std::move (volume));
    group->addChild (std::move (style));
    group->addChild (std::move (bright));
    group->addChild (std::move (bottom));
    group->addChild (std::move (presence));
    group->addChild (std::move (depth));
    group->addChild (std::move (masterA));
    group->addChild (std::move (masterB));
    group->addChild (std::move (masterSel));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("engl_pb_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("engl_pb_cab", "4x12 ENGL", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);

    mainMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { gainParam, bassParam, midParam, midFocusedParam }, std::vector<int> { 0, 0, 1, 1 });
    trebleMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { trebleParam }, std::vector<int> { 0, 1, 2, 2 });
    volumeMemory = std::make_unique<ChannelKnobMemory> (*channelParam, std::vector<juce::AudioParameterFloat*> { volumeParam });
    bottomMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { bottomParam }, std::vector<int> { 0, 0, 1, 2 });
}

std::unique_ptr<juce::XmlElement> ENGLPowerballStyleAmplifierProcessor::getState() const
{
    auto xml = EffectProcessor::getState();
    for (auto* m : { mainMemory.get(), trebleMemory.get(), volumeMemory.get(), bottomMemory.get() })
        m->writeState (*xml);
    return xml;
}

void ENGLPowerballStyleAmplifierProcessor::setState (const juce::XmlElement& state)
{
    // Presets from before the drawing rebuild (no engl_pb_style) kept Gain per channel (slots 0-3) and Treble / Mid / Bass
    // per channel pair (slots 0-1). Gain is now per main channel, Treble Clean / Crunch / Lead.
    juce::XmlElement migrated (state);
    if (! state.hasAttribute ("engl_pb_style"))
    {
        if (state.hasAttribute ("engl_pb_gain_ch2"))
            migrated.setAttribute ("engl_pb_gain_ch1", state.getDoubleAttribute ("engl_pb_gain_ch2"));
        if (state.hasAttribute ("engl_pb_treble_ch1"))
            migrated.setAttribute ("engl_pb_treble_ch2", state.getDoubleAttribute ("engl_pb_treble_ch1"));
        if (state.hasAttribute ("engl_pb_treble_ch0"))
            migrated.setAttribute ("engl_pb_treble_ch1", state.getDoubleAttribute ("engl_pb_treble_ch0"));
    }
    {
        const ChannelKnobMemory::ScopedSuspend a (*mainMemory), b (*trebleMemory), c (*volumeMemory), d (*bottomMemory);
        EffectProcessor::setState (migrated);
    }
    for (auto* m : { mainMemory.get(), trebleMemory.get(), volumeMemory.get(), bottomMemory.get() })
        m->readState (migrated);
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
        ch.iC = c.addCurrentSource (ch.sC, -0.004);
        ch.iD = c.addCurrentSource (ch.sD, -0.006);
        for (auto n : { ch.sA, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 430.0);
        c.setInitialGuess (ch.sD, 400.0);
    }

    // ================================================================ preamp, both tone stacks, V7A / V7B, Master A / B
    {
        auto& c = ch.pre;
        const auto vcc = c.addNode(), in = c.addNode();
        ch.pSrcVcc = c.addSource (vcc, 400.0);
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto sw = [&] (Sw s, NodalCircuit::Node a, NodalCircuit::Node b)
        { ch.sw[(size_t) s] = c.addResistor (a, b, s == ld1 || s == ld2 ? ldrOff : relayOff); };
        const auto stage = [&] (NodalCircuit::Node grid, double rPlate, double rk, double ck, double plateGuess, double kGuess)
        {
            const auto plate = c.addNode(), k = c.addNode();
            c.addTriode (plate, grid, k, triode12AX7());
            c.addCapacitor (grid, plate, cgp);
            c.addResistor (vcc, plate, rPlate);
            c.addResistor (k, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (k, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (k, kGuess);
            return plate;
        };

        // V5A: the input straight to the grid (R1 1M), R3 100K, R2 1K + CE1 22u.
        c.addResistor (in, gnd, 1.0e6);
        ch.pPlateV5a = stage (in, 100.0e3, 1.0e3, 22.0e-6, 200.0, 1.2);

        // C1 47n -> N1 (R4 2.2M). CH I: C2 15n (shorted by Bottom) -> R5 100K -> P1 1MA, C3 100p (Bright) from R5's top to
        // the wiper, LD1 to V5B. CH II: R6 100K -> P10 1MA, LD2 to V5B. R7 1M grid leak.
        const auto n1 = c.addNode(), x5 = c.addNode(), p1 = c.addNode(), w1 = c.addNode(), c3n = c.addNode();
        const auto p10 = c.addNode(), w10 = c.addNode(), g5b = c.addNode();
        c.addCapacitor (ch.pPlateV5a, n1, 47.0e-9);
        c.addResistor (n1, gnd, 2.2e6);
        c.addCapacitor (n1, x5, 15.0e-9);
        sw (bottomCh1, n1, x5);
        c.addResistor (x5, p1, 100.0e3);
        ch.rGain1Top = c.addResistor (p1, w1, 0.5e6);
        ch.rGain1Bot = c.addResistor (w1, gnd, 0.5e6);
        c.addCapacitor (x5, c3n, 100.0e-12);
        sw (bright, c3n, w1);
        c.addResistor (n1, p10, 100.0e3);
        ch.rGain2Top = c.addResistor (p10, w10, 0.5e6);
        ch.rGain2Bot = c.addResistor (w10, gnd, 0.5e6);
        sw (ld1, w1, g5b);
        sw (ld2, w10, g5b);
        c.addResistor (g5b, gnd, 1.0e6);

        // V5B: R9 100K, R8 1.5K + CE2 22u. C4 47n -> N4 (R10 2.2M) and its four dividers.
        ch.pPlateV5b = stage (g5b, 100.0e3, 1.5e3, 22.0e-6, 230.0, 1.5);
        const auto n4 = c.addNode(), j1 = c.addNode(), j2 = c.addNode(), pp = c.addNode(), qq = c.addNode(), aa = c.addNode();
        c.addCapacitor (ch.pPlateV5b, n4, 47.0e-9);
        c.addResistor (n4, gnd, 2.2e6);
        c.addResistor (n4, j1, 220.0e3);              // R11
        c.addResistor (j1, gnd, 470.0e3);             // R12
        c.addResistor (n4, j2, 100.0e3);              // R13
        c.addResistor (j2, gnd, 470.0e3);             // R14
        c.addResistor (n4, pp, 1.0e6);                // R15
        c.addResistor (pp, gnd, 220.0e3);             // R17
        c.addResistor (n4, qq, 680.0e3);              // R18
        c.addResistor (qq, gnd, 330.0e3);             // R16
        sw (re3aLo, pp, aa);
        sw (re3aHi, qq, aa);

        // V6A (Ch II only): RE3B picks J1 / J2; R20 330K, R19 3.3K + CE4 1u; C5 47n.
        const auto g6a = c.addNode(), c5o = c.addNode(), r21 = c.addNode(), g6b = c.addNode();
        sw (re3bLo, j1, g6a);
        sw (re3bHi, j2, g6a);
        ch.pPlateV6a = stage (g6a, 330.0e3, 3.3e3, 1.0e-6, 180.0, 2.0);
        c.addCapacitor (ch.pPlateV6a, c5o, 47.0e-9);
        sw (re1bMute, c5o, gnd);
        sw (re1aCh1, aa, r21);
        sw (re1aCh2, c5o, r21);
        c.addResistor (r21, g6b, 220.0e3);            // R21
        c.addResistor (g6b, gnd, 1.0e6);              // R22

        // V6B: R24 100K, R23 1.5K + CE5 22u. Its plate drives the tone block below through its Thevenin output resistance.
        ch.pPlateV6b = stage (g6b, 100.0e3, 1.5e3, 22.0e-6, 230.0, 1.5);
    }

    // ================================================================ tone block (linear): C6, both tone stacks, the Volumes, R40 / R41
    // NodalCircuit holds 48 unknowns a block, so the chain splits where a tube's own impedances make the split exact or
    // close: V6B's plate enters as its unloaded voltage behind R24 || ra (~38K), V7A's grid leaves as an ideal voltage.
    {
        auto& c = ch.tone;
        const auto srcNode = c.addNode(), plate = c.addNode();
        ch.tSrcPlate = c.addSource (srcNode, 0.0);
        c.addResistor (srcNode, plate, v6bOutputResistance);
        const auto sw = [&] (Sw s, NodalCircuit::Node a, NodalCircuit::Node b) { ch.sw[(size_t) s] = c.addResistor (a, b, relayOff); };
        // C6 100n -> S6 (R25 1M) -> RE2B -> a tone stack.
        const auto s6 = c.addNode(), in1 = c.addNode(), in2 = c.addNode();
        c.addCapacitor (plate, s6, 100.0e-9);
        c.addResistor (s6, gnd, 1.0e6);
        sw (re2bCh1, s6, in1);
        sw (re2bCh2, s6, in2);

        // CH I stack: C8 470p (+ C7 470p, RE6A) to both treble pots (P4 Clean / P5 Crunch 250KB, RE5A picks whose bottom
        // returns to the bass leg, RE5B whose wiper is heard); R27 100K slope; C9 220n; Bass P2 1MA; Middle P3 20KB;
        // C10 100n (+ C11 47n, RE6B). Volumes: R28 220K -> P6 Clean 250KB, R29 330K -> P7 Crunch 250KA; RE4A.
        const auto t1 = c.addNode(), c7n = c.addNode(), s1 = c.addNode(), bt1 = c.addNode(), mt1 = c.addNode(), c11n = c.addNode();
        const auto w4 = c.addNode(), b4 = c.addNode(), w5 = c.addNode(), b5 = c.addNode(), v1in = c.addNode();
        const auto p6 = c.addNode(), w6 = c.addNode(), p7 = c.addNode(), w7 = c.addNode(), out1 = c.addNode();
        c.addCapacitor (in1, t1, 470.0e-12);
        c.addCapacitor (in1, c7n, 470.0e-12);
        sw (re6aFocused, c7n, t1);
        c.addResistor (in1, s1, 100.0e3);
        c.addCapacitor (s1, bt1, 220.0e-9);
        ch.rBass1 = c.addResistor (bt1, mt1, 500.0e3);
        ch.rMid1 = c.addResistor (mt1, gnd, 10.0e3);
        c.addCapacitor (s1, mt1, 100.0e-9);
        sw (re6bOpen, s1, c11n);
        c.addCapacitor (c11n, mt1, 47.0e-9);
        ch.rCleanTrebleTop = c.addResistor (t1, w4, 125.0e3);
        ch.rCleanTrebleBot = c.addResistor (w4, b4, 125.0e3);
        ch.rCrunchTrebleTop = c.addResistor (t1, w5, 125.0e3);
        ch.rCrunchTrebleBot = c.addResistor (w5, b5, 125.0e3);
        sw (re5aClean, b4, bt1);
        sw (re5aCrunch, b5, bt1);
        sw (re5bClean, w4, v1in);
        sw (re5bCrunch, w5, v1in);
        c.addResistor (v1in, p6, 220.0e3);
        c.addResistor (v1in, p7, 330.0e3);
        ch.rCleanVolTop = c.addResistor (p6, w6, 125.0e3);
        ch.rCleanVolBot = c.addResistor (w6, gnd, 125.0e3);
        ch.rCrunchVolTop = c.addResistor (p7, w7, 125.0e3);
        ch.rCrunchVolBot = c.addResistor (w7, gnd, 125.0e3);
        sw (re4aClean, w6, out1);
        sw (re4aCrunch, w7, out1);

        // CH II stack: C13 470p (+ C12 470p, RE7A) -> Lead Treble P14 250KB; R31 100K slope; C14 220n; Lead Bass P11 1MA;
        // RE8 picks Open Middle P12 20KB + C16 22n or Focused Middle P13 20KB + C15 47n. Treble wiper -> R33 470K ->
        // P15 Lo Lead 250KB and R35 470K -> P16 Hi Lead 250KB; Lo / Hi Lead Bottom add R32 330K from the bass node
        // through R34 / R36 680K. RE4B picks the wiper.
        const auto t2 = c.addNode(), c12n = c.addNode(), s2 = c.addNode(), bt2 = c.addNode(), mt2 = c.addNode();
        const auto w14 = c.addNode(), p12 = c.addNode(), p13 = c.addNode(), c16n = c.addNode(), c15n = c.addNode();
        const auto p15 = c.addNode(), w15 = c.addNode(), p16 = c.addNode(), w16 = c.addNode(), r32n = c.addNode();
        const auto llbn = c.addNode(), hlbn = c.addNode(), out2 = c.addNode();
        c.addCapacitor (in2, t2, 470.0e-12);
        c.addCapacitor (in2, c12n, 470.0e-12);
        sw (re7aFocused, c12n, t2);
        c.addResistor (in2, s2, 100.0e3);
        c.addCapacitor (s2, bt2, 220.0e-9);
        ch.rLeadTrebleTop = c.addResistor (t2, w14, 125.0e3);
        ch.rLeadTrebleBot = c.addResistor (w14, bt2, 125.0e3);
        ch.rBass2 = c.addResistor (bt2, mt2, 500.0e3);
        sw (re8bOpen, mt2, p12);
        sw (re8bFocused, mt2, p13);
        ch.rMidOpen = c.addResistor (p12, gnd, 10.0e3);
        ch.rMidFocused = c.addResistor (p13, gnd, 10.0e3);
        sw (re8aOpen, s2, c16n);
        c.addCapacitor (c16n, p12, 22.0e-9);
        sw (re8aFocused, s2, c15n);
        c.addCapacitor (c15n, p13, 47.0e-9);
        c.addResistor (w14, p15, 470.0e3);           // R33
        c.addResistor (w14, p16, 470.0e3);           // R35
        c.addResistor (bt2, r32n, 330.0e3);          // R32
        sw (llb, r32n, llbn);
        c.addResistor (llbn, p15, 680.0e3);          // R34
        sw (hlb, r32n, hlbn);
        c.addResistor (hlbn, p16, 680.0e3);          // R36
        ch.rLoVolTop = c.addResistor (p15, w15, 125.0e3);
        ch.rLoVolBot = c.addResistor (w15, gnd, 125.0e3);
        ch.rHiVolTop = c.addResistor (p16, w16, 125.0e3);
        ch.rHiVolBot = c.addResistor (w16, gnd, 125.0e3);
        sw (re4bLo, w15, out2);
        sw (re4bHi, w16, out2);

        // RE2A -> R40 220K / R41 100K -> V7A (the FX send: R43 100K || C17 10n plate, 1K unbypassed cathode, CE6 4.7u out
        // of the cathode into R44 47K). The unplugged jacks pass that to the Balance: R45 10K + half of P19 (25K)
        // -> V7B (R47 100K leak; R49 100K, R48 1.5K + CE7 22u). C18 100n -> R50 / R51 100K -> Master A / B (1MB) -> RE9A.
        ch.pVolOut = c.addNode();
        sw (re2aCh1, out1, ch.pVolOut);
        sw (re2aCh2, out2, ch.pVolOut);
        ch.tGrid7a = c.addNode();
        c.addResistor (ch.pVolOut, ch.tGrid7a, 220.0e3);  // R40
        c.addResistor (ch.tGrid7a, gnd, 100.0e3);         // R41
    }

    // ================================================================ V7A (FX send) -> Balance -> V7B (return) -> Master A / B
    {
        auto& c = ch.post;
        const auto vcc = c.addNode();
        ch.qSrcVcc = c.addSource (vcc, 400.0);
        const auto g7a = c.addNode();
        ch.qSrcGrid = c.addSource (g7a, 0.0);
        const auto sw = [&] (Sw s, NodalCircuit::Node a, NodalCircuit::Node b) { ch.sw[(size_t) s] = c.addResistor (a, b, relayOff); };
        const auto stage = [&] (NodalCircuit::Node grid, double rPlate, double rk, double ck, double plateGuess, double kGuess)
        {
            const auto plate = c.addNode(), k = c.addNode();
            c.addTriode (plate, grid, k, triode12AX7());
            c.addCapacitor (grid, plate, cgp);
            c.addResistor (vcc, plate, rPlate);
            c.addResistor (k, gnd, rk);
            if (ck > 0.0)
                c.addCapacitor (k, gnd, ck);
            c.setInitialGuess (plate, plateGuess);
            c.setInitialGuess (k, kGuess);
            return plate;
        };
        const auto k7a = c.addNode(), sl = c.addNode(), bal = c.addNode(), g7b = c.addNode();
        ch.pPlateV7a = c.addNode();
        c.addTriode (ch.pPlateV7a, g7a, k7a, triode12AX7());
        c.addCapacitor (g7a, ch.pPlateV7a, cgp);
        c.addResistor (vcc, ch.pPlateV7a, 100.0e3);
        c.addCapacitor (vcc, ch.pPlateV7a, 10.0e-9);
        c.addResistor (k7a, gnd, 1.0e3);
        c.setInitialGuess (ch.pPlateV7a, 230.0);
        c.setInitialGuess (k7a, 1.2);
        c.addCapacitor (k7a, sl, 4.7e-6);
        c.addResistor (sl, gnd, 47.0e3);
        c.addResistor (sl, bal, 10.0e3);
        c.addResistor (bal, g7b, 25.0e3);
        c.addResistor (g7b, gnd, 100.0e3);
        ch.pPlateV7b = stage (g7b, 100.0e3, 1.5e3, 22.0e-6, 230.0, 1.5);
        const auto mn = c.addNode(), pa = c.addNode(), wa = c.addNode(), pb = c.addNode(), wb = c.addNode();
        ch.pMaster = c.addNode();
        c.addCapacitor (ch.pPlateV7b, mn, 100.0e-9);
        c.addResistor (mn, pa, 100.0e3);
        c.addResistor (mn, pb, 100.0e3);
        ch.rMasterATop = c.addResistor (pa, wa, 0.5e6);
        ch.rMasterABot = c.addResistor (wa, gnd, 0.5e6);
        ch.rMasterBTop = c.addResistor (pb, wb, 0.5e6);
        ch.rMasterBBot = c.addResistor (wb, gnd, 0.5e6);
        sw (re9aA, wa, ch.pMaster);
        sw (re9aB, wb, ch.pMaster);
        c.addResistor (ch.pMaster, gnd, 1.0e6);      // R52
    }

    // ================================================================ PI + power amp + OT + speaker + NFB (full only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto masterIn = c.addNode();
        ch.wSrcMaster = c.addSource (masterIn, 0.0);
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 430.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // PI V8 (sheet 1): C19 100n into V8A; both grids biased from the tail node T (R53 100K, R59 150K); cathodes ->
        // R55 470R -> T -> R56 22K -> B -> R57 4.7K -> ground; V8B's grid AC-grounded to B by C24 100n; the feedback
        // network lands on B through R58 1.5K. Plates R61 82K / R62 100K.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), tt = c.addNode(),
                   bb = c.addNode(), f = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = tt;
        ch.wFeedback = bb;
        c.addCapacitor (masterIn, g1, 100.0e-9);
        c.addResistor (g1, tt, 100.0e3);
        c.addResistor (g2, tt, 150.0e3);
        c.addCapacitor (g2, bb, 100.0e-9);
        c.addTriode (pa, g1, k, triode12AX7());
        c.addTriode (pb, g2, k, triode12AX7());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);
        c.addResistor (vpi, pb, 100.0e3);
        c.addResistor (k, tt, 470.0);
        c.addResistor (tt, bb, 22.0e3);
        c.addResistor (bb, gnd, 4.7e3);
        c.addResistor (f, bb, 1.5e3);
        c.setInitialGuess (pa, 250.0);
        c.setInitialGuess (pb, 250.0);
        c.setInitialGuess (k, 42.0);
        c.setInitialGuess (tt, 41.0);
        c.setInitialGuess (g1, 41.0);
        c.setInitialGuess (g2, 41.0);
        c.setInitialGuess (bb, 7.0);

        // Power: 4 x 6L6GC as two push-pull pairs; C20..C23 47n, 330K leaks, 2.2K stoppers.
        const auto g3 = c.addNode(), g4 = c.addNode(), g3s = c.addNode(), g4s = c.addNode(), nb = c.addNode(), nbt = c.addNode();
        ch.wBias = nb;
        ch.wPowerGridA = g3s;
        const auto pp1 = c.addNode(), pp2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
        ch.wPP1 = pp1;
        ch.wPP2 = pp2;
        const auto swn = c.addNode();
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
        ch.penA = c.addPentode (pp1, g3s, gnd, pentode6L6Pair(), railPlatesNominal - 24.0);
        ch.penB = c.addPentode (pp2, g4s, gnd, pentode6L6Pair(), railPlatesNominal - 24.0);

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
        c.addCoupledInductors ({ { a1, pp1 }, { a2, pp2 }, { swn, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        c.addResistor (swn, ch.wOut, secondaryResistance);
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

        // Feedback network (sheet 1): FB -> Presence P8 50KC (its wiper is F, its far end returns through C25 100n) and
        // Depth P9 1MA rheostat -> C27 10n -> R60 22K -> F.
        {
            const auto pc = c.addNode(), d1 = c.addNode(), d2 = c.addNode();
            ch.rPresTop = c.addResistor (ch.wOut, f, 25.0e3);
            ch.rPresBot = c.addResistor (f, pc, 25.0e3);
            c.addCapacitor (pc, gnd, 100.0e-9);
            ch.rDepth = c.addResistor (ch.wOut, d1, 500.0e3);
            c.addCapacitor (d1, d2, 10.0e-9);
            c.addResistor (d2, f, 22.0e3);
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

void ENGLPowerballStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const auto split = [] (double total, double fraction, int top, int bot, NodalCircuit& c)
    {
        const double b = juce::jmax (1.0, total * fraction);
        c.setResistance (bot, b);
        c.setResistance (top, juce::jmax (1.0, total - b));
    };
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);
    // More Presence = the wiper toward C25: less treble fed back, brighter (P8 is a C taper).
    const double presTop = juce::jmax (1.0, 50.0e3 * reverseAudio (k.presence));
    const double depthR = juce::jmax (1.0, 1.0e6 * (1.0 - pots::audio (k.depth)));

    for (auto& ch : channels)
    {
        auto& c = ch.tone;
        const bool chTwo = k.channel >= 2;
        split (1.0e6, pots::audio (k.gain), chTwo ? ch.rGain2Top : ch.rGain1Top, chTwo ? ch.rGain2Bot : ch.rGain1Bot, ch.pre);
        if (chTwo)
        {
            split (250.0e3, k.treble, ch.rLeadTrebleTop, ch.rLeadTrebleBot, c);
            c.setResistance (ch.rBass2, juce::jmax (1.0, 1.0e6 * pots::audio (k.bass)));
            c.setResistance (ch.rMidOpen, juce::jmax (1.0, 20.0e3 * k.mid));
            c.setResistance (ch.rMidFocused, juce::jmax (1.0, 20.0e3 * k.midFocused));
            if (k.channel == 2)
                split (250.0e3, k.volume, ch.rLoVolTop, ch.rLoVolBot, c);
            else
                split (250.0e3, k.volume, ch.rHiVolTop, ch.rHiVolBot, c);
        }
        else
        {
            if (k.channel == 0)
            {
                split (250.0e3, k.treble, ch.rCleanTrebleTop, ch.rCleanTrebleBot, c);
                split (250.0e3, k.volume, ch.rCleanVolTop, ch.rCleanVolBot, c);
            }
            else
            {
                split (250.0e3, k.treble, ch.rCrunchTrebleTop, ch.rCrunchTrebleBot, c);
                split (250.0e3, pots::audio (k.volume), ch.rCrunchVolTop, ch.rCrunchVolBot, c);
            }
            c.setResistance (ch.rBass1, juce::jmax (1.0, 1.0e6 * pots::audio (k.bass)));
            c.setResistance (ch.rMid1, juce::jmax (1.0, 20.0e3 * k.mid));
        }
        split (1.0e6, k.masterA, ch.rMasterATop, ch.rMasterABot, ch.post);
        split (1.0e6, k.masterB, ch.rMasterBTop, ch.rMasterBBot, ch.post);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresTop, presTop);
            ch.power.setResistance (ch.rPresBot, juce::jmax (1.0, 50.0e3 - presTop));
            ch.power.setResistance (ch.rDepth, depthR);
            ch.power.setResistance (ch.rBiasTrim, trim);
        }
        if (k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifier * idleSupplyCurrent);
    }
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

/** The relays and LDRs per mode (docs/circuits/ENGLPowerball.md): Ch I/II drives LD1 / LD2, RE1, RE2; Gain Lo/Hi drives
    RE3, RE4, RE5; Style drives RE6, RE7, RE8; Master A/B drives RE9. Bright and the three Bottom buttons are the panel's
    own switches (the hidden modes' Bottom buttons keep their last state). Each contact fades in the log domain. */
void ENGLPowerballStyleAmplifierProcessor::updateSwitches (bool snap)
{
    const int mode = juce::jlimit (0, 3, juce::roundToInt (channelParam->get()));
    const bool chTwo = mode >= 2, hi = mode == 1 || mode == 3;
    const bool focused = styleParam->get() >= 0.5f;
    const bool bottomOn = bottomParam->get() >= 0.5f;
    std::array<int, numSw> want {};
    for (int s = 0; s < numSw; ++s)
        want[(size_t) s] = -1; // keep
    const auto set = [&] (Sw s, bool on) { want[(size_t) s] = on ? 1 : 0; };
    set (ld1, ! chTwo);
    set (ld2, chTwo);
    set (bright, brightParam->get() >= 0.5f);
    if (! chTwo)
        set (bottomCh1, bottomOn);
    else
        set (mode == 2 ? llb : hlb, bottomOn);
    for (auto s : { re3aLo, re3bLo, re4aClean, re4bLo, re5aClean, re5bClean })
        set (s, ! hi);
    for (auto s : { re3aHi, re3bHi, re4aCrunch, re4bHi, re5aCrunch, re5bCrunch })
        set (s, hi);
    for (auto s : { re1aCh1, re1bMute, re2bCh1, re2aCh1 })
        set (s, ! chTwo);
    for (auto s : { re1aCh2, re2bCh2, re2aCh2 })
        set (s, chTwo);
    for (auto s : { re6aFocused, re7aFocused, re8aFocused, re8bFocused })
        set (s, focused);
    for (auto s : { re6bOpen, re8aOpen, re8bOpen })
        set (s, ! focused);
    set (re9aA, masterSelParam->get() < 0.5f);
    set (re9aB, masterSelParam->get() >= 0.5f);

    for (int s = 0; s < numSw; ++s)
    {
        const bool ldr = s == ld1 || s == ld2;
        double& now = swLog[(size_t) s];
        if (want[(size_t) s] < 0)
        {
            if (! snap)
                continue;
            want[(size_t) s] = now < 0.5 * (std::log (ldr ? ldrOn : relayOn) + std::log (ldr ? ldrOff : relayOff)) ? 1 : 0;
        }
        const double on = std::log (ldr ? ldrOn : relayOn), off = std::log (ldr ? ldrOff : relayOff);
        const double target = want[(size_t) s] == 1 ? on : off;
        if (! snap && juce::exactlyEqual (now, target))
            continue;
        const double step = (off - on) * (double) controlInterval / ((ldr ? ldrFadeSeconds : relayFadeSeconds) * sampleRate);
        now = snap ? target : (target > now ? juce::jmin (target, now + step) : juce::jmax (target, now - step));
        const bool inPre = s == ld1 || s == ld2 || s == bottomCh1 || s == bright || s == re3aLo || s == re3aHi || s == re3bLo
                        || s == re3bHi || s == re1aCh1 || s == re1aCh2 || s == re1bMute;
        const bool inPost = s == re9aA || s == re9aB;
        for (auto& ch : channels)
            (inPre ? ch.pre : inPost ? ch.post : ch.tone).setResistance (ch.sw[(size_t) s], std::exp (now));
    }
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
    ch.post.restoreDynamicState (ch.postRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.screenDropA = ch.screenDropB = 0.0;
    ch.sumPlate = ch.sumScreen = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
    ch.vScreen = ch.supply.voltage (ch.sB);
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
    // The preamp rail is set once in prepare(), not here: the model lumps every preamp stage on one node, and the
    // power stage's current then reached V5A through it -- a ~28 Hz supply loop (motorboating) the real amp's chain
    // of RC decoupling stages (R26 4.7K + CE3, R138 + CE10, ...) does not have.
    ch.vScreen = rail (ch.sB, 560.0);
}

double ENGLPowerballStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double drive) const noexcept
{
    constexpr double attackMs = 8.0, releaseMs = 45.0;
    const double absDrive = std::abs (drive);
    const double tauMs = absDrive > ch.bmEnvelope ? attackMs : releaseMs;
    const double coeff = 1.0 - std::exp (-1.0 / (0.001 * tauMs * juce::jmax (1.0, sampleRate)));
    ch.bmEnvelope += coeff * (absDrive - ch.bmEnvelope);
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
    const double raw = sat (drive + shift) - sat (shift);

    const double lf = bmAB0 * raw + bmAB1 * ch.bmAX1 + bmAB2 * ch.bmAX2 - bmAA1 * ch.bmAY1 - bmAA2 * ch.bmAY2;
    ch.bmAX2 = ch.bmAX1; ch.bmAX1 = raw;
    ch.bmAY2 = ch.bmAY1; ch.bmAY1 = lf;
    const double top = bmBB0 * lf + bmBB1 * ch.bmBX1 + bmBB2 * ch.bmBX2 - bmBA1 * ch.bmBY1 - bmBA2 * ch.bmBY2;
    ch.bmBX2 = ch.bmBX1; ch.bmBX1 = lf;
    ch.bmBY2 = ch.bmBY1; ch.bmBY1 = top;
    // Presence / Depth live in the feedback network of the full netlist: here a first-order HF shelf (flat at the 0.3
    // default) and a lean toward the LF bump.
    const double presAlpha = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 4500.0 / juce::jmax (1.0, sampleRate));
    ch.bmPresState += presAlpha * (top - ch.bmPresState);
    const double presShelf = ch.bmPresState + (0.64 + 1.2 * juce::jlimit (0.0, 1.0, (double) lastKnobs.presence)) * (top - ch.bmPresState);
    const double effTop = presShelf + (juce::jlimit (0.0, 1.0, (double) lastKnobs.depth) - 0.3) * 0.8 * (lf - raw);
    const double cut = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * bmCutHz / juce::jmax (1.0, sampleRate));
    ch.bmCutState += cut * (effTop - ch.bmCutState);
    ch.bmOutput = juce::jlimit (-bmOutMax, bmOutMax, ch.bmTail.process ((effTop - ch.bmCutState) * bmLevelTrim));
    return ch.bmOutput;
}

void ENGLPowerballStyleAmplifierProcessor::designPowerFilters()
{
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
    bilinear (1.0 / (wb * wb), 1.0 / (bmBumpQz * wb), 1.0,
              1.0 / (wb * wb), 1.0 / (bmBumpQp * wb), 1.0,
              sampleRate, bmAB0, bmAB1, bmAB2, bmAA1, bmAA2);
    const double wz = 2.0 * juce::MathConstants<double>::pi * bmTopZHz, wp = 2.0 * juce::MathConstants<double>::pi * bmTopPHz;
    bilinear (0.0, 1.0 / wz, 1.0,
              1.0 / (wp * wp), 1.0 / (bmTopQp * wp), 1.0,
              sampleRate, bmBB0, bmBB1, bmBB2, bmBA1, bmBA2);
}

double ENGLPowerballStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::u5aPlate: return ch.pre.voltage (ch.pPlateV5a);
        case Probe::u5bPlate: return ch.pre.voltage (ch.pPlateV5b);
        case Probe::u6aPlate: return ch.pre.voltage (ch.pPlateV6a);
        case Probe::u6bPlate: return ch.pre.voltage (ch.pPlateV6b);
        case Probe::u7aPlate: return ch.post.voltage (ch.pPlateV7a);
        case Probe::u7bPlate: return ch.post.voltage (ch.pPlateV7b);
        case Probe::toneStackOut: return ch.tone.voltage (ch.pVolOut);
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

double ENGLPowerballStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a, b, sa, sb;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
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
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, voicings::highGainENGL, 1.0 / outputScale);
    }

    auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p, double seconds)
    {
        s.reset (newSampleRate, seconds);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedGain, gainParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedMid, midParam, 0.02);
    setup (smoothedMidFocused, midFocusedParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedPresence, presenceParam, 0.02);
    setup (smoothedDepth, depthParam, 0.02);
    setup (smoothedMasterA, masterAParam, 0.02);
    setup (smoothedMasterB, masterBParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.18;
    appliedSpeaker = matchedSpeaker;
    // Every mode's pots from its stored row (the hidden ones keep them), then the visible mode on top.
    const int visible = juce::jlimit (0, 3, juce::roundToInt (channelParam->get()));
    for (int mode : { 0, 1, 2, 3, visible })
    {
        const auto mainSlot = mode >= 2 ? 1 : 0, trebleSlot = juce::jmin (mode, 2);
        updatePots ({ mainMemory->slotValue (mainSlot, 0), trebleMemory->slotValue (trebleSlot, 0), mainMemory->slotValue (mainSlot, 2),
                      mainMemory->slotValue (mainSlot, 3), mainMemory->slotValue (mainSlot, 1), volumeMemory->slotValue (mode, 0),
                      presenceParam->get(), depthParam->get(), masterAParam->get(), masterBParam->get(), powerParam->get(),
                      biasParam->get(), tubeFeelParam->get(), juce::roundToInt (speakerParam->get()), mode });
    }
    // The three Bottom buttons and every relay from their states.
    {
        const auto onOff = [&] (Sw s, bool on)
        {
            swLog[(size_t) s] = std::log (on ? relayOn : relayOff);
        };
        onOff (bottomCh1, bottomMemory->slotValue (0, 0) >= 0.5f);
        onOff (llb, bottomMemory->slotValue (1, 0) >= 0.5f);
        onOff (hlb, bottomMemory->slotValue (2, 0) >= 0.5f);
    }
    updateSwitches (true);

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        const double feel = 0.05 + 0.95 * (double) tubeFeelParam->get();
        bool passOk = true;
        double iPiRun = 0.004, iPreRun = 0.006, ipRun = 0.16, isRun = 0.012;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);
            ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.plateDcV6b = ch.pre.voltage (ch.pPlateV6b);
            ch.tone.setSource (ch.tSrcPlate, 0.0);
            passOk = ch.tone.prepare (newSampleRate) && passOk;
            ch.post.setSource (ch.qSrcVcc, ch.supply.voltage (ch.sD));
            ch.post.setSource (ch.qSrcGrid, 0.0);
            passOk = ch.post.prepare (newSampleRate) && passOk;

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
            const double vPre = ch.supply.voltage (ch.sD);
            const double iPre = (vPre - ch.pre.voltage (ch.pPlateV5a)) / 100.0e3 + (vPre - ch.pre.voltage (ch.pPlateV5b)) / 100.0e3
                              + (vPre - ch.pre.voltage (ch.pPlateV6a)) / 330.0e3 + (vPre - ch.pre.voltage (ch.pPlateV6b)) / 100.0e3
                              + (vPre - ch.post.voltage (ch.pPlateV7a)) / 100.0e3 + (vPre - ch.post.voltage (ch.pPlateV7b)) / 100.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
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
        ch.pre.setSource (ch.pSrcVcc, ch.supply.voltage (ch.sD));
        dcOk = ch.pre.prepare (newSampleRate) && dcOk;
        ch.plateDcV6b = ch.pre.voltage (ch.pPlateV6b);
        dcOk = ch.tone.prepare (newSampleRate) && dcOk;
        ch.post.setSource (ch.qSrcVcc, ch.supply.voltage (ch.sD));
        dcOk = ch.post.prepare (newSampleRate) && dcOk;
        if (! reducedOrder)
        {
            ch.power.setSource (ch.wSrcCt, ch.supply.voltage (ch.sA));
            ch.power.setSource (ch.wSrcPi, ch.supply.voltage (ch.sC));
            ch.power.setSource (ch.wSrcMaster, 0.0);
            ch.power.setPentodeScreen (ch.penA, ch.vScreen - 1.5);
            ch.power.setPentodeScreen (ch.penB, ch.vScreen - 1.5);
            dcOk = ch.power.prepare (newSampleRate) && dcOk;
        }
        ch.pre.saveDynamicState (ch.preRest);
        ch.tone.saveDynamicState (ch.toneRest);
        ch.post.saveDynamicState (ch.postRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = ch.bmOutput = 0.0;
        ch.bmAX1 = ch.bmAX2 = ch.bmAY1 = ch.bmAY2 = 0.0;
        ch.bmBX1 = ch.bmBX2 = ch.bmBY1 = ch.bmBY2 = 0.0;
        ch.bmCutState = ch.bmPresState = 0.0;
        ch.piCoupling.prepare (newSampleRate, 100.0e-9, 100.0e3, 0.0); // C19 into R53
        ch.chanTrim = std::pow (10.0, modeLevelMatchDb[(size_t) visible] / 20.0);
    }
    designPowerFilters();
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
    smoothedMidFocused.setTargetValue (midFocusedParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedPresence.setTargetValue (presenceParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    smoothedMasterA.setTargetValue (masterAParam->get());
    smoothedMasterB.setTargetValue (masterBParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int mode = juce::jlimit (0, 3, juce::roundToInt (channelParam->get()));
    const double chanTrimTarget = std::pow (10.0, modeLevelMatchDb[(size_t) mode] / 20.0);
    const double chanTrimAlpha = 1.0 - std::exp (-1.0 / (0.01 * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const float gn = smoothedGain.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float mi = smoothedMid.getNextValue();
        const float mf = smoothedMidFocused.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float vo = smoothedVolume.getNextValue();
        const float pr = smoothedPresence.getNextValue();
        const float dp = smoothedDepth.getNextValue();
        const float ma = smoothedMasterA.getNextValue();
        const float mb = smoothedMasterB.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, mf, ba, vo, pr, dp, ma, mb, pw, bi, fe, speakerChoice, mode });
            updateSwitches (false);
        }

        // Power Drive (page 2) scales the Master's output into the power section, unity at noon.
        const double drive = juce::jmax (0.002, pots::audio ((double) pw)) / pots::audio (0.5);
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            ch.tone.setSource (ch.tSrcPlate, ch.pre.voltage (ch.pPlateV6b) - ch.plateDcV6b);
            const bool okTone = ch.tone.solveSample();
            ch.post.setSource (ch.qSrcGrid, ch.tone.voltage (ch.tGrid7a));
            const bool okPost = ch.post.solveSample();
            const double master = drive * ch.post.voltage (ch.pMaster);
            bool ok = okPre && okTone && okPost;

            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcMaster, master);
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

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.piCoupling.process (master))
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
                    ch.post.saveDynamicState (ch.postRest);
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
                ch.chanTrim += chanTrimAlpha * (chanTrimTarget - ch.chanTrim);
                out = speakerVolts * outputScale * outGain * speakerGain * ch.chanTrim;
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
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
