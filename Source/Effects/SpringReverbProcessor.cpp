#include "SpringReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

SpringReverbProcessor::SpringReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_decay", "Decay", juce::NormalisableRange<float> (0.0f, 0.97f), 0.7f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "spring_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f);

    decay = decayParam.get();
    tone = toneParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "spring", "Spring", "|",
        std::move (decayParam), std::move (toneParam), std::move (mixParam));
}

void SpringReverbProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;

    // Three ascending allpass delays per channel -- the classic Schroeder
    // dispersion chain that gives the metallic, "smeared" spring timbre.
    static constexpr float allpassDelaysMs[numAllpassStages] = { 3.1f, 5.3f, 7.9f };

    for (auto& channelStages : allpass)
        for (int i = 0; i < numAllpassStages; ++i)
        {
            channelStages[(size_t) i].prepare (sampleRate, allpassDelaysMs[i]);
            channelStages[(size_t) i].feedback = 0.6f;
        }

    // ~45ms comb loop -- long enough to hear as a distinct "boing" repeat,
    // not a smooth wash (that's ReverbProcessor's job).
    for (auto& c : comb)
        c.prepare (sampleRate, 45.0f);

    reset();
}

void SpringReverbProcessor::reset()
{
    for (auto& channelStages : allpass)
        for (auto& stage : channelStages)
            stage.clear();
    for (auto& c : comb)
        c.clear();
}

void SpringReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    const float decayAmount = decay->get();
    // Freeverb-style damped comb: dampCoeff closer to 1 means the loop's
    // one-pole filter tracks the input more slowly -- i.e. darker. Tone is
    // brightness, so invert it for the damping coefficient.
    const float dampCoeff = 1.0f - tone->get();
    const float wet = mix->get();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& stages = allpass[(size_t) ch];
        auto& c = comb[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];

            float allpassOut = input;
            for (auto& stage : stages)
                allpassOut = stage.process (allpassOut);

            const float bufferOut = c.buffer[(size_t) c.pos];
            c.lowpassState = bufferOut * (1.0f - dampCoeff) + c.lowpassState * dampCoeff;
            c.buffer[(size_t) c.pos] = allpassOut + c.lowpassState * decayAmount;
            if (++c.pos >= (int) c.buffer.size())
                c.pos = 0;

            data[i] = input * (1.0f - wet) + bufferOut * wet;
        }
    }
}

void SpringReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/spring.svg -- the
    // unified icon set's "Spring" glyph (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::spring_svg, IconData::spring_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
