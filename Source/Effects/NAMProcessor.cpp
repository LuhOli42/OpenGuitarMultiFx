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

    // Page 2 of an amplifier: a behavioural supply sag (see SagEmulator.h). A pedal has no supply to droop.
    if (! isPedalRole())
    {
        auto sagParam = std::make_unique<juce::AudioParameterFloat> (
            "nam_sag", "Sag", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
        sagAmount = sagParam.get();
        parameters->addChild (std::make_unique<juce::AudioProcessorParameterGroup> ("nam_page2", "Page 2", "|", std::move (sagParam)));
    }

    startTimer (100); // sweeps modelSlot -- see DeferredReclaimer
}

NAMProcessor::~NAMProcessor() = default;

void NAMProcessor::loadModel (const std::filesystem::path& namFilePath)
{
    loadModelInternal (namFilePath, true);
}

void NAMProcessor::loadModelInternal (const std::filesystem::path& namFilePath, bool applyNormalization)
{
    auto model = nam::get_dsp (namFilePath); // throws nam::NamFileValidationError on a bad file

    if (sampleRate > 0.0)
        model->Reset (sampleRate, preparedBlockSize);

    if (applyNormalization)
        normalizeLevels (*model);

    lastLoadedPath = namFilePath;
    modelSlot.publish (std::move (model));
}

void NAMProcessor::normalizeLevels (nam::DSP& model)
{
    // Control thread only (loadModelInternal) -- the scratch buffers and the
    // calibration render itself never run on the audio path.
    // .nam metadata describes the ANALOG reference the capture was trained
    // at (input/output levels in dBu against a real amp's signal chain), not
    // the digital level the model actually emits -- captures on TONE3000
    // routinely come out tens of dB off unity. The reliable reference is a
    // measured response: push a DI-level probe through the fresh model and
    // trim the output so it lands back at DI level. The input trim stays at
    // 0 dB -- a capture expects instrument-level in, which is what the
    // chain feeds it.
    const double calibRate = sampleRate > 0.0 ? sampleRate
                           : (model.GetExpectedSampleRate() > 0.0 ? model.GetExpectedSampleRate() : 48000.0);
    const int block = preparedBlockSize > 0 ? preparedBlockSize : 512;

    model.Reset (calibRate, block); // Reset() prewarms by default

    const int signalLen = (int) (0.2 * calibRate); // 200 ms
    std::vector<float> in ((size_t) signalLen), out ((size_t) signalLen);
    for (int i = 0; i < signalLen; ++i)
        in[(size_t) i] = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / calibRate);

    for (int pos = 0; pos < signalLen; pos += block)
    {
        const int n = juce::jmin (block, signalLen - pos);
        float* inPtrs[1] = { in.data() + pos };
        float* outPtrs[1] = { out.data() + pos };
        model.process (inPtrs, outPtrs, n);
    }

    // Second half only: the model's filter/LSTM state has settled by then.
    double sumIn = 0.0, sumOut = 0.0;
    const int half = signalLen / 2;
    for (int i = half; i < signalLen; ++i)
    {
        sumIn += (double) in[(size_t) i] * in[(size_t) i];
        sumOut += (double) out[(size_t) i] * out[(size_t) i];
    }
    const double inRms = std::sqrt (sumIn / half);
    const double outRms = std::sqrt (sumOut / half);

    if (! std::isfinite (outRms) || outRms < 1.0e-7)
        return; // broken or silent model -- leave the trims where they are

    *inputGainDb = 0.0f;
    *outputGainDb = juce::jlimit (-24.0f, 24.0f, (float) juce::Decibels::gainToDecibels (inRms / outRms));

    // Flush the probe's residual filter/LSTM state: the published model must
    // not carry a decaying 220 Hz tail into its first live block.
    std::fill (in.begin(), in.end(), 0.0f);
    for (int pos = 0; pos < signalLen; pos += block)
    {
        const int n = juce::jmin (block, signalLen - pos);
        float* inPtrs[1] = { in.data() + pos };
        float* outPtrs[1] = { out.data() + pos };
        model.process (inPtrs, outPtrs, n);
    }
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
    sag.prepare (newSampleRate);

    // A sample-rate/block-size change invalidates an already-Reset() model.
    // Reloading builds a fresh instance and swaps it in atomically, rather
    // than mutating the live one (which the audio thread might be using).
    if (! lastLoadedPath.empty())
        loadModelInternal (lastLoadedPath, false); // keep the user's trims across a rebuild
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

    if (sagAmount != nullptr)
        sag.process (outputScratch.data(), numSamples, sagAmount->get());

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
    // The model must load BEFORE the base class restores params:
    // loadModel() auto-normalizes the input/output trims, and the saved
    // values then overwrite that -- a preset restores exactly what the
    // user dialed in, while a fresh load still gets calibrated defaults.
    const auto path = state.getStringAttribute ("modelPath");
    if (path.isNotEmpty())
    {
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

    EffectProcessor::setState (state); // base: input/output gain
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
