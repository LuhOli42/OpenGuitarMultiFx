#include "AC15StyleAmplifierProcessor.h"
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

    // ---- supply (docs/circuits/AC15Twin.md, "Power supply"). EZ81 rectifier, 8H/120mA choke, 450V post-choke B+ --
    // all printed on the drawing. ----
    constexpr double railPlatesNominal = 450.0;
    constexpr double rectifierResistance = 200.0; // bridge + transformer (assumed, a small EZ81 supply): full drive sags noticeably
    constexpr double chokeResistance = 150.0;     // assumed (not printed)
    constexpr double chokeInductance = 8.0;       // H, printed on the drawing

    // ---- reduced-order power stage: calibration data, A15_POWERCAL in the test file ----
    constexpr int bmSagPoints = 14;
    constexpr double bmSagDrive[bmSagPoints] = { 0.02, 0.05, 0.1, 0.2, 0.4, 0.7, 1.1, 1.6, 2.2, 3.0, 4.0, 5.2, 6.6, 8.2 };
    constexpr double bmSagRail[bmSagPoints]  = { 450.0, 450.0, 450.0, 449.8, 449.4, 448.5, 447.0, 444.5, 440.8, 435.5, 428.0, 418.0, 405.0, 390.0 };

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

    /** EF86 pentode, published Koren SPICE fit (not independently re-derived for this circuit, same disclosure as
        triode12AX7() -- see docs/circuits/AC15Twin.md). */
    KorenPentode::Parameters pentodeEF86()
    {
        KorenPentode::Parameters p;
        p.mu = 34.9;
        p.ex = 1.35;
        p.kg1 = 2648.1;
        p.kg2 = 4500.0;
        p.kp = 222.06;
        p.kvb = 4.7;
        return p;
    }

    /** EL84 (6BQ5) pentode, published Koren SPICE fit (mu 16, Kg1 570, Kg2 4200, Kp 50, Kvb 24, Ex 1.35 -- a
        commonly-circulated set, not independently re-fitted to this specific amp's own measured curve the way the
        Marshall/Fender power tubes on this roadmap were -- see docs/circuits/AC15Twin.md). Each EL84 has its OWN
        grid/plate (a real push-pull pair, not two tubes sharing nodes) -- no pair-halving here. */
    KorenPentode::Parameters pentodeEL84()
    {
        KorenPentode::Parameters p;
        p.mu = 16.0;
        p.ex = 1.35;
        p.kg1 = 570.0;
        p.kg2 = 4200.0;
        p.kp = 50.0;
        p.kvb = 24.0;
        return p;
    }

    constexpr double cgp = 1.7e-12; // grid-plate (Miller) capacitance of a 12AX7/EF86 section

    // Output transformer: ~8k plate-to-plate (two EL84s, well short of the multi-EL34 Marshalls), 16 ohm secondary tap
    // (the real amp ships a fixed ~15 ohm speaker -- see the header's Speaker note). Inductance/coupling are assumed
    // (not printed): a smaller core than the Marshall/Fender amps on this roadmap, a leaner bass corner is expected.
    constexpr double primaryHalfInductance = 5.0;         // H per half (20 H plate to plate)
    constexpr double halfToSecondaryTurns = 11.18;        // (8000/16)^0.5 / 2
    constexpr double couplingHalves = 0.999;
    constexpr double couplingSecondary = 0.998;
    constexpr double primaryHalfResistance = 60.0;
    constexpr double secondaryResistance = 0.2;

    constexpr double speakerEddyLoss = 150.0;

    constexpr double speakerNominal[3] = { 4.0, 8.0, 16.0 };
    constexpr int matchedSpeaker = 2; // the real amp's own ~15 ohm speaker, rounded to this project's 16 ohm tap

    constexpr double powerTheta = 0.9;
}

AC15StyleAmplifierProcessor::AC15StyleAmplifierProcessor()
{
    // Input: the real amp's own two Channel I jacks (High / Low sensitivity, sharing one EF86 grid).
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "a15_input", "Input", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return juce::roundToInt (v) == 0 ? juce::String ("High") : juce::String ("Low"); }));
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };
    auto volume = make ("a15_volume", "Volume", 0.4f);
    auto tone = make ("a15_tone", "Tone", 0.5f);
    auto output = make ("a15_output", "Output", 0.5f);
    auto power = make ("a15_power", "Power Drive", 1.0f);
    auto bias = make ("a15_bias", "Bias", 0.5f);
    auto feel = make ("a15_tube_feel", "Tube Feel", 1.0f);
    auto speaker = std::make_unique<juce::AudioParameterFloat> (
        "a15_speaker", "Speaker", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) matchedSpeaker,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return juce::String (speakerNominal[juce::jlimit (0, 2, juce::roundToInt (v))], 0) + " ohm";
        }));

    inputParam = input.get();
    volumeParam = volume.get();
    toneParam = tone.get();
    outputParam = output.get();
    powerParam = power.get();
    biasParam = bias.get();
    tubeFeelParam = feel.get();
    speakerParam = speaker.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ac15", "AC15-Style Amplifier", "|", std::move (input));
    group->addChild (std::move (volume));
    group->addChild (std::move (tone));
    group->addChild (std::move (output));
    auto page2 = std::make_unique<juce::AudioProcessorParameterGroup> ("ac15_page2", "Page 2", "|", std::move (power));
    page2->addChild (std::move (bias));
    page2->addChild (std::move (feel));
    page2->addChild (std::move (speaker));
    group->addChild (std::move (page2));
    parameters = std::move (group);
}

void AC15StyleAmplifierProcessor::buildChannel (Channel& ch)
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
        c.addCapacitor (ch.sA, gnd, 32.0e-6); // 16+16 mfd bank (one side, before the choke)
        c.addResistor (ch.sA, nl, chokeResistance);
        c.addCoupledInductors ({ { nl, ch.sB } }, { chokeInductance });
        c.addCapacitor (ch.sB, gnd, 32.0e-6); // the other 16+16 mfd bank (post-choke, this amp's own B+)

        ch.iA = c.addCurrentSource (ch.sA, -0.08);
        c.setInitialGuess (ch.sA, railPlatesNominal);
        c.setInitialGuess (nl, railPlatesNominal);
        c.setInitialGuess (ch.sB, railPlatesNominal);
    }

    // ================================================================ preamp: EF86 -> Tone -> Volume
    {
        auto& c = ch.pre;
        const auto in = c.addNode();
        ch.pSrcIn = c.addSource (in, 0.0);
        const auto rail = c.addNode();
        ch.pSrcRail = c.addSource (rail, railPlatesNominal * 0.85);

        // EF86: 220k grid stopper (both High/Low jacks share this one grid -- see the header's own note), 1.5k
        // cathode with a 25 uF bypass (full bass, no local NFB), 100k plate load, screen assumed at a fixed fraction
        // of the decoupled preamp rail (this tube's own screen current is too small to move it meaningfully sample
        // to sample, unlike a power tube's).
        const auto g = c.addNode(), k = c.addNode();
        ch.pPlate = c.addNode();
        c.addResistor (in, g, 220.0e3);
        c.addResistor (g, gnd, 1.0e6); // grid leak
        // EF86's real screen operating voltage (~200 V, well under its ~300 V rating) is far lower than the raw
        // decoupled preamp rail -- a dedicated screen dropping resistor exists on the real amp (not fully legible on
        // the scan, see the doc); modelled here as a fixed, plausible screen voltage rather than a literal resistor.
        ch.penPre = c.addPentode (ch.pPlate, g, k, pentodeEF86(), 200.0);
        c.addResistor (k, gnd, 1.5e3);
        c.addCapacitor (k, gnd, 25.0e-6);
        c.addResistor (rail, ch.pPlate, 100.0e3);
        c.setInitialGuess (ch.pPlate, 220.0);
        c.setInitialGuess (k, 1.4);

        // Tone: a single treble-cut control (no Bass/Middle/Presence on the real amp) -- a coupling cap into a fixed
        // load resistor, with a variable resistor + cap shunting highs to ground as the knob turns down.
        const auto toneIn = c.addNode();
        c.addCapacitor (ch.pPlate, toneIn, 0.02e-6);
        c.addResistor (toneIn, gnd, 1.0e6); // grid-leak-style DC reference for the coupling cap
        ch.pToneOut = c.addNode();
        c.addResistor (toneIn, ch.pToneOut, 33.0e3);
        ch.rToneTop = c.addResistor (ch.pToneOut, gnd, 1.0); // Tone pot: more resistance to ground = brighter
        c.addCapacitor (ch.pToneOut, gnd, 1.0e-9);

        // Volume: 1M log pot.
        ch.pVolOut = c.addNode();
        ch.rVolTop = c.addResistor (ch.pToneOut, ch.pVolOut, 1.0e6);
        ch.rVolBot = c.addResistor (ch.pVolOut, gnd, 1.0e6);
    }

    // ================================================================ power: cathodyne PI + 2x EL84, cathode-biased, no NFB
    // reducedOrder: none of this exists -- behavioralPowerStage() is driven directly from the preamp's own Volume
    // pot output instead (see the header's "reduced-order power stage" note), so this expensive netlist (a Newton
    // solve per sample: PI triode + 2 EL84 pentodes + a coupled-inductor OT) is skipped entirely, not just ignored.
    if (! reducedOrder)
    {
        auto& c = ch.power;
        const auto rail = c.addNode();
        ch.wSrcRail = c.addSource (rail, railPlatesNominal);
        const auto cf = c.addNode();
        ch.wSrcCf = c.addSource (cf, 0.0);

        // Cathodyne (split-load) phase inverter: a SINGLE 12AX7 half, equal 47k plate/cathode loads (confirmed on the
        // drawing) -- two anti-phase outputs, not a long-tailed pair.
        ch.wPiGrid = c.addNode();
        const auto piK = c.addNode();
        ch.wPiPlate = c.addNode();
        ch.wPiCathode = piK;
        c.addCapacitor (cf, ch.wPiGrid, 0.02e-6);
        c.addResistor (ch.wPiGrid, gnd, 1.0e6);
        c.addTriode (ch.wPiPlate, ch.wPiGrid, piK, triode12AX7());
        c.addCapacitor (ch.wPiGrid, ch.wPiPlate, cgp);
        c.addResistor (rail, ch.wPiPlate, 47.0e3);
        c.addResistor (piK, gnd, 47.0e3);
        c.setInitialGuess (ch.wPiPlate, 300.0);
        c.setInitialGuess (piK, 130.0);

        // Each output couples through a 100k-decoupled 0.05 uF cap into its own EL84's 10k grid stopper (confirmed on
        // the drawing, symmetric both sides).
        ch.wGridA = c.addNode();
        ch.wGridB = c.addNode();
        c.addCapacitor (ch.wPiPlate, ch.wGridA, 0.05e-6);
        c.addResistor (ch.wGridA, gnd, 220.0e3);
        c.addCapacitor (piK, ch.wGridB, 0.05e-6);
        c.addResistor (ch.wGridB, gnd, 220.0e3);
        const auto g3s = c.addNode(), g4s = c.addNode();
        c.addResistor (ch.wGridA, g3s, 10.0e3);
        c.addResistor (ch.wGridB, g4s, 10.0e3);

        // Two EL84s, a real push-pull pair (own grid/plate each), sharing ONE 130 ohm cathode-bias resistor with a
        // 50 uF bypass (confirmed 130 ohm on the drawing; the bypass value wasn't legible, 50 uF is a typical value
        // for this role) -- self-biased, no fixed bias supply, matching the real amp.
        ch.wCathodeBias = c.addNode();
        ch.rCathodeBias = c.addResistor (ch.wCathodeBias, gnd, 130.0);
        c.addCapacitor (ch.wCathodeBias, gnd, 50.0e-6);

        ch.wPP1 = c.addNode();
        ch.wPP2 = c.addNode();
        // EL84's own screen rating tops out around 300 V; the real amp's screens sit well under the raw plate rail
        // (a dedicated screen supply/dropping network, not fully legible on the scan -- see the doc). Modelled as a
        // fixed, plausible screen voltage rather than a literal resistor network.
        ch.penA = c.addPentode (ch.wPP1, g3s, ch.wCathodeBias, pentodeEL84(), 290.0);
        ch.penB = c.addPentode (ch.wPP2, g4s, ch.wCathodeBias, pentodeEL84(), 290.0);
        c.addResistor (rail, ch.wPP1, 100.0); // confirmed on the drawing (each plate's own series resistor)
        c.addResistor (rail, ch.wPP2, 100.0);
        c.setInitialGuess (ch.wPP1, 380.0);
        c.setInitialGuess (ch.wPP2, 375.0);
        c.setInitialGuess (ch.wCathodeBias, 11.0);

        // Output transformer, no negative feedback (the real amp has none): plate-to-plate, 16 ohm tap.
        c.addCapacitor (ch.wPP1, ch.wPP2, 400.0e-12);
        c.addResistor (ch.wPP1, ch.wPP2, 20.0e3);
        c.addCapacitor (ch.wPP1, gnd, 400.0e-12);
        c.addCapacitor (ch.wPP2, gnd, 400.0e-12);
        const auto a1 = c.addNode(), a2 = c.addNode(), sw = c.addNode();
        c.addResistor (rail, a1, primaryHalfResistance);
        c.addResistor (rail, a2, primaryHalfResistance); // the primary's own centre tap sits directly on the plate rail
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

void AC15StyleAmplifierProcessor::updatePots (const Knobs& k)
{
    lastKnobs = k;
    // Tone: a treble-cut pot -- more knob = brighter (more resistance to ground = less shunting of highs).
    const double toneTop = juce::jmax (1.0, 1.0e6 * k.tone);
    // Volume: 1M audio taper.
    const double volBottom = juce::jmax (1.0, 1.0e6 * pots::audio (k.volume));

    // Bias: a synthetic shift of the shared cathode-bias resistor (the real amp has no adjustable bias trim, being
    // self-biased) -- 0.7x .. 1.3x the real 130 ohm value.
    const double cathodeR = cathodeResistanceOverride > 0.0 ? cathodeResistanceOverride : 130.0 * (0.7 + 0.6 * k.bias);
    // Tube Feel: the supply's own series (sag) resistance. 1 = the real amp; 0 = a stiff supply (5% of the sag).
    const double rectifier = rectifierResistance * (0.05 + 0.95 * k.tubeFeel);

    for (auto& ch : channels)
    {
        ch.pre.setResistance (ch.rToneTop, toneTop);
        ch.pre.setResistance (ch.rVolTop, juce::jmax (1.0, 1.0e6 - volBottom));
        ch.pre.setResistance (ch.rVolBot, volBottom);
        if (! reducedOrder)
        {
            ch.power.setResistance (ch.rCathodeBias, cathodeR);
            ch.supply.setResistance (ch.rRect, rectifier);
        }
        applySpeaker (ch, k.speaker);
    }
    if (appliedSpeaker != k.speaker && ! resistiveLoadForced)
        appliedSpeaker = k.speaker;

    speakerGain = reducedOrder ? 1.0 : std::pow (speakerNominal[juce::jlimit (0, 2, k.speaker)] / speakerNominal[matchedSpeaker], -0.8);
}

void AC15StyleAmplifierProcessor::applySpeaker (Channel& ch, int index) const
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

void AC15StyleAmplifierProcessor::debugSetResistiveLoad (double ohms)
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

void AC15StyleAmplifierProcessor::debugSetCathodeResistance (double ohms)
{
    cathodeResistanceOverride = ohms;
    if (reducedOrder)
        return;
    for (auto& ch : channels)
        ch.power.setResistance (ch.rCathodeBias, ohms);
}

void AC15StyleAmplifierProcessor::recover (Channel& ch) const
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

void AC15StyleAmplifierProcessor::updateSupply (Channel& ch) const
{
    const double n = (double) juce::jmax (1, ch.sumCount);
    if (! supplyCurrentFrozen)
        ch.supply.setCurrentSource (ch.iA, -juce::jlimit (0.0, 0.3, ch.sumPlate / n));
    ch.supply.solveSample();
    ch.sumPlate = 0.0;
    ch.sumCount = 0;

    const auto rail = [&] (NodalCircuit::Node node, double maxVolts) { return juce::jlimit (0.0, maxVolts, ch.supply.voltage (node)); };
    if (! reducedOrder)
        ch.power.setSource (ch.wSrcRail, rail (ch.sB, 500.0));
    ch.pre.setSource (ch.pSrcRail, rail (ch.sB, 500.0) * 0.85);
}

double AC15StyleAmplifierProcessor::behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept
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

double AC15StyleAmplifierProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    switch (p)
    {
        case Probe::preampPlate: return ch.pre.voltage (ch.pPlate);
        case Probe::toneOut: return ch.pre.voltage (ch.pToneOut);
        case Probe::volumeOut: return ch.pre.voltage (ch.pVolOut);
        case Probe::piGrid: return ch.power.voltage (ch.wPiGrid);
        case Probe::piPlate: return ch.power.voltage (ch.wPiPlate);
        case Probe::piCathode: return ch.power.voltage (ch.wPiCathode);
        case Probe::powerGridA: return ch.power.voltage (ch.wGridA);
        case Probe::powerPlateA: return ch.power.voltage (ch.wPP1);
        case Probe::powerPlateB: return ch.power.voltage (ch.wPP2);
        case Probe::speaker: return reducedOrder ? ch.bmOutput : ch.power.voltage (ch.wOut);
        case Probe::cathodeBias: return ch.power.voltage (ch.wCathodeBias);
    }
    return 0.0;
}

double AC15StyleAmplifierProcessor::plateCurrentTotal() const noexcept
{
    if (reducedOrder)
        return 0.0;
    double a = 0.0, b = 0.0, sa = 0.0, sb = 0.0;
    channels[0].power.pentodeCurrents (channels[0].penA, a, sa);
    channels[0].power.pentodeCurrents (channels[0].penB, b, sb);
    return a + b;
}

void AC15StyleAmplifierProcessor::prepare (double newSampleRate, int, int)
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
    setup (smoothedTone, toneParam, 0.02);
    setup (smoothedOutput, outputParam, 0.02);
    setup (smoothedPower, powerParam, 0.02);
    setup (smoothedBias, biasParam, 0.05);
    setup (smoothedFeel, tubeFeelParam, 0.05);

    idleSupplyCurrent = 0.06;
    appliedSpeaker = matchedSpeaker;
    updatePots ({ volumeParam->get(), toneParam->get(), powerParam->get(), biasParam->get(), tubeFeelParam->get(),
                  juce::roundToInt (speakerParam->get()) });

    dcOk = true;
    for (auto& ch : channels)
    {
        const double supplyRate = newSampleRate / (double) supplyInterval;
        bool passOk = true;
        double ipRun = 0.06;
        for (int pass = 0; pass < 10; ++pass)
        {
            passOk = ch.supply.prepare (supplyRate);

            ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * 0.85);
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
        ch.pre.setSource (ch.pSrcRail, ch.supply.voltage (ch.sB) * 0.85);
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
}

void AC15StyleAmplifierProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedVolume.setTargetValue (volumeParam->get());
    smoothedTone.setTargetValue (toneParam->get());
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
        const float to = smoothedTone.getNextValue();
        const float ou = smoothedOutput.getNextValue();
        const float pw = smoothedPower.getNextValue();
        const float bi = smoothedBias.getNextValue();
        const float fe = smoothedFeel.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots ({ vo, to, pw, bi, fe, speakerChoice });
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
                ch.power.setSource (ch.wSrcCf, masterGain * ch.pre.voltage (ch.pVolOut));
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

            const double speakerVolts = reducedOrder ? behavioralPowerStage (ch, masterGain * ch.pre.voltage (ch.pVolOut))
                                                       : ch.power.voltage (ch.wOut);
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

void AC15StyleAmplifierProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::overdrive_svg, IconData::overdrive_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
