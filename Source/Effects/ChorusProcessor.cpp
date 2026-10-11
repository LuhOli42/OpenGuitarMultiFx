#include "ChorusProcessor.h"
#include "FastMath.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

ChorusProcessor::ChorusProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "chorus_rate", "Rate", juce::NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.5f), 0.8f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "chorus_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "chorus_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "chorus", "Chorus", "|",
        std::move (rate), std::move (depthParam), std::move (mixParam));
}

void ChorusProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (bufferHeadroomMs * 0.001 * sampleRate) + 4;
    line.prepare (numChannels, bufferLength);

    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void ChorusProcessor::reset()
{
    line.clear();
    lfoPhase = 0.0;
}

void ChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || line.getLength() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), line.buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedMix.setTargetValue (mix->get());
    const double phaseIncrement = twoPi * (double) smoothedRateHz.skip (numSamples) / currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const float delayMs = centreDelayMs + smoothedDepth.getNextValue() * maxDepthMs * fast::sinRadians (lfoPhase);
        const float wet = smoothedMix.getNextValue();
        const float delaySamples = delayMs * 0.001f * (float) currentSampleRate;

        int readIndex0, readIndex1;
        float frac;
        line.getReadIndices (delaySamples, readIndex0, readIndex1, frac);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delayData = line.getChannelData (ch);
            const float input = data[i];

            const float delayed = FractionalDelayLine::interpolate (delayData, readIndex0, readIndex1, frac);
            delayData[line.writePos] = input; // no feedback -- chorus, not flanger

            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        line.advance();
        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void ChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/chorus.svg -- two
    // near-identical offset waves (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
