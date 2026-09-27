#include "IRLoaderProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

IRLoaderProcessor::IRLoaderProcessor (juce::String chainRoleName)
    : name (std::move (chainRoleName))
{
    auto mix = std::make_unique<juce::AudioParameterFloat> (
        "ir_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);
    auto output = std::make_unique<juce::AudioParameterFloat> (
        "ir_output", "Output", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f);

    mixParam = mix.get();
    outputGainDb = output.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ir", name, "|", std::move (mix), std::move (output));

    startTimer (100); // sweeps irSlot -- see DeferredReclaimer
}

IRLoaderProcessor::~IRLoaderProcessor() = default;

void IRLoaderProcessor::loadImpulseResponse (const juce::File& irFile)
{
    auto conv = std::make_unique<juce::dsp::Convolution>();

    if (sampleRate > 0.0)
    {
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) preparedNumChannels };
        conv->prepare (spec);
    }

    // A guitar cabinet's impulse response is over within ~100 ms; what follows in a captured file is a noise floor
    // and room that no one hears, and convolving it is most of the cost (a 4 s file cost 18% of a core, the first
    // 150 ms about a tenth of that). A reverb IS its tail, so only the Cab role is capped.
    const int maxSamples = isReverbRole() || sampleRate <= 0.0 ? 0 : (int) (0.15 * sampleRate);
    conv->loadImpulseResponse (irFile,
                                juce::dsp::Convolution::Stereo::yes,
                                juce::dsp::Convolution::Trim::yes,
                                (size_t) maxSamples, // 0 -- use the whole file
                                juce::dsp::Convolution::Normalise::yes);

    lastLoadedFile = irFile;
    loadedName = irFile.getFileName();
    irSlot.publish (std::move (conv));
}

void IRLoaderProcessor::clearImpulseResponse()
{
    irSlot.publish (nullptr);
    lastLoadedFile = juce::File();
    loadedName.clear();
}

void IRLoaderProcessor::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    preparedBlockSize = maxBlockSize;
    preparedNumChannels = juce::jmax (1, numChannels);

    dryScratch.setSize (preparedNumChannels, maxBlockSize, false, false, true);

    // JUCE normalises a loaded IR to 0.125 / sqrt (sum of squares), i.e. an energy gain of -18 dB: a "Reverb" wet path is 18 dB
    // under the dry until this brings it back (the starting value; WetLevelMatcher then follows the actual IR and signal).
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 1.0f / 0.125f);

    // Same reasoning as NAMProcessor::prepare(): a sample-rate/block-size
    // change invalidates an already-prepared Convolution, so reloading
    // builds a fresh instance and swaps it in atomically rather than
    // mutating the live one.
    if (lastLoadedFile.existsAsFile())
        loadImpulseResponse (lastLoadedFile);
}

void IRLoaderProcessor::reset()
{
    // Convolution's internal tail state isn't safe to reach into from here
    // without risking a race with the audio thread -- see prepare()'s
    // reload-on-change comment for how state actually resets.
}

void IRLoaderProcessor::process (juce::AudioBuffer<float>& buffer)
{
    auto* conv = irSlot.currentRaw();

    if (conv == nullptr)
        return; // no IR loaded -- pass through unchanged

    const int numSamples = buffer.getNumSamples();
    const float mix = mixParam->get();
    const bool matchWet = isReverbRole(); // a Cab keeps its own level; a reverb's wet is level-matched to the dry
    const bool needsDryBlend = mix < 0.999f || matchWet;

    if (needsDryBlend)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    conv->process (context);

    if (matchWet)
    {
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto* dry = dryScratch.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                wetMatch[(size_t) ch].accumulate (dry[i], data[i]);
                data[i] = dry[i] * (1.0f - mix) + data[i] * mix * wetMatch[(size_t) ch].gain();
            }
        }
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            wetMatch[(size_t) ch].endBlock (numSamples, 1);
    }
    else if (needsDryBlend)
    {
        buffer.applyGain (mix);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, dryScratch, ch, 0, numSamples, 1.0f - mix);
    }

    const float outGain = juce::Decibels::decibelsToGain (outputGainDb->get());
    if (outGain != 1.0f)
        buffer.applyGain (outGain);
}

juce::Colour IRLoaderProcessor::getAccentColour() const
{
    return isReverbRole() ? juce::Colour (0xff2d7a9e) : juce::Colour (0xff7a5c2d);
}

std::unique_ptr<juce::XmlElement> IRLoaderProcessor::getState() const
{
    auto xml = EffectProcessor::getState(); // base: mix/output gain

    if (lastLoadedFile != juce::File())
        xml->setAttribute ("irPath", lastLoadedFile.getFullPathName());

    return xml;
}

void IRLoaderProcessor::setState (const juce::XmlElement& state)
{
    EffectProcessor::setState (state); // base: mix/output gain

    const auto path = state.getStringAttribute ("irPath");
    if (path.isEmpty())
        return;

    const juce::File file (path);
    if (file.existsAsFile())
        loadImpulseResponse (file);
    // else: the file moved or was deleted since the preset was saved --
    // leave this block unloaded rather than fail the whole preset load.
}

void IRLoaderProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // Both roles share cab.svg -- embedded SVG, not a hand-transcribed
    // juce::Path call; see IconKit.h for why. The Reverb role used to
    // borrow hall.svg as a stand-in (see docs/icons/AGENT-icon-notes.md),
    // but that glyph now belongs to HallReverbProcessor's real algorithmic
    // "Hall" block, so this generic loaded-IR box is the reuse until the
    // reference sheet's own grey "IR Loader" utility glyph gets confirmed.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::cab_svg, IconData::cab_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
