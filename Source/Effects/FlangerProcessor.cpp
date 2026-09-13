#include "FlangerProcessor.h"
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
    delayBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    reset();
}

void FlangerProcessor::reset()
{
    delayBuffer.clear();
    writePos = 0;
    lfoPhase = 0.0;
}

void FlangerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || delayBuffer.getNumSamples() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();

    const double phaseIncrement = twoPi * (double) rateHz->get() / currentSampleRate;
    const float depthMs = depth->get() * maxDepthMs;
    const float fb = feedback->get();
    const float wet = mix->get();

    for (int i = 0; i < numSamples; ++i)
    {
        // Centred so the sweep never reaches (or crosses) zero delay --
        // a true zero-crossing would need a discontinuous read index.
        const float delayMs = centreDelayMs + depthMs * (float) std::sin (lfoPhase);
        const float delaySamples = delayMs * 0.001f * (float) currentSampleRate;

        float readPos = (float) writePos - delaySamples;
        while (readPos < 0.0f)
            readPos += (float) bufferLength;

        const int readIndex0 = (int) readPos;
        const int readIndex1 = (readIndex0 + 1 >= bufferLength) ? 0 : readIndex0 + 1;
        const float frac = readPos - (float) readIndex0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delayData = delayBuffer.getWritePointer (ch);
            const float input = data[i];

            const float delayed = delayData[readIndex0] + frac * (delayData[readIndex1] - delayData[readIndex0]);
            delayData[writePos] = input + fb * delayed; // feedback -- the metallic resonance, unlike Chorus

            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        if (++writePos >= bufferLength)
            writePos = 0;
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
