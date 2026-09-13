#include "DynamicCabProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

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

    startTimer (100); // sweeps irSlotA/B -- see DeferredReclaimer
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
                                0, // 0 -- use the whole file
                                juce::dsp::Convolution::Normalise::yes);

    if (slot == 0)
    {
        lastLoadedFileA = irFile;
        loadedNameA = irFile.getFileName();
        irSlotA.publish (std::move (conv));
    }
    else
    {
        lastLoadedFileB = irFile;
        loadedNameB = irFile.getFileName();
        irSlotB.publish (std::move (conv));
    }
}

void DynamicCabProcessor::clearImpulseResponse (int slot)
{
    if (slot == 0)
    {
        irSlotA.publish (nullptr);
        lastLoadedFileA = juce::File();
        loadedNameA.clear();
    }
    else
    {
        irSlotB.publish (nullptr);
        lastLoadedFileB = juce::File();
        loadedNameB.clear();
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

    // Same reasoning as IRLoaderProcessor::prepare(): a sample-rate/block-
    // size change invalidates an already-prepared Convolution, so reloading
    // builds a fresh instance and swaps it in atomically rather than
    // mutating the live one.
    if (lastLoadedFileA.existsAsFile())
        loadImpulseResponse (lastLoadedFileA, 0);
    if (lastLoadedFileB.existsAsFile())
        loadImpulseResponse (lastLoadedFileB, 1);
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
    const bool needsDryBlend = mix < 0.999f;

    if (needsDryBlend)
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

    if (needsDryBlend)
    {
        buffer.applyGain (mix);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom (ch, 0, dryScratch, ch, 0, numSamples, 1.0f - mix);
    }

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
    EffectProcessor::setState (state); // base: blend/dynamics/mix/output

    const auto pathA = state.getStringAttribute ("irPathA");
    if (pathA.isNotEmpty())
    {
        const juce::File fileA (pathA);
        if (fileA.existsAsFile())
            loadImpulseResponse (fileA, 0);
        // else: moved/deleted since the preset was saved -- leave unloaded
        // rather than fail the whole preset load.
    }

    const auto pathB = state.getStringAttribute ("irPathB");
    if (pathB.isNotEmpty())
    {
        const juce::File fileB (pathB);
        if (fileB.existsAsFile())
            loadImpulseResponse (fileB, 1);
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
