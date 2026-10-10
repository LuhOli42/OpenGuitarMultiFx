#include "PitchModProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

PitchModProcessor::PitchModProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "pitchmod_rate", "Rate", juce::NormalisableRange<float> (0.05f, 8.0f, 0.0f, 0.5f), 3.0f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "pitchmod_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "pitchmod_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "pitchmod", "Pitch Mod", "|",
        std::move (rate), std::move (depthParam), std::move (mixParam));
}

void PitchModProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    for (auto& shifter : shifters)
        shifter.prepare (sampleRate);
    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void PitchModProcessor::reset()
{
    for (auto& shifter : shifters)
        shifter.clear();
    lfoPhase = 0.0;
}

void PitchModProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedMix.setTargetValue (mix->get());
    const double phaseIncrement = twoPi * (double) smoothedRateHz.skip (numSamples) / currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const float semitones = maxSemitones * smoothedDepth.getNextValue() * (float) std::sin (lfoPhase);
        const float wet = smoothedMix.getNextValue();
        const float ratio = std::pow (2.0f, semitones / 12.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float input = data[i];

            const float shifted = shifters[(size_t) ch].process (input, ratio);
            data[i] = input * (1.0f - wet) + shifted * wet;
        }

        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void PitchModProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/pitch_mod.svg -- a
    // wave with arrows pointing both up and down (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pitch_mod_svg, IconData::pitch_mod_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
