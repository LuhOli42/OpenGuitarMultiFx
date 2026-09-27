#include "ToneBenderStyleFuzzProcessor.h"
#include "DualMono.h"
#include "IconKit.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double supplyVolts = 9.0;

    // Values shared by both drawings.
    constexpr double rInputShunt = 1.5e6;     // R9, input to ground
    constexpr double cInputShunt = 0.01e-6;   // C6, input to ground (a low-pass with the guitar's source resistance)
    constexpr double cInput = 4.7e-6;         // C1
    constexpr double rBaseQ1 = 100.0e3;       // R1, Q1 base to ground
    constexpr double rCollectorQ1 = 10.0e3;   // R2
    constexpr double cInterstage = 0.1e-6;    // C2, Q1 collector to Q2 base
    constexpr double rCollectorQ2 = 47.0e3;   // R3
    constexpr double rFeedback = 100.0e3;     // R4, Q3 emitter back to Q2 base
    constexpr double rSupplySeries = 1.0e3;   // R5, in series with Q3's collector load
    constexpr double cAttackBypass = 4.7e-6;  // C4
    constexpr double attackPot = 1.0e3;       // R7, 1K reverse-log
    constexpr double volumePot = 100.0e3;     // R8, 100K log
    constexpr double outputLoadResistance = 1.0e6;
}

const ToneBenderStyleFuzzProcessor::ModelSpec& ToneBenderStyleFuzzProcessor::specFor (Model m) noexcept
{
    // Germanium: OC81D / OC75-class PNP. Is from ~0.22 V at 1 mA, beta 100. The collector-base leakage is what biases
    // Q1 (see the header): 1 Mohm puts ~350 uA through Q1 and its collector near mid-supply, the operating point
    // the circuit is built around; a leakier part pulls it toward the rail and a tighter one cuts Q1 off, which is
    // why these were sorted by hand.
    static const ModelSpec ge { "Tone Bender-Style Fuzz", "tb", true, -supplyVolts, 2.0e-7, 100.0, 100.0, 100.0,
                                0.0, 0.0, 0.0, 0.0, 8.2e3, 0.01e-6, 1.0e6,
                                1.0 / (2.0 * 3.14159265 * 0.7e6), 100.0e-12, 35.0e-12 };   // OC75-class: fT ~0.7 MHz, Cob ~35 pF
    // Silicon: 2N4401 (Q1, Q2) and 2N5089 (Q3). Q3's 20k trimmer is set the way the builder would, to centre Q3's
    // collector; the value here was found by measuring that (see the doc).
    static const ModelSpec si { "Silicon Tone Bender-Style Fuzz", "tbsi", false, supplyVolts, 1.0e-14, 200.0, 200.0, 600.0,
                                470.0e3, 2.7e3, 100.0, 220.0, 5.4e3, 0.1e-6, 0.0,
                                1.0 / (2.0 * 3.14159265 * 250.0e6), 25.0e-12, 6.5e-12 };   // 2N4401 / 2N5089: fT 100-250 MHz, Cob 4-6.5 pF
    return m == Model::germanium ? ge : si;
}

ToneBenderStyleFuzzProcessor::ToneBenderStyleFuzzProcessor (Model m)
    : model (m), spec (specFor (m))
{
    const juce::String p (spec.idPrefix);
    auto attack = std::make_unique<juce::AudioParameterFloat> (
        p + "_attack", "Attack", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        p + "_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    attackParam = attack.get();
    volumeParam = volume.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        p, spec.displayName, "|", std::move (attack));
    group->addChild (std::move (volume));
    parameters = std::move (group);
}

void ToneBenderStyleFuzzProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;
    const auto bjt = [this] (double beta) { return NodalCircuit::BjtParams { spec.satCurrent, 25.85e-3, beta, 4.0, spec.tauF, spec.cje, spec.cjc }; };
    // An emitter resistor of 0 means the emitter goes straight to its node.
    const auto emitterVia = [&c] (double ohms, NodalCircuit::Node toNode)
    {
        if (ohms <= 0.0)
            return toNode;
        const auto e = c.addNode();
        c.addResistor (e, toNode, ohms);
        return e;
    };

    const auto rail = c.addNode(), nIn = c.addNode();
    const auto nPick = nIn; // the guitar's own R + L is modelled externally now (PickupLoadEffect, EffectRegistry.cpp)
    c.addSource (rail, spec.railVolts);
    ch.srcIn = c.addSource (nIn, 0.0);
    // The guitar, in front of an input that shunts 0.01 uF (C6) to ground: source resistance x C6 is a low-pass.
    c.addResistor (nPick, gnd, rInputShunt);
    c.addCapacitor (nPick, gnd, cInputShunt);

    // ---- Q1: common emitter, coupled in through C1 ----
    const auto b1 = c.addNode(), c1 = c.addNode();
    c.addCapacitor (nPick, b1, cInput);
    c.addResistor (b1, gnd, rBaseQ1);
    c.addResistor (rail, c1, rCollectorQ1);
    if (spec.rQ1CollectorBase > 0.0)
        c.addResistor (c1, b1, spec.rQ1CollectorBase);   // R10: the bias a silicon part needs
    const auto e1 = emitterVia (spec.rE1, gnd);
    c.addBjt (c1, b1, e1, spec.pnp, bjt (spec.betaQ1));

    // ---- Q2 + Q3: the Fuzz Face pair ----
    const auto b2 = c.addNode(), c2 = c.addNode(), c3 = c.addNode(), x = c.addNode();
    c.addCapacitor (c1, b2, cInterstage);
    c.addResistor (rail, c2, rCollectorQ2);
    const auto e2 = emitterVia (spec.rE2, gnd);
    c.addBjt (c2, b2, e2, spec.pnp, bjt (spec.betaQ2));
    // Q3's base IS Q2's collector (direct coupling).
    const auto e3 = c.addNode();
    c.addBjt (c3, c2, e3, spec.pnp, bjt (spec.betaQ3));
    const auto n = emitterVia (spec.rE3, e3); // R13 sits between Q3's emitter and the node R4 and the pot hang from
    c.addResistor (n, b2, rFeedback);         // R4: Q3's emitter back to Q2's base -- bias AND AC feedback

    // Attack: the pot from that node to ground, its wiper bypassed to ground by 4.7 uF, so only the top segment
    // degenerates Q3 for signal; the two segments always sum to the pot's 1K, its DC resistance.
    const auto wiper = c.addNode();
    ch.rAttackTop = c.addResistor (n, wiper, attackPot * 0.5);
    ch.rAttackBottom = c.addResistor (wiper, gnd, attackPot * 0.5);
    c.addCapacitor (wiper, gnd, cAttackBypass);

    // Split collector load, output at the junction (see FuzzFaceStyleFuzz.md).
    c.addResistor (rail, x, rSupplySeries);
    c.addResistor (x, c3, spec.rQ3Collector);

    if (spec.rLeakage > 0.0)
    {
        // Germanium leakage, collector to base, at the resistance it presents at the working point. For Q1 this is
        // its ONLY forward bias.
        c.addResistor (c1, b1, spec.rLeakage);
        c.addResistor (c2, b2, spec.rLeakage);
        c.addResistor (c3, c2, spec.rLeakage);
    }

    const auto volTop = c.addNode(), volW = c.addNode();
    c.addCapacitor (x, volTop, spec.cOut);
    ch.rVolumeTop = c.addResistor (volTop, volW, volumePot * 0.5);
    ch.rVolumeBottom = c.addResistor (volW, gnd, volumePot * 0.5);
    c.addResistor (volW, gnd, outputLoadResistance);

    ch.nC1 = c1;
    ch.nC2 = c2;
    ch.nC3 = c3;
    ch.nE3 = e3;
    ch.nOut = volW;

    const double s = spec.pnp ? -1.0 : 1.0;
    c.setInitialGuess (b1, s * 0.2);
    c.setInitialGuess (c1, s * 5.5);
    c.setInitialGuess (b2, s * 0.2);
    c.setInitialGuess (c2, s * 0.7);
    c.setInitialGuess (e3, s * 0.4);
    c.setInitialGuess (c3, s * 5.0);
    c.setInitialGuess (x, s * 8.6);
}

void ToneBenderStyleFuzzProcessor::updatePots (double attack, double volume)
{
    const auto clampFraction = [] (double v) { return juce::jlimit (0.001, 0.999, v); };
    const double a = clampFraction (attack), v = clampFraction (volume);
    // Attack is a 1K REVERSE-log pot: the segment that degenerates Q3 (top) is audio(1 - a) of the track, so at half
    // rotation it is already only 15% (150 ohm) and the gain climbs steadily over the whole sweep instead of
    // bunching at the very end, which is what a linear pot does to a gain control that goes as 1/(re + R).
    // Volume is an ordinary 100K audio pot.
    const double top = juce::jmax (0.001, pots::audio (1.0 - a));
    const double vol = pots::audio (v);
    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rAttackTop, attackPot * top);
        ch.c.setResistance (ch.rAttackBottom, attackPot * juce::jmax (0.001, 1.0 - top));
        ch.c.setResistance (ch.rVolumeTop, volumePot * juce::jmax (0.001, 1.0 - vol));
        ch.c.setResistance (ch.rVolumeBottom, volumePot * juce::jmax (0.001, vol));
    }
}

void ToneBenderStyleFuzzProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedAttack.reset (newSampleRate, 0.02);
    smoothedAttack.setCurrentAndTargetValue (attackParam->get());
    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());

    updatePots (attackParam->get(), volumeParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void ToneBenderStyleFuzzProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedAttack.setTargetValue (attackParam->get());
    smoothedVolume.setTargetValue (volumeParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float a = smoothedAttack.getNextValue();
        const float v = smoothedVolume.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (a, v);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            ch.c.setSource (ch.srcIn, (double) data[i]);
            const bool ok = ch.c.solveSample();
            data[i] = (float) ch.c.voltage (ch.nOut);

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

void ToneBenderStyleFuzzProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Placeholder, the same stand-in as the other fuzzes (no fuzz glyph exists in the repo; never invent one).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
