#include "DynamicCabProcessor.h"
#include "IRLoaderProcessor.h" // bundledCabIRs -- same built-in list as the Cab role
#include "IconKit.h"

#include <IconData.h>

#include <array>
#include <cmath>

namespace openguitarmultifx
{

namespace
{
    juce::AudioParameterFloat* makeSourceParam (const char* paramID, const char* paramName, float defaultValue,
                                                juce::AudioProcessorParameterGroup& group)
    {
        auto p = std::make_unique<juce::AudioParameterFloat> (
            paramID, paramName,
            juce::NormalisableRange<float> (0.0f, (float) bundledCabIRs::count(), 1.0f),
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) -> juce::String
                {
                    const int i = (int) std::lround (v);
                    return i <= 0 ? juce::String ("File") : bundledCabIRs::selectorName (i - 1);
                })
                .withValueFromStringFunction ([] (const juce::String& text) -> float
                {
                    for (int i = 0; i < bundledCabIRs::count(); ++i)
                        if (text == bundledCabIRs::selectorName (i) || text == bundledCabIRs::displayName (i))
                            return (float) (i + 1);
                    return 0.0f;
                }));
        auto* raw = p.get();
        group.addChild (std::move (p));
        return raw;
    }
}

DynamicCabProcessor::DynamicCabProcessor()
{
    auto blendParam = std::make_unique<juce::AudioParameterFloat> (
        "dyncab_blend", "Blend", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto dynamics = std::make_unique<juce::AudioParameterFloat> (
        "dyncab_dynamics", "Dynamics", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
    auto mix = std::make_unique<juce::AudioParameterFloat> (
        "dyncab_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "dyncab_output", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f);

    blend = blendParam.get();
    dynamicsAmount = dynamics.get();
    mixParam = mix.get();
    outputGainDb = output.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "dyncab", "Dynamic Cab", "|",
        std::move (blendParam), std::move (dynamics), std::move (mix), std::move (output));

    // Appended AFTER blend/dynamics/mix/output so param order (and presets)
    // are untouched. Slot A defaults to the bundled closed-back so a fresh
    // block already sounds like a cab; slot B stays "File"/empty so the
    // single-IR default behaves exactly like a plain Cab.
    sourceParamA = makeSourceParam ("dyncab_source_a", "IR A Source", 1.0f, *parameters);
    sourceParamB = makeSourceParam ("dyncab_source_b", "IR B Source", 0.0f, *parameters);

    startTimer (100); // sweeps irSlotA/B and applies Source changes -- see DeferredReclaimer
}

DynamicCabProcessor::~DynamicCabProcessor() = default;

bool DynamicCabProcessor::hasImpulseResponse (int slot) const noexcept
{
    return slot == 0 ? irSlotA.currentRaw() != nullptr : irSlotB.currentRaw() != nullptr;
}

void DynamicCabProcessor::loadImpulseResponse (const juce::File& irFile, int slot)
{
    auto conv = std::make_unique<juce::dsp::Convolution>();

    if (sampleRate > 0.0)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedNumChannels };
        conv->prepare (spec);
    }

    conv->loadImpulseResponse (irFile,
                                juce::dsp::Convolution::Stereo::yes,
                                juce::dsp::Convolution::Trim::yes,
                                (size_t) (sampleRate > 0.0 ? (int) (0.15 * sampleRate) : 0), // a cab IR is over within ~100 ms (see IRLoaderProcessor)
                                juce::dsp::Convolution::Normalise::yes);

    if (slot == 0)
    {
        lastLoadedFileA = irFile;
        loadedNameA = irFile.getFileName();
        irSlotA.publish (std::move (conv));
        *sourceParamA = 0.0f; // a file load is an explicit choice of the "File" source
        appliedSourceA = 0;
    }
    else
    {
        lastLoadedFileB = irFile;
        loadedNameB = irFile.getFileName();
        irSlotB.publish (std::move (conv));
        *sourceParamB = 0.0f;
        appliedSourceB = 0;
    }
}

void DynamicCabProcessor::loadBundledIR (int index, int slot)
{
    auto conv = std::make_unique<juce::dsp::Convolution>();

    conv->loadImpulseResponse (bundledCabIRs::data (index), (size_t) bundledCabIRs::dataSize (index),
                               juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::yes,
                               (size_t) (sampleRate > 0.0 ? (int) (0.15 * sampleRate) : 0),
                               juce::dsp::Convolution::Normalise::yes);

    // Load BEFORE prepare so the IR is fully initialised for the first
    // process() call (see IRLoaderProcessor::loadBundledIR).
    if (sampleRate > 0.0)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedNumChannels };
        conv->prepare (spec);
    }

    // lastLoadedFileA/B deliberately kept -- flipping the selector back to
    // "File" restores the user's own IR rather than losing it.
    if (slot == 0)
    {
        loadedNameA = bundledCabIRs::selectorName (index);
        irSlotA.publish (std::move (conv));
    }
    else
    {
        loadedNameB = bundledCabIRs::selectorName (index);
        irSlotB.publish (std::move (conv));
    }
}

void DynamicCabProcessor::applySourceSelection (int slot)
{
    auto* sourceParam = slot == 0 ? sourceParamA : sourceParamB;
    auto& applied = slot == 0 ? appliedSourceA : appliedSourceB;
    auto& lastFile = slot == 0 ? lastLoadedFileA : lastLoadedFileB;
    auto& loadedName = slot == 0 ? loadedNameA : loadedNameB;
    auto& irSlot = slot == 0 ? irSlotA : irSlotB;

    const int wanted = juce::jlimit (0, bundledCabIRs::count(), (int) std::lround (sourceParam->get()));
    if (wanted == applied)
        return;
    applied = wanted;

    if (wanted > 0)
        loadBundledIR (wanted - 1, slot);
    else if (lastFile.existsAsFile())
        loadImpulseResponse (lastFile, slot); // rewrites applied = 0, same value
    else
    {
        irSlot.publish (nullptr);
        loadedName.clear();
    }
}

void DynamicCabProcessor::clearImpulseResponse (int slot)
{
    if (slot == 0)
    {
        irSlotA.publish (nullptr);
        lastLoadedFileA = juce::File();
        loadedNameA.clear();
        *sourceParamA = 0.0f;
        appliedSourceA = 0;
    }
    else
    {
        irSlotB.publish (nullptr);
        lastLoadedFileB = juce::File();
        loadedNameB.clear();
        *sourceParamB = 0.0f;
        appliedSourceB = 0;
    }
}

void DynamicCabProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    preparedBlockSize = maxBlockSize;
    preparedNumChannels = juce::jmax (1, numChannels);

    scratchA.setSize (preparedNumChannels, maxBlockSize, false, false, true);
    scratchB.setSize (preparedNumChannels, maxBlockSize, false, false, true);
    dryScratch.setSize (preparedNumChannels, maxBlockSize, false, false, true);

    inputEnvelope.prepare (newSampleRate);
    inputEnvelope.setAttackTime (5.0f);
    inputEnvelope.setReleaseTime (150.0f);

    // A normalised cab IR lands ~-8 dB under the dry for guitar content;
    // the matcher starts near that and follows the actual IRs and signal.
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 2.5f);

    // Same reasoning as IRLoaderProcessor::prepare(): a sample-rate/block-
    // size change invalidates an already-prepared Convolution, so rebuilding
    // whichever source is selected and swapping it in atomically rather
    // than mutating the live one.
    appliedSourceA = appliedSourceB = -1;
    applySourceSelection (0);
    applySourceSelection (1);
}

void DynamicCabProcessor::reset()
{
    inputEnvelope.reset();
    // Convolution's own internal tail state isn't safe to reach into from
    // here without risking a race with the audio thread -- see prepare()'s
    // reload-on-change comment for how state actually resets.
}

void DynamicCabProcessor::process (juce::AudioBuffer<float>& buffer)
{
    auto* convA = irSlotA.currentRaw();
    auto* convB = irSlotB.currentRaw();

    if (convA == nullptr && convB == nullptr)
        return; // nothing loaded -- pass through unchanged

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), preparedNumChannels);
    const float mix = mixParam->get();

    // With any IR loaded the wet is level-matched to the dry so Mix is a
    // real crossfade (docs/circuits/MixLaw.md) -- see IRLoaderProcessor.
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // Only one slot loaded -- behave exactly like a single-IR cab, ignoring
    // Blend/Dynamics entirely, rather than crossfading toward silence in
    // whichever slot is still empty.
    float effectiveBlend = (convA == nullptr) ? 1.0f : (convB == nullptr) ? 0.0f : blend->get();

    if (convA != nullptr && convB != nullptr && dynamicsAmount->get() > 0.0f)
    {
        // Block-level peak (all channels) drives how hard the room/ribbon
        // IR gets pulled in on louder playing -- see class doc comment for
        // why this mapping is this project's own design, not a port of an
        // existing algorithm.
        const float peak = buffer.getMagnitude (0, numSamples);
        const float envelopeLevel = inputEnvelope.processSample (peak);
        effectiveBlend = juce::jlimit (0.0f, 1.0f, effectiveBlend + dynamicsAmount->get() * envelopeLevel);
    }

    if (convA != nullptr)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            scratchA.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> blockA (scratchA.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> contextA (blockA);
        convA->process (contextA);
    }

    if (convB != nullptr)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            scratchB.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> blockB (scratchB.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> contextB (blockB);
        convB->process (contextB);
    }

    if (convA != nullptr && convB != nullptr)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer.copyFrom (ch, 0, scratchA, ch, 0, numSamples);
            buffer.applyGain (ch, 0, numSamples, 1.0f - effectiveBlend);
            buffer.addFrom (ch, 0, scratchB, ch, 0, numSamples, effectiveBlend);
        }
    }
    else
    {
        auto& onlyLoaded = (convA != nullptr) ? scratchA : scratchB;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, onlyLoaded, ch, 0, numSamples);
    }

    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            wetMatch[(size_t) ch].accumulate (dry[i], data[i]);
            data[i] = dry[i] * (1.0f - mix) + data[i] * mix * wetMatch[(size_t) ch].gain();
        }
    }
    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
        wetMatch[(size_t) ch].endBlock (numSamples, 1);

    const float outGain = juce::Decibels::decibelsToGain (outputGainDb->get());
    if (outGain != 1.0f)
        buffer.applyGain (outGain);
}

juce::String DynamicCabProcessor::getStatusText() const
{
    if (loadedNameA.isEmpty() && loadedNameB.isEmpty())
        return "No IR loaded";

    juce::String status;
    status << "A: " << (loadedNameA.isEmpty() ? juce::String ("--") : loadedNameA);
    status << "  B: " << (loadedNameB.isEmpty() ? juce::String ("--") : loadedNameB);
    return status;
}

std::unique_ptr<juce::XmlElement> DynamicCabProcessor::getState() const
{
    auto xml = EffectProcessor::getState(); // base: blend/dynamics/mix/output

    if (lastLoadedFileA != juce::File())
        xml->setAttribute ("irPathA", lastLoadedFileA.getFullPathName());
    if (lastLoadedFileB != juce::File())
        xml->setAttribute ("irPathB", lastLoadedFileB.getFullPathName());

    return xml;
}

void DynamicCabProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state); // base: blend/dynamics/mix/output/sources

    for (int slot = 0; slot < 2; ++slot)
    {
        auto* sourceParam = slot == 0 ? sourceParamA : sourceParamB;
        const auto sourceID = slot == 0 ? "dyncab_source_a" : "dyncab_source_b";
        const auto pathKey = slot == 0 ? "irPathA" : "irPathB";

        const bool savedByBundledAwareVersion = state.hasAttribute (sourceID);
        const auto path = state.getStringAttribute (pathKey);

        if (savedByBundledAwareVersion && std::lround (sourceParam->get()) > 0)
        {
            // New-format preset that explicitly picked a bundled IR -- that
            // selection wins over any stale irPath attribute also on it.
            applySourceSelection (slot);
            continue;
        }

        if (! savedByBundledAwareVersion)
            *sourceParam = 0.0f; // pre-bundled preset: keep its file-or-nothing meaning

        if (path.isNotEmpty())
        {
            const juce::File file (path);
            if (file.existsAsFile())
            {
                loadImpulseResponse (file, slot);
                continue;
            }
            // else: moved/deleted since the preset was saved -- leave unloaded
            // rather than fail the whole preset load.
        }

        applySourceSelection (slot); // file source with no (valid) file -> clears
    }
}

void DynamicCabProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Same cab.svg as IRLoaderProcessor's "Cab" role -- still fundamentally
    // the same gear category, just able to blend two IRs instead of one.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::cab_svg, IconData::cab_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
