#include "NAMProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <NAM/get_dsp.h>

namespace openguitarmultifx
{

NAMProcessor::NAMProcessor (juce::String chainRoleName)
    : name (std::move (chainRoleName))
{
    auto input = std::make_unique<juce::AudioParameterFloat> (
        "nam_input", "Input", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "nam_output", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f);

    inputGainDb = input.get();
    outputGainDb = output.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "nam", name, "|", std::move (input), std::move (output));

    startTimer (100); // sweeps modelSlot -- see DeferredReclaimer
}

NAMProcessor::~NAMProcessor() = default;

void NAMProcessor::loadModel (const std::filesystem::path& namFilePath)
{
    auto model = nam::get_dsp (namFilePath); // throws nam::NamFileValidationError on a bad file

    if (sampleRate > 0.0)
        model->Reset (sampleRate, preparedBlockSize);

    lastLoadedPath = namFilePath;
    modelSlot.publish (std::move (model));
}

juce::String NAMProcessor::getLoadedModelName() const
{
    return lastLoadedPath.empty() ? juce::String() : juce::String (lastLoadedPath.filename().string());
}

void NAMProcessor::clearModel()
{
    modelSlot.publish (nullptr);
    lastLoadedPath.clear();
}

void NAMProcessor::prepare (double newSampleRate, int maxBlockSize, int)
{
    sampleRate = newSampleRate;
    preparedBlockSize = maxBlockSize;

    inputScratch.assign ((size_t) maxBlockSize, 0.0f);
    outputScratch.assign ((size_t) maxBlockSize, 0.0f);

    // A sample-rate/block-size change invalidates an already-Reset() model.
    // Reloading builds a fresh instance and swaps it in atomically, rather
    // than mutating the live one (which the audio thread might be using).
    if (! lastLoadedPath.empty())
        loadModel (lastLoadedPath);
}

void NAMProcessor::reset()
{
    // nam::DSP's internal state (WaveNet/LSTM hidden state) isn't safe to
    // reach into from here without risking a race with the audio thread --
    // see prepare()'s reload-on-change comment for how state actually resets.
}

void NAMProcessor::process (juce::AudioBuffer<float>& buffer)
{
    auto* model = modelSlot.currentRaw();
    const int numSamples = buffer.getNumSamples();

    if (model == nullptr)
        return; // no model loaded -- pass through unchanged

    const float inGain = juce::Decibels::decibelsToGain (inputGainDb->get());
    const float outGain = juce::Decibels::decibelsToGain (outputGainDb->get());

    const auto* inChannel = buffer.getReadPointer (0);
    for (int i = 0; i < numSamples; ++i)
        inputScratch[(size_t) i] = inChannel[i] * inGain;

    float* inPtrs[1] = { inputScratch.data() };
    float* outPtrs[1] = { outputScratch.data() };
    model->process (inPtrs, outPtrs, numSamples);

    // outputScratch * outGain is identical for every output channel -- scale
    // once into itself, then copy, instead of redoing the multiply per channel.
    for (int i = 0; i < numSamples; ++i)
        outputScratch[(size_t) i] *= outGain;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, outputScratch.data(), numSamples);
}

std::unique_ptr<juce::XmlElement> NAMProcessor::getState() const
{
    auto xml = EffectProcessor::getState(); // base: input/output gain

    if (! lastLoadedPath.empty())
        xml->setAttribute ("modelPath", juce::String (lastLoadedPath.string()));

    return xml;
}

void NAMProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state); // base: input/output gain

    const auto path = state.getStringAttribute ("modelPath");
    if (path.isEmpty())
        return;

    try
    {
        loadModel (std::filesystem::path (path.toStdString()));
    }
    catch (const std::exception&)
    {
        // The file may have moved or been deleted since the preset was
        // saved -- leave this block unloaded rather than fail the whole
        // preset load over one missing model.
    }
}

void NAMProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/neura_chip.svg
    // (Neural Amp + Neural Pedal -- the sheet draws both as the same bare
    // chip) and neura_chip_cab.svg (Neural Amp + Cab: chip on top of a cab
    // box). Embedded SVGs, not hand-transcribed juce::Path calls -- an
    // earlier version of this function reimplemented the chip's geometry
    // by eye and drifted from the approved proportions (box inset, pin
    // length) in the process; see IconKit.h.
    if (isAmpCabRole())
    {
        static const std::unique_ptr<juce::Drawable> svg =
            icon::loadSvg (IconData::neura_chip_cab_svg, IconData::neura_chip_cab_svgSize);
        icon::drawSvg (g, b, svg.get());
        return;
    }

    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::neura_chip_svg, IconData::neura_chip_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
