#include "FuzzFaceStyleFuzzProcessor.h"
#include "DualMono.h"
#include "IconKit.h"
#include "PotTaper.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double supplyVolts = 9.0;

    // Values shared by both drawings (identical on the two GGG schematics).
    constexpr double cInput = 2.2e-6;          // C1
    constexpr double rBiasQ1 = 33.0e3;         // R2, Q1 collector to the supply
    constexpr double rFeedback = 100.0e3;      // R3, Q2 emitter back to Q1 base
    constexpr double rCollectorLoad = 8.2e3;   // R5
    constexpr double cOutput = 0.01e-6;        // C3
    constexpr double cFuzzBypass = 22.0e-6;    // C2, on the Fuzz pot's wiper
    constexpr double fuzzPot = 1.0e3;          // R6, 1K linear ("B")
    constexpr double volumePot = 500.0e3;      // R10, 500K audio ("A")
    // The amplifier this pedal drives (same assumption as the DS-1 and Big Muff models).
    constexpr double outputLoadResistance = 1.0e6;
}

const FuzzFaceStyleFuzzProcessor::ModelSpec& FuzzFaceStyleFuzzProcessor::specFor (Model m) noexcept
{
    // Germanium: Is is set from a forward drop of ~0.22 V at 1 mA (AC128-class), beta 100 (the pedal is built from
    // hand-picked parts with a gain around 80-120), and a collector-base leakage of ~2 uA at the ~4.5 V Q2 sees.
    static const ModelSpec ge { "Fuzz Face-Style Fuzz", "ff", true, -supplyVolts, 2.0e-7, 100.0, 470.0, 2.2e6,
                              1.0 / (2.0 * 3.14159265 * 1.0e6), 100.0e-12, 30.0e-12 };   // AC128-class: fT ~1 MHz (beta corner ~10 kHz), Cob ~30 pF
    // Silicon: BC108C-class, Is 1e-14 (0.645 V at 0.7 mA), beta 500 (the C grade is 420-800), no leakage.
    static const ModelSpec si { "Silicon Fuzz Face-Style Fuzz", "ffsi", false, supplyVolts, 1.0e-14, 500.0, 330.0, 0.0,
                              1.0 / (2.0 * 3.14159265 * 250.0e6), 10.0e-12, 4.5e-12 };   // BC108C: fT ~250 MHz, Cob 4.5 pF
    return m == Model::germanium ? ge : si;
}

FuzzFaceStyleFuzzProcessor::FuzzFaceStyleFuzzProcessor (Model m)
    : model (m), spec (specFor (m))
{
    const juce::String p (spec.idPrefix);
    auto fuzz = std::make_unique<juce::AudioParameterFloat> (
        p + "_fuzz", "Fuzz", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto volume = std::make_unique<juce::AudioParameterFloat> (
        p + "_volume", "Volume", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    fuzzParam = fuzz.get();
    volumeParam = volume.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        p, spec.displayName, "|", std::move (fuzz));
    group->addChild (std::move (volume));
    parameters = std::move (group);
}

void FuzzFaceStyleFuzzProcessor::buildChannel (Channel& ch)
{
    auto& c = ch.c;
    const auto gnd = NodalCircuit::ground;
    const NodalCircuit::BjtParams bjt { spec.satCurrent, 25.85e-3, spec.beta, 4.0, spec.tauF, spec.cje, spec.cjc };

    const auto rail = c.addNode(), nIn = c.addNode();
    const auto nPick = nIn; // the guitar's own R + L is modelled externally now (PickupLoadEffect, EffectRegistry.cpp)
    c.addSource (rail, spec.railVolts);
    ch.srcIn = c.addSource (nIn, 0.0);
    // The guitar: a source resistance in front of a very low input impedance. See the header.

    const auto b1 = c.addNode(), c1 = c.addNode(), e2 = c.addNode(), c2 = c.addNode(), x = c.addNode();
    c.addCapacitor (nPick, b1, cInput);

    // Q1: emitter grounded, collector drives Q2's base directly (the same node).
    c.addResistor (rail, c1, rBiasQ1);
    c.addBjt (c1, b1, gnd, spec.pnp, bjt);
    // Q2: base on Q1's collector, emitter to the Fuzz pot, collector into the split load.
    c.addBjt (c2, c1, e2, spec.pnp, bjt);
    c.addResistor (e2, b1, rFeedback);   // R3: bias AND AC feedback

    // Fuzz pot: Q2's emitter to ground through the whole 1K at DC; the wiper is bypassed by 22 uF, so for signal only
    // the part between the emitter and the wiper is degeneration.
    const auto wiper = c.addNode();
    ch.rFuzzTop = c.addResistor (e2, wiper, fuzzPot * 0.5);
    ch.rFuzzBottom = c.addResistor (wiper, gnd, fuzzPot * 0.5);
    c.addCapacitor (wiper, gnd, cFuzzBypass);

    // Split collector load: R4 to the supply, R5 to the collector, the output taken at their junction.
    c.addResistor (rail, x, spec.rSupplySeries);
    c.addResistor (x, c2, rCollectorLoad);

    if (spec.rLeakage > 0.0)
    {
        // Germanium leakage: collector-base current that flows with the emitter open. Modelled as the resistance it
        // would present at the working point; a real part's leakage also moves with temperature, which this does not.
        c.addResistor (c1, b1, spec.rLeakage);
        c.addResistor (c2, c1, spec.rLeakage);
    }

    // Output: C3 into the Volume pot, with the amp's input across it.
    const auto volTop = c.addNode(), volW = c.addNode();
    c.addCapacitor (x, volTop, cOutput);
    ch.rVolumeTop = c.addResistor (volTop, volW, volumePot * 0.5);
    ch.rVolumeBottom = c.addResistor (volW, gnd, volumePot * 0.5);
    c.addResistor (volW, gnd, outputLoadResistance);

    ch.nQ1Collector = c1;
    ch.nQ2Emitter = e2;
    ch.nQ2Collector = c2;
    ch.nOut = volW;

    // Bias guesses from the hand derivation in the doc (signed for the PNP model).
    const double s = spec.pnp ? -1.0 : 1.0;
    c.setInitialGuess (b1, s * 0.2);
    c.setInitialGuess (c1, s * 0.7);
    c.setInitialGuess (e2, s * 0.5);
    c.setInitialGuess (c2, s * 4.0);
    c.setInitialGuess (x, s * 8.6);
}

void FuzzFaceStyleFuzzProcessor::updatePots (double fuzz, double volume)
{
    const auto clampFraction = [] (double v) { return juce::jlimit (0.001, 0.999, v); };
    const double f = clampFraction (fuzz), v = clampFraction (volume);
    // Fuzz is a linear pot; fully up = the wiper at the EMITTER end (no degeneration, maximum gain). The two segments
    // always sum to the pot's 1K, which is its DC resistance whatever the wiper does.
    // Volume is an audio-taper pot: the wiper-to-ground segment follows the 15% law.
    const double vol = pots::audio (v);
    for (auto& ch : channels)
    {
        ch.c.setResistance (ch.rFuzzTop, fuzzPot * (1.0 - f));
        ch.c.setResistance (ch.rFuzzBottom, fuzzPot * f);
        ch.c.setResistance (ch.rVolumeTop, volumePot * juce::jmax (0.001, 1.0 - vol));
        ch.c.setResistance (ch.rVolumeBottom, volumePot * juce::jmax (0.001, vol));
    }
}

void FuzzFaceStyleFuzzProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
    }

    smoothedFuzz.reset (newSampleRate, 0.02);
    smoothedFuzz.setCurrentAndTargetValue (fuzzParam->get());
    smoothedVolume.reset (newSampleRate, 0.02);
    smoothedVolume.setCurrentAndTargetValue (volumeParam->get());

    updatePots (fuzzParam->get(), volumeParam->get());

    dcOk = true;
    for (auto& ch : channels)
        dcOk = ch.c.prepare (newSampleRate) && dcOk;

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void FuzzFaceStyleFuzzProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedFuzz.setTargetValue (fuzzParam->get());
    smoothedVolume.setTargetValue (volumeParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float f = smoothedFuzz.getNextValue();
        const float v = smoothedVolume.getNextValue();

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (f, v);
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

void FuzzFaceStyleFuzzProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Placeholder, the same stand-in as the Big Muff: no fuzz glyph exists in the repo and this project never
    // invents one (docs/icons/AGENT-icon-notes.md).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
