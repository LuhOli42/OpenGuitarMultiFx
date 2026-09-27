#include "ReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

ReverbProcessor::ReverbProcessor()
{
    auto sizeParam = std::make_unique<juce::AudioParameterFloat> (
        "ambient_size", "Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto dampingParam = std::make_unique<juce::AudioParameterFloat> (
        "ambient_damping", "Damping", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "ambient_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f);
    auto widthParam = std::make_unique<juce::AudioParameterFloat> (
        "ambient_width", "Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);

    size = sizeParam.get();
    damping = dampingParam.get();
    mix = mixParam.get();
    width = widthParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "ambient", "Ambient", "|",
        std::move (sizeParam), std::move (dampingParam), std::move (mixParam), std::move (widthParam));
}

void ReverbProcessor::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (1, numChannels) };
    reverb.prepare (spec);
    dryScratch.setSize (juce::jmax (1, numChannels), juce::jmax (1, maxBlockSize), false, false, true);
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 1.43f);
    updateReverbParameters();
    reset();
}

void ReverbProcessor::reset()
{
    for (auto& m : wetMatch)
        m.reset();
    reverb.reset();
}

void ReverbProcessor::updateReverbParameters()
{
    juce::dsp::Reverb::Parameters params;
    params.roomSize = size->get();
    params.damping = damping->get();
    // juce::dsp::Reverb scales its wet level by 3 and its dry level by 2 internally: with the knob's own numbers Mix = 0 came out
    // 6 dB LOUDER than the input. The reverb here runs fully wet at unity (wetLevel 1/3, dryLevel 0) and the dry/wet crossfade is
    // done in process(), with the wet path level-matched to the dry (WetLevelMatcher).
    params.wetLevel = 1.0f / 3.0f;
    params.dryLevel = 0.0f;
    params.width = width->get();
    reverb.setParameters (params);
}

void ReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    updateReverbParameters(); // cheap struct assignment, no allocation -- safe every block

    const int numChannels = juce::jmin (2, buffer.getNumChannels(), dryScratch.getNumChannels());
    const int numSamples = juce::jmin (buffer.getNumSamples(), dryScratch.getNumSamples());
    for (int ch = 0; ch < numChannels; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context); // the buffer is now the wet signal

    const float wet = mix->get();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            wetMatch[(size_t) ch].accumulate (dry[i], data[i]);
            data[i] = dry[i] * (1.0f - wet) + data[i] * wet * wetMatch[(size_t) ch].gain();
        }
    }
    for (int ch = 0; ch < numChannels; ++ch)
        wetMatch[(size_t) ch].endBlock (numSamples, 1);
}

void ReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/ambient.svg --
    // the unified icon set's "Ambient" glyph (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::ambient_svg, IconData::ambient_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
