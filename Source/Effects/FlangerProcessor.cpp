#include "FlangerProcessor.h"
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

FlangerProcessor::FlangerProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "flanger_rate", "Rate", juce::NormalisableRange<float> (0.05f, 3.0f, 0.0f, 0.5f), 0.25f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "flanger_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "flanger_feedback", "Feedback", juce::NormalisableRange<float> (-0.9f, 0.9f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "flanger_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();
    feedback = fb.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "flanger", "Flanger", "|",
        std::move (rate), std::move (depthParam), std::move (fb), std::move (mixParam));
}

void FlangerProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (bufferHeadroomMs * 0.001 * sampleRate) + 4;
    line.prepare (numChannels, bufferLength);

    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedFeedback.reset (sampleRate, paramRampSeconds);
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void FlangerProcessor::reset()
{
    line.clear();
    lfoPhase = 0.0;
}

void FlangerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || line.getLength() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), line.buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());
    const double phaseIncrement = twoPi * (double) smoothedRateHz.skip (numSamples) / currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // Centred so the sweep never reaches (or crosses) zero delay --
        // a true zero-crossing would need a discontinuous read index.
        const float delayMs = centreDelayMs + smoothedDepth.getNextValue() * maxDepthMs * fast::sinRadians (lfoPhase);
        const float fb = smoothedFeedback.getNextValue();
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
            delayData[line.writePos] = input + fb * delayed; // feedback -- the metallic resonance, unlike Chorus

            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        line.advance();
        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void FlangerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/flanger.svg -- the
    // same wave twice, one dotted and trailing slightly behind (the
    // literal delayed copy flanging mixes in) (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::flanger_svg, IconData::flanger_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
