#include "DualRectifierStyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/DualRectifier.md). Factory Mesa Boogie Dual Rectifier (2-channel, 6-93 GEO. M.).
    // Solid-state rectification is the Bold default; the drec_spongy param models the
    // tube-rectifier mode (more B+ drop, deeper sag, ~6% lower plate rail).
    // Rails: A=460V plates, B=454V screens, C=422V (PI), D=406V (V3A), E=402V (V1A/V2).
    // Chain: Rect → A (220µF, 150K bleeder) → CHOKE → B (220µF) → 2.7K → C (30µF) → 22K → D (30µF) → 15K → E (30µF). ----
    constexpr double railPlatesNominal = 460.0;
    constexpr double railScreensNominal = 454.0;
    constexpr double rectifierResistance = 60.0;
    constexpr double chokeResistance = 50.0;
    constexpr double chokeInductance = 10.0;
    constexpr double bleeder = 150.0e3;
    constexpr double screenResistor = 500.0;  // 1K per tube / 2 per pair

    // ---- reduced-order power stage sag table (Twin Reverb's data scaled from 497V to 460V) ----
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

    // Output transformer: 4x6L6GC, ~100 W. Same class as the SLO-100 / Mark IIC+.
    constexpr double primaryHalfInductance = 3.0;
    constexpr double halfToSecondaryTurns = 6.25;
    constexpr double couplingHalves = 0.9997;
    constexpr double couplingSecondary = 0.995;
    constexpr double primaryHalfResistance = 45.0;
    constexpr double secondaryResistance = 0.15;
    constexpr double feedbackResistor = 47.0e3; // R276, from the 16 ohm tap the model also drives

    // BIAS switch (power-supply sheet): F = -51 V with 6L6, -39 V with EL34.
    constexpr double biasSupplyVolts = -51.0;
    constexpr double biasSupplyVoltsEL34 = -39.0;

    /** An EL34 pair: Koren's published EL34 set (Ex 1.35, kg1 650, kg2 4200, Kp 60, Kvb 24) with mu re-placed from 11 to
        12.4 so a tube idles at ~40 mA on this amp's -37 V grid / 454 V screen (the Bias Switch's -39 V through the bias
        divider) -- a real EL34 Recto's idle. Koren's mu 11 idles at ~84 mA there and the codebase's plexi-region fit
        (Super Lead, placed for -55 V) at ~220 mA. Peak current at 0 V grid stays ~600 mA. */
    KorenPentode::Parameters pentodeEL34Pair()
    {
        KorenPentode::Parameters p;
        p.mu = 12.4;
        p.ex = 1.35;
        p.kg1 = 650.0 / 2.0;
        p.kg2 = 4200.0 / 2.0;
        p.kp = 60.0;
        p.kvb = 24.0;
        p.grid.Gg *= 2.0;
        p.arcResistance /= 2.0;
        return p;
    }

    // The switching LDRs: lit ~500 ohm, dark ~50M (VTL5C-class cells). A mode change fades each one between them in the
    // log domain over ldrFadeSeconds, the way the cells' own response does, so a switch never steps the netlist.
    constexpr double ldrOn = 500.0;
    constexpr double ldrOff = 50.0e6;
    constexpr double ldrFadeSeconds = 0.02;

    // NFB (power-amp sheet): FB -> C51 .1 -> R276 47K, a second 47K joining through LDR20 ("more feedback"), all through
    // LDR19 ("feedback"). Modern has none.
    constexpr double nfbLeg = 47.0e3;

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2;
    constexpr double powerTheta = 0.9;

    // ---- preamp helper ----
    struct PreampBuild
    {
        int srcV2 = 0, srcV3 = 0, srcE = 0, srcIn = 0;
        int rRdGainTop = 0, rRdGainBot = 0, rOrGainTop = 0, rOrGainBot = 0;
        int ldr1 = 0, ldr2 = 0, ldr3a = 0, ldr3b = 0, ldr4 = 0, ldr5 = 0, ldr6 = 0, ldr10 = 0;
        NodalCircuit::Node plateV1a = 0, plateV2a = 0, plateV2b = 0, plateV3a = 0, follower = 0;
    };

    /** The 2-channel preamp, factory drawing "PREAMP RF-1F" (6-93 GEO. M.; docs/circuits/DualRectifier.md). One
        tube chain V1A -> V2A -> V2B -> V3A -> V3B follower serves all five modes; the LDRs (light-dependent resistors,
        `ldrOn` / `ldrOff` here) re-voice it per the drawing's own truth table:
        - V1A / V2A / V3A cathodes: 1.8K with 1uF in series with 47K -- LDR3b / LDR3a / LDR10 short the 47K (full 1uF
          bypass); CLEAN leaves them open.
        - V1A out: C21 .02 -> R322 2.2M || C10 82p (R321 2.2M to ground), and R110 680K || C9 .002 joining through LDR4
          (the "V1a output pad"); CLEAN keeps only the 2.2M leg, a ~-10 dB bright pad.
        - LDR1 / LDR2 pick the RED / ORANGE Gain pot (1MA each, .001 top-to-wiper), LDR5 / LDR6 their wipers ->
          R241 470K -> V2A (C12 20p).
        - V2A 100K plate; C22 .02 -> R242 470K -> V2B (R212 1M); V2B 100K || C6 .001 plate, 39K unbypassed cathode;
          C27 .02 -> R106 330K / R225 220K -> V3A (220K plate) -> V3B follower.
        Rails: E (402 V) V1A, D (406 V) V2A / V2B, C (422 V) V3A / V3B. */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop, double eGuess, double dGuess, double cGuess)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b;

        const auto vccE = c.addNode(), vccD = c.addNode(), vccC = c.addNode(), in = c.addNode();
        b.srcE = c.addSource (vccE, eGuess);
        b.srcV2 = c.addSource (vccD, dGuess);
        b.srcV3 = c.addSource (vccC, cGuess);
        b.srcIn = c.addSource (in, 0.0);

        // V1A: the input jack straight to the grid (R211 1M leak), R221 220K plate, R291 1.8K + C31 1uF / R271 47K.
        const auto k1 = c.addNode(), kc1 = c.addNode();
        b.plateV1a = c.addNode();
        c.addResistor (in, gnd, 1.0e6);                  // R211
        c.addTriode (b.plateV1a, in, k1, triode12AX7());
        c.addCapacitor (in, b.plateV1a, cgp);
        c.addResistor (vccE, b.plateV1a, 220.0e3);       // R221
        c.addResistor (k1, gnd, 1.8e3);                  // R291
        c.addCapacitor (k1, kc1, 1.0e-6);                // C31
        c.addResistor (kc1, gnd, 47.0e3);                // R271
        b.ldr3b = c.addResistor (kc1, gnd, ldrOff);
        c.setInitialGuess (b.plateV1a, 200.0);
        c.setInitialGuess (k1, 1.6);

        // V1A output pad and the two Gain pots.
        const auto x = c.addNode(), w = c.addNode(), y = c.addNode();
        c.addCapacitor (b.plateV1a, x, 0.02e-6);         // C21
        c.addResistor (x, gnd, 2.2e6);                   // R321
        c.addResistor (x, w, 2.2e6);                     // R322
        c.addCapacitor (x, w, 82.0e-12);                 // C10
        c.addResistor (x, y, 680.0e3);                   // R110
        c.addCapacitor (x, y, 0.002e-6);                 // C9
        b.ldr4 = c.addResistor (y, w, ldrOff);
        const auto rdTop = c.addNode(), rdWiper = c.addNode(), orTop = c.addNode(), orWiper = c.addNode();
        b.ldr1 = c.addResistor (w, rdTop, ldrOff);
        b.ldr2 = c.addResistor (w, orTop, ldrOff);
        b.rRdGainTop = c.addResistor (rdTop, rdWiper, 0.5e6);    // RED GAIN 1MA
        b.rRdGainBot = c.addResistor (rdWiper, gnd, 0.5e6);
        c.addCapacitor (rdTop, rdWiper, 0.001e-6);
        b.rOrGainTop = c.addResistor (orTop, orWiper, 0.5e6);    // OR GAIN 1MA
        b.rOrGainBot = c.addResistor (orWiper, gnd, 0.5e6);
        c.addCapacitor (orTop, orWiper, 0.001e-6);

        // V2A: R241 470K / C12 20p grid, R201 100K plate, R292 1.8K + C32 1uF / R272 47K.
        const auto gsel = c.addNode(), g2 = c.addNode(), k2 = c.addNode(), kc2 = c.addNode();
        b.plateV2a = c.addNode();
        b.ldr5 = c.addResistor (rdWiper, gsel, ldrOff);
        b.ldr6 = c.addResistor (orWiper, gsel, ldrOff);
        c.addResistor (gsel, g2, 470.0e3);               // R241
        c.addCapacitor (g2, gnd, 20.0e-12);              // C12
        c.addTriode (b.plateV2a, g2, k2, triode12AX7());
        c.addCapacitor (g2, b.plateV2a, cgp);
        c.addResistor (vccD, b.plateV2a, 100.0e3);       // R201
        c.addResistor (k2, gnd, 1.8e3);                  // R292
        c.addCapacitor (k2, kc2, 1.0e-6);                // C32
        c.addResistor (kc2, gnd, 47.0e3);                // R272
        b.ldr3a = c.addResistor (kc2, gnd, ldrOff);
        c.setInitialGuess (b.plateV2a, 280.0);
        c.setInitialGuess (k2, 2.0);

        // V2B: C22 .02 -> R242 470K, R212 1M leak; R202 100K || C6 .001 plate, R103 39K unbypassed (384 V / 6 V).
        const auto c3 = c.addNode(), g3 = c.addNode(), k3 = c.addNode();
        b.plateV2b = c.addNode();
        c.addCapacitor (b.plateV2a, c3, 0.02e-6);        // C22
        c.addResistor (c3, g3, 470.0e3);                 // R242
        c.addResistor (g3, gnd, 1.0e6);                  // R212
        c.addTriode (b.plateV2b, g3, k3, triode12AX7());
        c.addCapacitor (g3, b.plateV2b, cgp);
        c.addResistor (vccD, b.plateV2b, 100.0e3);       // R202
        c.addCapacitor (vccD, b.plateV2b, 0.001e-6);     // C6
        c.addResistor (k3, gnd, 39.0e3);                 // R103
        c.setInitialGuess (b.plateV2b, 384.0);
        c.setInitialGuess (k3, 6.0);

        // V3A: C27 .02 -> R106 330K to ground / R225 220K to the grid; R224 220K plate, R293 1.8K + C33 1uF / R277 47K.
        const auto c4 = c.addNode(), g4 = c.addNode(), k4 = c.addNode(), kc4 = c.addNode();
        b.plateV3a = c.addNode();
        c.addCapacitor (b.plateV2b, c4, 0.02e-6);        // C27
        c.addResistor (c4, gnd, 330.0e3);                // R106
        c.addResistor (c4, g4, 220.0e3);                 // R225
        c.addTriode (b.plateV3a, g4, k4, triode12AX7());
        c.addCapacitor (g4, b.plateV3a, cgp);
        c.addResistor (vccC, b.plateV3a, 220.0e3);       // R224
        c.addResistor (k4, gnd, 1.8e3);                  // R293
        c.addCapacitor (k4, kc4, 1.0e-6);                // C33
        c.addResistor (kc4, gnd, 47.0e3);                // R277
        b.ldr10 = c.addResistor (kc4, gnd, ldrOff);
        c.setInitialGuess (b.plateV3a, 213.0);
        c.setInitialGuess (k4, 1.6);

        // V3B: cathode follower (R203 100K) driving both tone stacks.
        b.follower = c.addNode();
        c.addFollower (b.plateV3a, b.follower, followerDrop);
        return b;
    }
}

namespace
{
// Alternates this amp's page-2 Cab selector offers -- the option ORDER must match
// between makeCabParam() (names) and the tails' setCabChoices() (profiles).
const tubeamp::CabOption* const cabAlts[] = { &tubeamp::cabs::greenback412, &tubeamp::cabs::v30_412, &tubeamp::cabs::alnico212 };
}

DualRectifierStyleAmplifierProcessor::DualRectifierStyleAmplifierProcessor()
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
    // Front panel: the channel and its own row of pots. Rear panel (manual 2chRecto.pdf): ORANGE CHANNEL GAIN (Clean /
    // Variable Hi Gain "Normal"), CHANNEL STYLE SELECT (Normal = Orange Vintage + Red Modern, or clone Orange to
    // Modern / Red to Vintage), RECTIFIER SELECT (5U4 tubes / silicon diodes), POWER (Spongy / Bold) and the BIAS
    // switch matching the power tubes (6L6 / EL34).
    auto channel = makeSwitch ("drec_channel", "Channel", { "Orange", "Red" }, 1.0f);
    auto gain = make ("drec_gain", "Gain", 0.5f);
    auto treble = make ("drec_treble", "Treble", 0.5f);
    auto mid = make ("drec_mid", "Mid", 0.5f);
    auto bass = make ("drec_bass", "Bass", 0.5f);
    auto presence = make ("drec_presence", "Presence", 0.3f);
    auto master = make ("drec_master", "Master", 0.5f);
    auto orangeGain = makeSwitch ("drec_orange_gain", "Orange Gain", { "Clean", "Hi Gain" }, 1.0f);
    auto style = makeSwitch ("drec_style", "Channel Style", { "Normal", "Orange to Modern", "Red to Vintage" }, 0.0f);
    auto rectifier = makeSwitch ("drec_rectifier", "Rectifier", { "Tubes", "Diodes" }, 1.0f);
    auto spongy = makeSwitch ("drec_spongy", "Power", { "Bold", "Spongy" }, 0.0f);
    auto tubes = makeSwitch ("drec_bias_switch", "Bias Switch", { "6L6", "EL34" }, 0.0f);
    auto output = make ("drec_output", "Output", 0.5f);
    auto power = make ("drec_power", "Power Drive", 0.5f);
    auto bias = make ("drec_bias", "Bias", 0.5f);
    auto feel = make ("drec_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "drec_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
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
    masterParam = master.get();
    orangeGainParam = orangeGain.get();
    styleParam = style.get();
    rectifierParam = rectifier.get();
    spongyParam = spongy.get();
    tubesParam = tubes.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "dualrec", "Dual Rectifier", "|", std::move (channel));
    group->addChild (std::move (gain));
    group->addChild (std::move (treble));
    group->addChild (std::move (mid));
    group->addChild (std::move (bass));
    group->addChild (std::move (presence));
    group->addChild (std::move (master));
    group->addChild (std::move (orangeGain));
    group->addChild (std::move (style));
    group->addChild (std::move (rectifier));
    group->addChild (std::move (spongy));
    group->addChild (std::move (tubes));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("drec_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    page2->addChild (std::move (output));
    auto cab = tubeamp::makeCabParam ("drec_cab", "4x12 Recto", cabAlts);
    cabParam = cab.get();
    page2->addChild (std::move (cab));
    group->addChild (std::move (page2));
    parameters = std::move (group);

    // The real amp has a full Gain/Treble/Mid/Bass/Presence/Master row per channel.
    channelMemory = std::make_unique<ChannelKnobMemory> (*channelParam,
        std::vector<juce::AudioParameterFloat*> { gainParam, trebleParam, midParam, bassParam, presenceParam, masterParam });
    orangePresence = presenceParam->get();
}

std::unique_ptr<juce::XmlElement> DualRectifierStyleAmplifierProcessor::getState() const
{
    auto xml = EffectProcessor::getState();
    channelMemory->writeState (*xml);
    return xml;
}

void DualRectifierStyleAmplifierProcessor::setState (const juce::XmlElement& state)
{
    // Presets from before the 2-channel rebuild (no drec_style) had Channel = Clean / Orange / Red and a knob slot per
    // channel: Clean becomes Orange in its Clean mode, Orange stays Orange (Hi Gain), Red stays Red.
    juce::XmlElement migrated (state);
    if (! state.hasAttribute ("drec_style") && state.hasAttribute ("drec_channel"))
    {
        const int old = juce::jlimit (0, 2, juce::roundToInt (state.getDoubleAttribute ("drec_channel")));
        migrated.setAttribute ("drec_channel", old == 2 ? 1.0 : 0.0);
        migrated.setAttribute ("drec_orange_gain", old == 0 ? 0.0 : 1.0);
        migrated.setAttribute ("drec_style", 0.0);
        const int orangeSlot = old == 0 ? 0 : 1;
        for (auto* k : { gainParam, trebleParam, midParam, bassParam, presenceParam, masterParam })
        {
            const auto id = k->paramID;
            const auto from = [&] (int slot) { return id + "_ch" + juce::String (slot); };
            if (state.hasAttribute (from (orangeSlot)))
                migrated.setAttribute (from (0), state.getDoubleAttribute (from (orangeSlot)));
            if (state.hasAttribute (from (2)))
                migrated.setAttribute (from (1), state.getDoubleAttribute (from (2)));
            migrated.removeAttribute (from (2));
        }
    }
    {
        const ChannelKnobMemory::ScopedSuspend suspend (*channelMemory);
        EffectProcessor::setState (migrated);
    }
    channelMemory->readState (migrated);
    orangePresence = channelMemory->slotValue (0, 4);
}

void DualRectifierStyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    // Solid-state rectification → choke-filtered RC chain.
    // A (220µF, 150K bleeder) → CHOKE → B (220µF) → 2.7K → C (30µF) → 22K → D (30µF) → 15K → E (30µF)
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();                           // plates rail (~460V)
        const auto nl = c.addNode();
        ch.sB = c.addNode();                           // screens rail (~454V)
        ch.sC = c.addNode();                           // PI supply (~422V)
        ch.sD = c.addNode();                           // V2/V3 (~406V)
        ch.sE = c.addNode();                           // V1A (~402V)

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 220.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 220.0e-6);
        c.addResistor (ch.sA, gnd, bleeder);              // 150K bleeder across main bank
        c.addResistor (ch.sB, ch.sC, 2.7e3);              // R502
        c.addCapacitor (ch.sC, gnd, 30.0e-6);
        c.addResistor (ch.sC, ch.sD, 22.0e3);             // R252
        c.addCapacitor (ch.sD, gnd, 30.0e-6);
        c.addResistor (ch.sD, ch.sE, 15.0e3);             // R261
        c.addCapacitor (ch.sE, gnd, 30.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.16);        // power tubes plate current
        ch.iB = c.addCurrentSource (ch.sB, -0.012);       // power tubes screen current
        ch.iC = c.addCurrentSource (ch.sC, -0.003);       // PI current draw
        ch.iD = c.addCurrentSource (ch.sD, -0.003);       // V2A/V2B/V3A preamp current
        ch.iE = c.addCurrentSource (ch.sE, -0.001);       // V1A preamp current
        for (auto n : { ch.sA, nl, ch.sB })
            c.setInitialGuess (n, railPlatesNominal);
        c.setInitialGuess (ch.sC, 422.0);
        c.setInitialGuess (ch.sD, 406.0);
        c.setInitialGuess (ch.sE, 402.0);
    }

    // ================================================================ preamp (two-pass for cathode follower DC offset)
    {
        auto probe = buildPreamp (ch.pre, 0.0, 402.0, 406.0, 422.0);
        ch.pre.prepare (48000.0);
        const double plate = ch.pre.voltage (probe.plateV3a);
        const double drop = plate - cathodeFollowerDc (422.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop, 402.0, 406.0, 422.0);
        ch.pSrcE = b.srcE;
        ch.pSrcV2 = b.srcV2;
        ch.pSrcV3 = b.srcV3;
        ch.pSrcIn = b.srcIn;
        ch.rRdGainTop = b.rRdGainTop;
        ch.rRdGainBot = b.rRdGainBot;
        ch.rOrGainTop = b.rOrGainTop;
        ch.rOrGainBot = b.rOrGainBot;
        ch.ldr[ldr1] = b.ldr1;
        ch.ldr[ldr2] = b.ldr2;
        ch.ldr[ldr3a] = b.ldr3a;
        ch.ldr[ldr3b] = b.ldr3b;
        ch.ldr[ldr4] = b.ldr4;
        ch.ldr[ldr5] = b.ldr5;
        ch.ldr[ldr6] = b.ldr6;
        ch.ldr[ldr10] = b.ldr10;
        ch.pPlateV1a = b.plateV1a;
        ch.pPlateV2a = b.plateV2a;
        ch.pPlateV2b = b.plateV2b;
        ch.pPlateV3a = b.plateV3a;
        ch.pFollower = b.follower;
    }

    // ================================================================ the two tone stacks + masters (always built and solved)
    // V3B -> LDR8 -> RED stack / LDR9 -> ORANGE stack, R215 1M between their inputs. Each a TMB with its own Treble 250K,
    // Bass 1M, Mid 25K (wiper on the second .02) and 47K slope; RED: C4 680p treble cap, its Presence (R254 22K + C8
    // .003 + 25K rheostat) shunting the treble wiper; ORANGE: C5 500p, R263 82K || C10 82p from the treble wiper to
    // LDR16 (the Master bypass) and the R2E3 22K + C7 .003 + R354 10K treble rolloff (LDR7 shorts the 10K). Each 1M
    // Master's wiper reaches the PI bus through LDR14 / LDR15.
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode(), f = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);
        c.addResistor (cf, f, cathodeFollowerImpedance);
        ch.wToneIn = f;
        ch.wTone = c.addNode();
        const auto bus = ch.wTone;

        const auto ri = c.addNode(), oi = c.addNode();
        ch.ldr[ldr8] = c.addResistor (f, ri, ldrOff);
        ch.ldr[ldr9] = c.addResistor (f, oi, ldrOff);
        c.addResistor (ri, oi, 1.0e6);                     // R215

        struct Stack { int trebleTop, trebleBottom, bass, midTop, midBottom, masterTop, masterBottom; NodalCircuit::Node wiper, master; };
        const auto stack = [&] (NodalCircuit::Node in, double trebleCap)
        {
            Stack st {};
            const auto top = c.addNode(), slope = c.addNode(), bassTop = c.addNode(), midTop = c.addNode(), midWiper = c.addNode();
            st.wiper = c.addNode();
            st.master = c.addNode();
            c.addCapacitor (in, top, trebleCap);
            c.addResistor (in, slope, 47.0e3);                     // R273 / R274
            st.trebleTop = c.addResistor (top, st.wiper, 125.0e3);  // TRBL 250K
            st.trebleBottom = c.addResistor (st.wiper, bassTop, 125.0e3);
            c.addCapacitor (slope, bassTop, 0.02e-6);              // C23 / C25
            st.bass = c.addResistor (bassTop, midTop, 500.0e3);    // BASS 1M rheostat
            st.midTop = c.addResistor (midTop, midWiper, 12.5e3);  // MID 25K
            st.midBottom = c.addResistor (midWiper, gnd, 12.5e3);
            c.addCapacitor (slope, midWiper, 0.02e-6);             // C24 / C26
            st.masterTop = c.addResistor (st.wiper, st.master, 0.5e6); // MSTR 1M
            st.masterBottom = c.addResistor (st.master, gnd, 0.5e6);
            return st;
        };
        const auto red = stack (ri, 680.0e-12);
        const auto orange = stack (oi, 500.0e-12);
        ch.rRdTrebleTop = red.trebleTop;       ch.rOrTrebleTop = orange.trebleTop;
        ch.rRdTrebleBottom = red.trebleBottom; ch.rOrTrebleBottom = orange.trebleBottom;
        ch.rRdBass = red.bass;                 ch.rOrBass = orange.bass;
        ch.rRdMidTop = red.midTop;             ch.rOrMidTop = orange.midTop;
        ch.rRdMidBottom = red.midBottom;       ch.rOrMidBottom = orange.midBottom;
        ch.rRdMasterTop = red.masterTop;       ch.rOrMasterTop = orange.masterTop;
        ch.rRdMasterBottom = red.masterBottom; ch.rOrMasterBottom = orange.masterBottom;

        {
            const auto p1 = c.addNode(), p2 = c.addNode();
            c.addResistor (red.wiper, p1, 22.0e3);                 // R254
            c.addCapacitor (p1, p2, 0.003e-6);                     // C8
            ch.rRdPresence = c.addResistor (p2, gnd, 7.5e3);       // RED PRSNC 25K rheostat
        }
        {
            const auto n = c.addNode(), q1 = c.addNode(), q2 = c.addNode();
            c.addResistor (orange.wiper, n, 82.0e3);               // R263
            c.addCapacitor (orange.wiper, n, 82.0e-12);            // C10
            ch.ldr[ldr16] = c.addResistor (n, orange.master, ldrOff);
            c.addResistor (orange.wiper, q1, 22.0e3);              // R2E3
            c.addCapacitor (q1, q2, 0.003e-6);                     // C7
            c.addResistor (q2, gnd, 10.0e3);                       // R354
            ch.ldr[ldr7] = c.addResistor (q2, gnd, ldrOff);
        }
        ch.ldr[ldr14] = c.addResistor (red.master, bus, ldrOff);
        ch.ldr[ldr15] = c.addResistor (orange.master, bus, ldrOff);
        c.addResistor (bus, gnd, 10.0e6); // numerical DC reference only: the bus otherwise sees the dark LDRs and the PI cap
    }

    // ================================================================ phase inverter, power amp (full reference only)
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto vpi = c.addNode(), ct = c.addNode(), vc18 = c.addNode();
        ch.wSrcPi = c.addSource (vpi, 422.0);
        ch.wSrcCt = c.addSource (ct, railPlatesNominal);
        ch.wSrcBias = c.addSource (vc18, biasSupplyVolts);

        // Phase inverter (power-amp sheet): 12AX7 long-tailed pair. C28 .02 from the bus to V5B; both grids biased from
        // the tail node T through 1M (R214 / R213); cathodes -> R341 470 -> T -> R353 10K -> J -> R372 4.7K -> ground.
        // V5A's grid is AC-grounded to J by C40 .1, and J is where the NFB lands -- the loop drives the pair through the
        // bottom of its tail. V5B 82K || C1 120p, V5A 90K || C2 120p plates (C rail), C3 75p plate to plate.
        const auto g1 = c.addNode(), g2 = c.addNode(), pa = c.addNode(), pb = c.addNode(), k = c.addNode(), nt = c.addNode(), fp = c.addNode();
        ch.wGridA = g1;
        ch.wPlateA = pa;
        ch.wPlateB = pb;
        ch.wTail = nt;
        ch.wFeedback = fp;
        c.addCapacitor (ch.wTone, g1, 0.02e-6);             // C28
        c.addResistor (g1, nt, 1.0e6);                      // R214
        c.addResistor (g2, nt, 1.0e6);                      // R213
        c.addCapacitor (g2, fp, 0.1e-6);                    // C40
        c.addTriode (pa, g1, k, triode12AX7Pi());
        c.addTriode (pb, g2, k, triode12AX7Pi());
        c.addCapacitor (g1, pa, cgp);
        c.addCapacitor (g2, pb, cgp);
        c.addResistor (vpi, pa, 82.0e3);                    // R281
        c.addCapacitor (vpi, pa, 120.0e-12);                // C1
        c.addResistor (vpi, pb, 90.0e3);                    // R104
        c.addCapacitor (vpi, pb, 120.0e-12);                // C2
        c.addCapacitor (pa, pb, 75.0e-12);                  // C3
        c.addResistor (k, nt, 470.0);                       // R341
        c.addResistor (nt, fp, 10.0e3);                     // R353
        c.addResistor (fp, gnd, 4.7e3);                     // R372
        c.setInitialGuess (pa, 280.0);
        c.setInitialGuess (pb, 280.0);
        c.setInitialGuess (k, 48.0);
        c.setInitialGuess (nt, 47.0);
        c.setInitialGuess (g1, 30.0);
        c.setInitialGuess (g2, 30.0);
        c.setInitialGuess (fp, 14.0);

        // Power amplifier: 4 × 6L6GC as two push-pull pairs. .047µF couplings, 1.5K grid stoppers (3K/pair),
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
        c.addResistor (g3, nb, 220.0e3);                    // R223
        c.addResistor (g4, nb, 220.0e3);                    // R222
        c.addResistor (g3, g3s, 1.5e3);                     // grid stoppers (3K/pair = 1.5K each)
        c.addResistor (g4, g4s, 1.5e3);
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

        // NFB (sheet): the 8-16 ohm jack -> C51 .1 -> the LDR19 / LDR20 legs (rFeedback, set in updateLdrs) -> J. The
        // ORANGE Presence (C52 .1 + 25K rheostat) shunts J -- the drawing puts the Orange Presence here and the Red one in
        // its preamp stack, so a Vintage mode borrowed by the Red channel has both (manual).
        {
            const auto fc = c.addNode(), wp = c.addNode();
            c.addCapacitor (ch.wOut, fc, 0.1e-6);           // C51
            ch.rFeedback = c.addResistor (fc, fp, feedbackResistor);
            c.addCapacitor (fp, wp, 0.1e-6);                // C52
            ch.rPresence = c.addResistor (wp, gnd, 25.0e3); // ORANGE PRSNC 25K rheostat
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

void DualRectifierStyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    const double trebleBottom = juce::jmax (1.0, 250.0e3 * k.treble);
    const double trebleTop = juce::jmax (1.0, 250.0e3 - trebleBottom);
    const double bassR = juce::jmax (1.0, 1.0e6 * pots::audio (k.bass));
    const double midBottom = juce::jmax (1.0, 25.0e3 * k.mid);
    const double midTop = juce::jmax (1.0, 25.0e3 - midBottom);
    const double masterBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.master));
    const double masterTop = juce::jmax (1.0, 1.0e6 - masterBottom);
    const double gainBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.gain));
    const double gainTop = juce::jmax (1.0, 1.0e6 - gainBottom);
    if (k.channel == 0)
        orangePresence = k.presence;
    // The Orange Presence lives in the NFB loop: it moves with the Orange knob even while Red plays (Red to Vintage).
    // More Presence = less of the rheostat in series with C52 = more treble shunted out of the loop = brighter.
    const double presR = juce::jmax (1.0, 25.0e3 * (1.0 - orangePresence));
    const double trim = juce::jmax (1.0, 220.0e3 * k.bias);

    // RECTIFIER SELECT: the 5U4 pair drops far more and sags deeper than the diodes. POWER Spongy: the lower primary tap,
    // every rail down ~10% (ESTIMATED -- the drawing names the taps, not their voltages).
    const bool tubeRectifier = rectifierParam != nullptr && rectifierParam->get() < 0.5f;
    const bool spongy = spongyParam != nullptr && spongyParam->get() >= 0.5f;
    rectifierScale = tubeRectifier ? 5.0 : 1.0;
    rectifierSag = tubeRectifier ? 1.7 : 1.0;
    railNow = railPlatesNominal * (tubeRectifier ? 0.94 : 1.0) * (spongy ? spongyRailScale : 1.0);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel) * rectifierScale;
    const bool el34 = tubesParam != nullptr && tubesParam->get() >= 0.5f;

    for (auto& ch : channels)
    {
        if (k.channel == 0)
        {
            ch.pre.setResistance (ch.rOrGainBot, gainBottom);
            ch.pre.setResistance (ch.rOrGainTop, gainTop);
            ch.power.setResistance (ch.rOrTrebleTop, trebleTop);
            ch.power.setResistance (ch.rOrTrebleBottom, trebleBottom);
            ch.power.setResistance (ch.rOrBass, bassR);
            ch.power.setResistance (ch.rOrMidTop, midTop);
            ch.power.setResistance (ch.rOrMidBottom, midBottom);
            ch.power.setResistance (ch.rOrMasterTop, masterTop);
            ch.power.setResistance (ch.rOrMasterBottom, masterBottom);
        }
        else
        {
            ch.pre.setResistance (ch.rRdGainBot, gainBottom);
            ch.pre.setResistance (ch.rRdGainTop, gainTop);
            ch.power.setResistance (ch.rRdTrebleTop, trebleTop);
            ch.power.setResistance (ch.rRdTrebleBottom, trebleBottom);
            ch.power.setResistance (ch.rRdBass, bassR);
            ch.power.setResistance (ch.rRdMidTop, midTop);
            ch.power.setResistance (ch.rRdMidBottom, midBottom);
            ch.power.setResistance (ch.rRdMasterTop, masterTop);
            ch.power.setResistance (ch.rRdMasterBottom, masterBottom);
            ch.power.setResistance (ch.rRdPresence, juce::jmax (1.0, 25.0e3 * k.presence)); // more = less treble shunted
        }
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rPresence, presR);
            ch.power.setResistance (ch.rBiasTrim, trim);
            if (el34 != appliedEL34)
            {
                ch.power.setPentodeModel (ch.penA, el34 ? modelEL34 : model6L6);
                ch.power.setPentodeModel (ch.penB, el34 ? modelEL34 : model6L6);
                ch.power.setSource (ch.wSrcBias, el34 ? biasSupplyVoltsEL34 : biasSupplyVolts);
            }
        }
        if (! resistiveLoadForced && k.speaker != appliedSpeaker)
            applySpeaker (ch, k.speaker);
        ch.supply.setResistance (ch.rRect, rectifier);
        ch.supply.setSource (ch.srcVoc, railNow + rectifier * idleSupplyCurrent);
    }
    appliedEL34 = el34;
    appliedEL34Reduced = el34;
    appliedSpeaker = k.speaker;
    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

/** The factory LDR truth table (switching-matrix sheet, docs/circuits/DualRectifier.md), built from the buses the
    sheet draws: the ORANGE bus lights 2, 6, 9, 15 and -- unless the rear Orange Gain switch is on Clean (relay RY1
    lifts them) -- 3, 4, 10; Clean adds 16 and 20. The RED bus lights 1, 3, 4, 5, 8, 10, 14. The Vintage voicing (7
    treble rolloff, 19 feedback) rides on Orange unless cloned "Orange to Modern", and joins Red when cloned "Red to
    Vintage". Each cell then fades toward its target in the log domain. */
void DualRectifierStyleAmplifierProcessor::updateLdrs (bool snap)
{
    const int channel = juce::roundToInt (channelParam->get());
    const bool clean = orangeGainParam->get() < 0.5f;
    const int style = juce::roundToInt (styleParam->get());
    std::array<bool, numLdrs> lit {};
    if (channel == 0)
    {
        for (auto l : { ldr2, ldr6, ldr9, ldr15 })
            lit[l] = true;
        for (auto l : { ldr3a, ldr3b, ldr4, ldr10 })
            lit[l] = ! clean;
        lit[ldr16] = lit[ldr20] = clean;
        lit[ldr7] = lit[ldr19] = style != 1;
    }
    else
    {
        for (auto l : { ldr1, ldr3a, ldr3b, ldr4, ldr5, ldr8, ldr10, ldr14 })
            lit[l] = true;
        lit[ldr7] = lit[ldr19] = style == 2;
    }

    const double logOn = std::log (ldrOn), logOff = std::log (ldrOff);
    const double step = (logOff - logOn) * (double) controlInterval / (ldrFadeSeconds * sampleRate);
    bool nfbMoved = false;
    for (int l = 0; l < numLdrs; ++l)
    {
        const double target = lit[(size_t) l] ? logOn : logOff;
        double& now = ldrLog[(size_t) l];
        if (! snap && juce::exactlyEqual (now, target))
            continue;
        now = snap ? target : (target > now ? juce::jmin (target, now + step) : juce::jmax (target, now - step));
        const double r = std::exp (now);
        if (l == ldr19 || l == ldr20)
        {
            nfbMoved = true;
            continue;
        }
        for (auto& ch : channels)
        {
            const bool inPre = l == ldr1 || l == ldr2 || l == ldr3a || l == ldr3b || l == ldr4 || l == ldr5 || l == ldr6 || l == ldr10;
            (inPre ? ch.pre : ch.power).setResistance (ch.ldr[(size_t) l], r);
        }
    }
    if (nfbMoved || snap)
    {
        // R19 in series with R276 || (R276 + R20).
        const double r19 = std::exp (ldrLog[ldr19]), r20 = std::exp (ldrLog[ldr20]);
        const double leg = r19 + nfbLeg * (nfbLeg + r20) / (2.0 * nfbLeg + r20);
        // reducedOrder: 0 = no loop (Modern), 1 = the Vintage loop, 2 = Clean's doubled one, smooth in between.
        nfbAmount = nfbLeg / leg;
        const double feel = 1.0 + 1.5 * (1.0 - lastKnobs.tubeFeel); // Tube Feel's old NFB lean, kept
        if (! reducedOrder)
            for (auto& ch : channels)
                ch.power.setResistance (ch.rFeedback, feedbackOverride > 0.0 ? feedbackOverride : leg / feel);
    }
}

void DualRectifierStyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void DualRectifierStyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void DualRectifierStyleAmplifierProcessor::recover (Channel& ch) const
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

void DualRectifierStyleAmplifierProcessor::updateSupply (Channel& ch) const
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
    ch.pre.setSource (ch.pSrcV3, rail (ch.sC, 480.0));   // V3A from PI rail (C)
    ch.pre.setSource (ch.pSrcV2, rail (ch.sD, 480.0));   // V2A/V2B from D
    ch.pre.setSource (ch.pSrcE, rail (ch.sE, 480.0));    // V1A from E
    ch.vScreen = rail (ch.sB, 560.0);
}

double DualRectifierStyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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
    // Spongy drops the idle rail to railNow (tube rectifier's lower B+); the sag depth scales
    // with it so Bold keeps byte-identical behavior when railNow == bmSagRail[0].
    const double railScale = railNow / bmSagRail[0];
    ch.bmRail = railNow - feel * rectifierSag * railScale * (bmSagRail[0] - sagRailLookup (ch.bmEnvelope));

    // Loop / tube state: dB into and out of the stage, linear in nfbAmount between Modern (0), Vintage (1), Clean (2).
    const double n = juce::jlimit (0.0, 2.0, nfbAmount);
    const bool el34 = appliedEL34Reduced;
    const double inDb = (n <= 1.0 ? n * fit.vinDb : fit.vinDb + (n - 1.0) * (fit.cinDb - fit.vinDb)) + (el34 ? fit.elInDb : 0.0);
    const double outDb = (n <= 1.0 ? n * fit.voutDb : fit.voutDb + (n - 1.0) * (fit.coutDb - fit.voutDb)) + (el34 ? fit.elOutDb : 0.0);
    toneVoltage *= std::pow (10.0, inDb / 20.0);
    const double k = ch.bmRail * fit.ymax / fit.g0;
    const double shift = 0.6 * (bias - 0.5) * k;
    const auto sat = [k, rail = ch.bmRail, ymax = fit.ymax, knee = fit.knee] (double xx)
    {
        const double ux = std::abs (xx) / juce::jmax (1.0e-9, k);
        const double yx = ymax * ux / std::pow (1.0 + std::pow (ux, knee), 1.0 / knee);
        return std::copysign (yx * rail, xx);
    };
    const double raw = sat (toneVoltage + shift) - sat (shift);

    // The frequency response the removed stages used to provide: two fixed biquads fitted to the reference
    // netlist's own measured transfer (resonant high-pass LF bump + zero/pole top section).
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
    // The Orange Presence is an NFB-loop control: it fades out with the loop (Modern has none). The Red one is in the
    // preamp netlist, which both paths solve.
    const double presGain = 0.64 + 1.2 * juce::jlimit (0.0, 1.0, orangePresence);
    const double presShelf = ch.bmPresState + (1.0 + juce::jlimit (0.0, 1.0, nfbAmount) * (presGain - 1.0)) * (top - ch.bmPresState);
    const double cut = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * fit.cutHz / juce::jmax (1.0, sampleRate));
    ch.bmCutState += cut * (presShelf - ch.bmCutState);
    ch.bmOutput = juce::jlimit (-bmOutMax, bmOutMax, ch.bmTail.process ((presShelf - ch.bmCutState) * fit.trim * std::pow (10.0, outDb / 20.0)));
    return ch.bmOutput;
}

void DualRectifierStyleAmplifierProcessor::designPowerFilters()
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
    const double wb = 2.0 * juce::MathConstants<double>::pi * fit.bumpHz;
    // resonant high-pass: peaks +20log10(Qp) dB at bmBumpHz, -12 dB/oct below, unity above
    bilinear (1.0 / (wb * wb), 0.0, 0.0,
              1.0 / (wb * wb), 1.0 / (fit.bumpQp * wb), 1.0,
              sampleRate, bmAB0, bmAB1, bmAB2, bmAA1, bmAA2);
    const double wz = 2.0 * juce::MathConstants<double>::pi * fit.topZ, wp = 2.0 * juce::MathConstants<double>::pi * fit.topP;
    bilinear (0.0, 1.0 / wz, 1.0,
              1.0 / (wp * wp), 1.0 / (fit.topQp * wp), 1.0,
              sampleRate, bmBB0, bmBB1, bmBB2, bmBA1, bmBA2);
}

double DualRectifierStyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::v1aPlate: return ch.pre.voltage (ch.pPlateV1a);
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

double DualRectifierStyleAmplifierProcessor::debugIterations (int block) const noexcept
{
    return block == 0 ? channels[0].pre.averageIterations() : channels[0].power.averageIterations();
}

int DualRectifierStyleAmplifierProcessor::debugLastPowerIterations() const noexcept { return channels[0].power.lastIterations(); }

void DualRectifierStyleAmplifierProcessor::debugSetFeedbackResistance (double ohms)
{
    feedbackOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rFeedback, ohms);
}

double DualRectifierStyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

double DualRectifierStyleAmplifierProcessor::screenCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return sa + sb;
}

void DualRectifierStyleAmplifierProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        ch.bmTail.setCabChoices (cabParam, cabAlts);
        ch.bmTail.prepare (sampleRate, voicings::highGain, 1.0 / outputScale);
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
    appliedEL34 = false; // buildChannel() wires 6L6s
    model6L6.setParameters (pentode6L6PairPower());
    modelEL34.setParameters (pentodeEL34Pair());
    // Both channels' pots from their stored rows, then the visible channel's live knobs on top.
    const int visible = juce::roundToInt (channelParam->get());
    for (int slot : { 1 - visible, visible })
    {
        const auto v = [&] (size_t knob) { return (double) channelMemory->slotValue (slot, knob); };
        updatePots ({ v (0), v (1), v (2), v (3), v (4), v (5), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                      juce::roundToInt (speakerParam->get()), slot });
    }
    orangePresence = channelMemory->slotValue (0, 4);
    updatePots (lastKnobs);
    updateLdrs (true);

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

            ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sC));
            ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
            ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
            passOk = ch.pre.prepare (newSampleRate) && passOk;
            ch.followerDc = ch.pre.voltage (ch.pFollower);

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
                iPi = (vPi - ch.power.voltage (ch.wPlateA)) / 82.0e3 + (vPi - ch.power.voltage (ch.wPlateB)) / 90.0e3;
            }
            const double vV3 = ch.supply.voltage (ch.sC);
            const double iV3 = (vV3 - ch.pre.voltage (ch.pPlateV3a)) / 220.0e3 + ch.followerDc / 100.0e3;
            const double vV2 = ch.supply.voltage (ch.sD);
            const double iV2 = (vV2 - ch.pre.voltage (ch.pPlateV2a)) / 100.0e3
                             + (vV2 - ch.pre.voltage (ch.pPlateV2b)) / 100.0e3;
            const double vV1 = ch.supply.voltage (ch.sE);
            const double iV1 = (vV1 - ch.pre.voltage (ch.pPlateV1a)) / 220.0e3;
            ipRun += 0.5 * ((ipA + ipB) - ipRun);
            isRun += 0.5 * ((isA + isB) - isRun);
            iPiRun += 0.5 * (iPi - iPiRun);
            iV3Run += 0.5 * (iV3 - iV3Run);
            iV2Run += 0.5 * (iV2 - iV2Run);
            iV1Run += 0.5 * (iV1 - iV1Run);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            ch.supply.setCurrentSource (ch.iB, -isRun);
            ch.supply.setCurrentSource (ch.iC, -iPiRun);
            ch.supply.setCurrentSource (ch.iD, -(iV3Run + iV2Run));
            ch.supply.setCurrentSource (ch.iE, -iV1Run);
            idleSupplyCurrent = ipRun + isRun + iPiRun + iV3Run + iV2Run + iV1Run + (ch.vScreen / bleeder);
            ch.supply.setSource (ch.srcVoc, railNow + rectifierResistance * feel * rectifierScale * idleSupplyCurrent);
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
        ch.pre.setSource (ch.pSrcV3, ch.supply.voltage (ch.sC));
        ch.pre.setSource (ch.pSrcV2, ch.supply.voltage (ch.sD));
        ch.pre.setSource (ch.pSrcE, ch.supply.voltage (ch.sE));
        ch.pre.saveDynamicState (ch.preRest);
        ch.power.saveDynamicState (ch.powerRest);
        ch.supply.saveDynamicState (ch.supplyRest);
        ch.failStreak = 0;
        ch.bmRail = railPlatesNominal;
        ch.bmEnvelope = 0.0;
        ch.bmOutput = 0.0;
        ch.bmOutput = 0.0;
        ch.bmAX1 = ch.bmAX2 = ch.bmAY1 = ch.bmAY2 = 0.0;
        ch.bmBX1 = ch.bmBX2 = ch.bmBY1 = ch.bmBY2 = 0.0;
        ch.bmCutState = 0.0;
        ch.bmPresState = 0.0;
        ch.piCoupling.prepare (newSampleRate, 0.02e-6, 1.0e6, ch.power.voltage (ch.wTone)); // C28 into R214
    }
    designPowerFilters();
    updatePots (lastKnobs);

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void DualRectifierStyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
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

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ gn, tr, mi, ba, pr, ms, pw, bi, fe, speakerChoice, juce::roundToInt (channelParam->get()) });
            updateLdrs (false);
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));

        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);
        // Per-mode level match (user request 2026-11-02: a mode change must not jump in level), eased over ~10 ms.
        const double chanTrimTarget = std::pow (10.0, modeLevelMatchDb[(size_t) currentMode()] / 20.0);
        const double chanTrimAlpha = 1.0 - std::exp (-1.0 / (0.01 * sampleRate));

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit ((double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            ch.power.setSource (ch.wSrcCf, ch.followerDc + masterGain * cathodeFollowerGain * (ch.pre.voltage (ch.pFollower) - ch.followerDc));
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

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, ch.piCoupling.process (ch.power.voltage (ch.wTone))) : ch.power.voltage (ch.wOut);
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
                lastSampleOk = ok;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void DualRectifierStyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::amp_svg, IconData::amp_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
