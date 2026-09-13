#include "VibratoProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

VibratoProcessor::VibratoProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "vibrato_rate", "Rate", juce::NormalisableRange<float> (0.5f, 10.0f, 0.0f, 0.5f), 5.0f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "vibrato_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "vibrato", "Vibrato", "|",
        std::move (rate), std::move (depthParam));
}

void VibratoProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (bufferHeadroomMs * 0.001 * sampleRate) + 4;
    delayBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    reset();
}

void VibratoProcessor::reset()
{
    delayBuffer.clear();
    writePos = 0;
    lfoPhase = 0.0;
}

void VibratoProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || delayBuffer.getNumSamples() == 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();

    const double phaseIncrement = twoPi * (double) rateHz->get() / currentSampleRate;
    const float depthMs = depth->get() * maxDepthMs;

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
            delayData[writePos] = input;

            data[i] = delayed; // 100% wet -- see class doc comment
        }

        if (++writePos >= bufferLength)
            writePos = 0;
        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void VibratoProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/vibrato.svg -- one
    // wave whose own period wobbles (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::vibrato_svg, IconData::vibrato_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
