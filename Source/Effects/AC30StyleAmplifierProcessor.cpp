#include "AC30StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/AC30TopBoost.md, "Power supply"). 5AR4 rectifier, 20H/100mA choke (both printed);
    // the main B+ voltage itself is NOT printed on this sheet -- 400 V assumed, a commonly-published AC30 Top Boost
    // figure. The preamp's own decoupled tap (290 V) IS printed. ----
    constexpr double railPlatesNominal = 400.0;
    constexpr double rectifierResistance = 130.0; // assumed: a 5AR4 is lower-drop than an EZ81
    constexpr double chokeResistance = 120.0;     // assumed (not printed)
    constexpr double chokeInductance = 20.0;      // H, printed

    // ---- reduced-order power stage: calibration data, A30_POWERCAL in the test file ----
    constexpr int bmSagPoints = 14;
    constexpr double bmSagDrive[bmSagPoints] = { 0.02, 0.05, 0.1, 0.2, 0.4, 0.7, 1.1, 1.6, 2.2, 3.0, 4.0, 5.2, 6.6, 8.2 };
    constexpr double bmSagRail[bmSagPoints]  = { 400.0, 400.0, 400.0, 399.8, 399.4, 398.6, 397.2, 395.0, 391.8, 387.2, 380.7, 372.0, 361.0, 348.0 };

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
    KorenTriode::Parameters triode12AX7() { return {}; } // Koren's ECC83 set

    /** The LTP's own 12AX7, `kg1` softened -- see docs/circuits/AC30TopBoost.md's "A small power-stage instability"
        section. With silence at the input, the shared-cathode-bias node coupling the two output phases (all four
        EL84s tied to ONE cathode resistor, confirmed on the drawing) let the LTP settle into a slow, low-amplitude
        flip-flop between two nearly-symmetric plate states instead of a single stable point. Freezing the supply's
        own sag loop did NOT remove it, ruling that out. Softening EITHER tube ALONE needed a much larger multiple to
        reach genuine stability, and on the OUTPUT pentode alone that visibly flattened the reference model's own
        driven-signal response into a near-dead-zone (output barely changed across two decades of input level) well
        before reaching stability -- the same "fix crushes the curve" failure this project's other kg1
        investigations found. Splitting a smaller multiple across BOTH tubes (6x each) reaches the same genuine 10 s
        stability with much less damage to either one's own transfer curve -- same lesson as the JCM800's own
        power-stage investigation. */
    KorenTriode::Parameters triode12AX7Pi()
    {
        auto p = triode12AX7();
        p.kg1 *= 6.0;
        return p;
    }

    /** EL84 (6BQ5), same published Koren SPICE fit as the AC15's own (mu 16, Kg1 570, Kg2 4200, Kp 50, Kvb 24,
        Ex 1.35) with `kg1` softened -- see triode12AX7Pi()'s own comment for the full story. A PAIR (two tubes in
        parallel on the same nodes -- confirmed on the drawing: this amp uses FOUR EL84s as two parallel pairs, not
        two singles) is one tube with twice the current (kg1/kg2 halved, grid conductance doubled, arc resistance
        halved) -- same trick as the Marshall amps' own EL34 pairs. */
    KorenPentode::Parameters pentodeEL84Pair()
    {
        KorenPentode::Parameters p;
        p.mu = 16.0;
        p.ex = 1.35;
        p.kg1 = 570.0 / 2.0 * 6.0;
        p.kg2 = 4200.0 / 2.0;
        p.kp = 50.0;
        p.kvb = 24.0;
        p.grid.Gg *= 2.0;
        p.arcResistance /= 2.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7 section

    // Output transformer: the real amp's own three secondary taps are printed directly (15 / 8 / 0 ohm) -- this
    // project's own 4/8/16 Speaker convention rounds 15 to 16. Plate-to-plate impedance/inductance are assumed (not
    // printed): four EL84s (two parallel pairs) want roughly half the single-pair AC15's own impedance.
    constexpr double primaryHalfInductance = 4.0;         // H per half (16 H plate to plate)
    constexpr double halfToSecondaryTurns = 7.906;        // (4000/16)^0.5 / 2
    constexpr double couplingHalves = 0.999;
    constexpr double couplingSecondary = 0.998;
    constexpr double primaryHalfResistance = 40.0;
    constexpr double secondaryResistance = 0.15;

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // the real amp's own ~15 ohm tap, rounded to this project's 16 ohm

    constexpr double powerTheta = 0.9;
}

AC30StyleAmplifierProcessor::AC30StyleAmplifierProcessor()
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "a30_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("High") : juce::String ("Low"); }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("a30_volume", "Volume", 0.4f);
    auto treble = make ("a30_treble", "Treble", 0.5f);
    auto bass = make ("a30_bass", "Bass", 0.5f);
    auto cut = make ("a30_cut", "Cut", 0.5f);
    auto output = make ("a30_output", "Output", 0.5f);
    auto power = make ("a30_power", "Power Drive", 1.0f);
    auto bias = make ("a30_bias", "Bias", 0.5f);
    auto feel = make ("a30_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "a30_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    volumeParam = volume.get();
    trebleParam = treble.get();
    bassParam = bass.get();
    cutParam = cut.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ac30", "AC30-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (volume));
    group->addChild (std::move (treble));
    group->addChild (std::move (bass));
    group->addChild (std::move (cut));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ac30_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

namespace
{
    struct PreampBuild
    {
        int srcIn = 0, srcRail = 0, rVolTop = 0, rVolBot = 0;
        NodalCircuit::Node plate = 0, volOut = 0, follower = 0;
    };

    /** V1a (input gain stage) -> Volume -> V2a (second gain stage) -> V2b (cathode follower, DIRECT-COUPLED from
        V2a's own plate -- a plain wire on the real drawing, no capacitor). `followerDrop` is V2b's real DC drop
        (computed by the caller from a first pass with drop=0, then rebuilt -- same two-pass pattern the JCM800's
        own follower uses). */
    PreampBuild buildPreamp (NodalCircuit& c, double followerDrop)
    {
        const auto gnd = NodalCircuit::ground;
        PreampBuild b;
        const auto in = c.addNode();
        b.srcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        b.srcRail = c.addSource (rail, railPlatesNominal * 0.725); // the printed +290 V preamp tap, ~0.725 of 400 V

        // V1a: 68k grid stopper (both High/Low jacks share this one grid), 1k5 cathode + 25 uF bypass, 220k plate.
        const auto g1 = c.addNode(), k1 = c.addNode();
        b.plate = c.addNode();
        c.addResistor (in, g1, 68.0e3);
        c.addResistor (g1, gnd, 1.0e6);
        c.addTriode (b.plate, g1, k1, triode12AX7());
        c.addResistor (k1, gnd, 1.5e3);
        c.addCapacitor (k1, gnd, 25.0e-6);
        c.addResistor (rail, b.plate, 220.0e3);
        c.setInitialGuess (b.plate, 250.0);
        c.setInitialGuess (k1, 1.2);

        // Coupling into Volume (500k log): 500 pF then 100 pF to ground (confirmed on the drawing).
        const auto volIn = c.addNode();
        c.addCapacitor (b.plate, volIn, 500.0e-12);
        c.addCapacitor (volIn, gnd, 100.0e-12);
        b.volOut = c.addNode();
        b.rVolTop = c.addResistor (volIn, b.volOut, 500.0e3);
        b.rVolBot = c.addResistor (b.volOut, gnd, 500.0e3);

        // V2a: 100k plate, 1k5 cathode + 25 uF bypass -- DIRECT-COUPLED (a plain wire, confirmed on the drawing, no
        // capacitor) into V2b's grid.
        const auto g2 = c.addNode(), k2 = c.addNode();
        const auto plate2a = c.addNode();
        c.addTriode (plate2a, g2, k2, triode12AX7());
        c.addResistor (b.volOut, g2, 1.0e6); // the Volume pot's own load into V2a's grid
        c.addResistor (rail, plate2a, 100.0e3);
        c.addResistor (k2, gnd, 1.5e3);
        c.addCapacitor (k2, gnd, 25.0e-6);
        c.setInitialGuess (plate2a, 200.0);
        c.setInitialGuess (k2, 1.2);

        // V2b: cathode follower, direct-coupled from V2a's own plate (an ideal follower here, with the DC drop of
        // the real one) -- 56k cathode, unbypassed, confirmed on the drawing.
        b.follower = c.addNode();
        c.addFollower (plate2a, b.follower, followerDrop);
        return b;
    }
}

void AC30StyleAmplifierProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;

    // ================================================================ supply
    {
        auto& c = ch.supply;
        c.setIntegrationTheta (0.5);
        const auto vo = c.addNode();
        ch.sA = c.addNode();
        const auto nl = c.addNode();
        ch.sB = c.addNode();

        ch.srcVoc = c.addSource (vo, railPlatesNominal);
        ch.rRect = c.addResistor (vo, ch.sA, rectifierResistance);
        c.addCapacitor (ch.sA, gnd, 16.0e-6);
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 16.0e-6);

        ch.iA = c.addCurrentSource (ch.sA, -0.15);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (nl, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPlatesNominal);
    }

    // ================================================================ preamp: V1a -> Volume -> V2a -> V2b follower
    // The follower's DC drop needs V2a's own DC plate voltage, which doesn't depend on the follower: build once with
    // a placeholder drop, read the plate, solve the follower, build again (same two-pass pattern the JCM800 uses).
    {
        auto probe = buildPreamp (ch.pre, 0.0);
        ch.pre.prepare (48000.0);
        // V2a's own plate is not separately exposed on PreampBuild (only the follower's DC matters downstream), so
        // read it via the follower's own input side voltage before the ideal-follower stamp: the follower's own
        // node IS an ideal copy (minus drop) of V2a's plate, so at drop=0 it reads V2a's plate directly.
        const double plate = ch.pre.voltage (probe.follower);
        const double drop = plate - cathodeFollowerDc (400.0, plate);
        ch.pre = NodalCircuit {};
        auto b = buildPreamp (ch.pre, drop);
        ch.pSrcIn = b.srcIn;
        ch.pSrcRail = b.srcRail;
        ch.pPlate = b.plate;
        ch.pVolOut = b.volOut;
        ch.rVolTop = b.rVolTop;
        ch.rVolBot = b.rVolBot;
        ch.pFollower = b.follower;
    }

    // ================================================================ power: Top Boost tone stack -> LTP -> 2x2 EL84
    // pairs, cathode-biased, no NFB (confirmed absent on the drawing -- no feedback resistor from the OT back into
    // the phase inverter appears anywhere on this circuit)
    {
        auto& c = ch.power;
        c.setIntegrationTheta (powerTheta);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        // Top Boost tone stack (a bridged RC network, not a simple TMB ladder -- confirmed on the drawing):
        // IN -(50pF)-> A -(1M Treble, wiper=out)-> D; IN -(100k)-> B; B -(.022uF)-> D; B -(.022uF)-> E -> Bass
        // pot's wiper; Bass pot (1M) spans D..F; F -(10k)-> ground.
        ch.wToneIn = cf;
        const auto a = c.addNode(), b = c.addNode(), d = c.addNode(), e = c.addNode(), f = c.addNode();
        c.addCapacitor (cf, a, 50.0e-12);
        c.addResistor (cf, b, 100.0e3);
        c.addCapacitor (b, d, 0.022e-6);
        c.addCapacitor (b, e, 0.022e-6);
        ch.wTone = c.addNode();
        ch.rTrebleTop = c.addResistor (a, ch.wTone, 1.0e6);
        ch.rTrebleBottom = c.addResistor (ch.wTone, d, 1.0e6);
        ch.rBass = c.addResistor (d, e, 1.0e6);
        ch.rBassBottom = c.addResistor (e, f, 1.0e6);
        c.addResistor (f, gnd, 10.0e3);

        // Long-tailed-pair phase inverter: V1a driven from the tone stack's own output through 47k + 1M grid leak;
        // V1b's own grid returns to ground through its own 1M leak (no feedback signal reaches it -- this circuit
        // has no global feedback loop at all, confirmed absent on the drawing); shared 1k2 tail, unbypassed.
        const auto rail = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        const auto g1 = c.addNode(), g2 = c.addNode(), tail = c.addNode();
        ch.wPiGridA = g1;
        ch.wPiPlateA = c.addNode();
        ch.wPiPlateB = c.addNode();
        c.addCapacitor (ch.wTone, g1, 0.01e-6);
        c.addResistor (g1, gnd, 1.0e6);
        c.addResistor (g2, gnd, 1.0e6);
        c.addTriode (ch.wPiPlateA, g1, tail, triode12AX7Pi());
        c.addTriode (ch.wPiPlateB, g2, tail, triode12AX7Pi());
        c.addCapacitor (g1, ch.wPiPlateA, cgp);
        c.addCapacitor (g2, ch.wPiPlateB, cgp);
        c.addResistor (rail, ch.wPiPlateA, 100.0e3);
        c.addResistor (rail, ch.wPiPlateB, 100.0e3);
        c.addResistor (tail, gnd, 1.2e3);
        c.setInitialGuess (ch.wPiPlateA, 280.0);
        c.setInitialGuess (ch.wPiPlateB, 280.0);
        c.setInitialGuess (tail, 1.0);

        // Each phase couples through a 0.047 uF cap and a 220k grid leak into its own pair of EL84 grids
        // (confirmed on the drawing).
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlateA, ch.wGridA, 0.047e-6);
        c.addResistor (ch.wGridA, gnd, 220.0e3);
        c.addCapacitor (ch.wPiPlateB, ch.wGridB, 0.047e-6);
        c.addResistor (ch.wGridB, gnd, 220.0e3);

        // Four EL84s as two parallel pairs (one pair per phase) sharing ONE cathode-bias node through their own
        // 100 ohm resistors each (confirmed on the drawing) -- self-biased, no fixed bias supply, matching the AC15.
        ch.wCathodeBias = c.addNode();
        ch.rCathodeBias = c.addResistor (ch.wCathodeBias, gnd, 65.0); // two 100 ohm resistors in parallel per pair, halved again for two pairs sharing one node -- see the doc
        c.addCapacitor (ch.wCathodeBias, gnd, 25.0e-6); // confirmed 25 uF on the drawing

        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        // Screens: all four tied to one shared node through a 50 ohm resistor + 25 uF bypass (confirmed on the
        // drawing) -- modelled as a fixed, plausible screen voltage rather than a literal shared resistor node
        // (the screen isn't a circuit node in this project's pentode model -- see NodalCircuit::addPentode).
        ch.penA = c.addPentode (ch.wPP1, ch.wGridA, ch.wCathodeBias, pentodeEL84Pair(), 300.0);
        ch.penB = c.addPentode (ch.wPP2, ch.wGridB, ch.wCathodeBias, pentodeEL84Pair(), 300.0);
        c.addResistor (rail, ch.wPP1, 50.0); // 1k5 || 1k5 (two per phase, confirmed on the drawing)
        c.addResistor (rail, ch.wPP2, 50.0);
        c.setInitialGuess (ch.wPP1, 340.0);
        c.setInitialGuess (ch.wPP2, 335.0);
        c.setInitialGuess (ch.wCathodeBias, 12.0);

        // Output transformer, no negative feedback: plate-to-plate, 16 ohm tap (the real amp's own printed 15 ohm
        // tap, rounded).
        c.addCapacitor (ch.wPP1, ch.wPP2, 400.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 20.0e3);
        c.addCapacitor (ch.wPP1, gnd, 400.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 400.0e-12);
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance);
        const double lh = primaryHalfInductance;
        const double ls = lh / (halfToSecondaryTurns * halfToSecondaryTurns);
        const double m12 = -couplingHalves * lh;
        const double mps = couplingSecondary * std::sqrt (lh * ls);
        c.addCoupledInductors ({ { a1, ch.wPP1 }, { a2, ch.wPP2 }, { sw, gnd } },
                               { lh,  m12, -mps,
                                 m12, lh,   mps,
                                 -mps, mps, ls });
        ch.wOut = c.addNode();
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
        c.setInitialGuess (a1, railPlatesNominal);
        c.setInitialGuess (a2, railPlatesNominal);
    }
}

void AC30StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Treble: 1M linear (the Top Boost pot is a linear/reverse pot in the real amp), wiper is the stage output --
    // more knob = more of the top (bright, capacitively-coupled) segment reaches the wiper.
    const double trebleTop = juce::jmax (1.0, 1.0e6 * k.treble);
    const double trebleBottom = juce::jmax (1.0, 1.0e6 - trebleTop);
    // Bass: 1M linear, wiper feeds the .022 uF divider node (more knob = more of the top segment, closer to D).
    const double bassTop = juce::jmax (1.0, 1.0e6 * k.bass);
    const double bassBottom = juce::jmax (1.0, 1.0e6 - bassTop);
    // Volume: 500k audio taper.
    const double volBottom = juce::jmax (1.0, 500.0e3 * pots::audio (k.volume));

    const double cathodeR = cathodeResistanceOverride > 0.0 ? cathodeResistanceOverride : 65.0 * (0.7 + 0.6 * k.bias);
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 500.0e3 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rTrebleTop, trebleTop);
            ch.power.setResistance (ch.rTrebleBottom, trebleBottom);
            ch.power.setResistance (ch.rBass, bassTop);
            ch.power.setResistance (ch.rBassBottom, bassBottom);
            ch.power.setResistance (ch.rCathodeBias, cathodeR);
            ch.supply.setResistance (ch.rRect, rectifier);
        }
        applySpeaker (ch, k.speaker);
    }
    if (appliedSpeaker != k.speaker && ! resistiveLoadForced)
        appliedSpeaker = k.speaker;

    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void AC30StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void AC30StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void AC30StyleAmplifierProcessor::debugSetCathodeResistance (double ohms)
{
    cathodeResistanceOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rCathodeBias, ohms);
}

void AC30StyleAmplifierProcessor::recover (Channel& ch) const
{
    ++recoveries;
    ch.pre.restoreDynamicState (ch.preRest);
    ch.power.restoreDynamicState (ch.powerRest);
    ch.supply.restoreDynamicState (ch.supplyRest);
    ch.sumPlate = 0.0;
    ch.sumCount = 0;
    ch.failStreak = 0;
    ch.alignOutput = true;
}

void AC30StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! supplyCurrentFrozen)
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.5, ch.sumPlate / n));
    ch.supply.solveSample();
    ch.sumPlate = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
        ch.power.setSource (ch.wSrcRail, rail (ch.sB, 450.0));
    ch.pre.setSource (ch.pSrcRail, rail (ch.sB, 450.0) * 0.725);
}

double AC30StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double AC30StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::preampPlate: return ch.pre.voltage (ch.pPlate);
        case Probe::followerOut: return ch.pre.voltage (ch.pFollower);
        case Probe::toneOut: return ch.power.voltage (ch.wTone);
        case Probe::piGridA: return ch.power.voltage (ch.wPiGridA);
        case Probe::piPlateA: return ch.power.voltage (ch.wPiPlateA);
        case Probe::piPlateB: return ch.power.voltage (ch.wPiPlateB);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::cathodeBias: return ch.power.voltage (ch.wCathodeBias);
    }
    return 0.0;
}

double AC30StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

void AC30StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedVolume, volumeParam, 0.02);
    setup (smoothedTreble, trebleParam, 0.02);
    setup (smoothedBass, bassParam, 0.02);
    setup (smoothedCut, cutParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.08;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ volumeParam->get(), trebleParam->get(), bassParam->get(), cutParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = 0.08;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * 0.725);
            passOk = ch.pre.prepare (newSampleRate) && passOk;

            double ipA = 0.0, ipB = 0.0, isA = 0.0, isB = 0.0;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcCf, 0.0);
                ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sB));
                passOk = ch.power.prepare (newSampleRate) && passOk;
                ch.power.solveSample();
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
            }
            ipRun += 0.5 * ((ipA + ipB + isA + isB) - ipRun);
            ch.supply.setCurrentSource (ch.iA, -ipRun);
            idleSupplyCurrent = ipRun;
            ch.supply.setSource (ch.srcVoc, railPlatesNominal + rectifierResistance * idleSupplyCurrent);
        }
        dcOk = passOk && ch.supply.prepare (supplyRate) && dcOk;
        if (! reducedOrder)
            ch.power.setSource (ch.wSrcRail, ch.supply.voltage (ch.sB));
        ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * 0.725);
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

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
    cutFilterState = 0.0;
}

void AC30StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTreble.setTargetValue (trebleParam->get());
    smoothedBass.setTargetValue (bassParam->get());
    smoothedCut.setTargetValue (cutParam->get());
    smoothedOutput.setTargetValue (outputParam->get());
    smoothedPower.setTargetValue (powerParam->get());
    smoothedBias.setTargetValue (biasParam->get());
    smoothedFeel.setTargetValue (tubeFeelParam->get());
    const int speakerChoice = juce::roundToInt (speakerParam->get());
    const int inputChoice = juce::roundToInt (inputParam->get());
    const double inputGain = inputChoice == 0 ? 1.0 : 0.25;

    for (int i = 0; i < numSamples; ++i)
    {
        const float vo = smoothedVolume.getNextValue();
        const float tr = smoothedTreble.getNextValue();
        const float ba = smoothedBass.getNextValue();
        const float cu = smoothedCut.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, tr, ba, cu, pw, bi, fe, speakerChoice });
        }

        const double masterGain = juce::jmax (0.002, pots::audio ((double) pw));
        const double outDb = ou < 0.5f ? ((double) ou - 0.5) * 60.0 : ((double) ou - 0.5) * 24.0;
        const double outGain = std::pow (10.0, outDb / 20.0);

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            const double x = std::isfinite (data[i]) ? inputLimit (inputGain * (double) data[i]) : 0.0;
            ch.pre.setSource (ch.pSrcIn, x);
            const bool okPre = ch.pre.solveSample();
            bool ok = okPre;

            bool ok2 = true;
            if (! reducedOrder)
            {
                ch.power.setSource (ch.wSrcCf, masterGain * ch.pre.voltage (ch.pFollower));
                ok2 = ch.power.solveSample();
                double ipA, ipB, isA, isB;
                ch.power.pentodeCurrents (ch.penA, ipA, isA);
                ch.power.pentodeCurrents (ch.penB, ipB, isB);
                ch.sumPlate += ipA + ipB + isA + isB;
                ++ch.sumCount;
            }
            ok = ok && ok2;
            if (chIdx == 0)
            {
                failuresPre += okPre ? 0 : 1;
                failuresPower += ok2 ? 0 : 1;
            }

            if (++ch.supplyCounter >= supplyInterval)
            {
                ch.supplyCounter = 0;
                updateSupply (ch);
            }

            double speakerVolts = reducedOrder ? behavioralPowerStage (ch, masterGain * ch.pre.voltage (ch.pFollower))
                                                       : ch.power.voltage (ch.wOut);
            {
                const double cutHz = 800.0 + 19200.0 * (1.0 - pots::audio ((double) cu));
                const double cutCoeff = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutHz / juce::jmax (1.0, sampleRate));
                cutFilterState += cutCoeff * (speakerVolts - cutFilterState);
                speakerVolts = cutFilterState;
            }
            constexpr double saneLimit = 120.0;
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

void AC30StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
