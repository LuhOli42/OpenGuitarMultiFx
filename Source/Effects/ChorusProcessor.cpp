#include "ChorusProcessor.h"
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
    delayBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    reset();
}

void ChorusProcessor::reset()
{
    delayBuffer.clear();
    writePos = 0;
    lfoPhase = 0.0;
}

void ChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || delayBuffer.getNumSamples() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();

    const double phaseIncrement = twoPi * (double) rateHz->get() / currentSampleRate;
    const float depthMs = depth->get() * maxDepthMs;
    const float wet = mix->get();

    for (int i = 0; i < numSamples; ++i)
    {
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
            delayData[writePos] = input; // no feedback -- chorus, not flanger

            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        if (++writePos >= bufferLength)
            writePos = 0;
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
