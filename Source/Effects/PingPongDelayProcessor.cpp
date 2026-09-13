#include "PingPongDelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

PingPongDelayProcessor::PingPongDelayProcessor()
{
    auto time = std::make_unique<juce::AudioParameterFloat> (
        "pingpong_time", "Time",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 280.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "pingpong_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "pingpong_mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);

    timeMs = time.get();
    feedback = fb.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "pingpong", "Ping Pong", "|",
        std::move (time), std::move (fb), std::move (mixParam));
}

void PingPongDelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;
    preparedNumChannels = numChannels;

    const int bufferLength = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4;
    delayBuffer.setSize (2, bufferLength, false, true, true); // always stereo -- see class doc

    smoothedDelaySamples.reset (sampleRate, 0.03);
    smoothedFeedback.reset (sampleRate, 0.03);
    smoothedMix.reset (sampleRate, 0.03);

    smoothedDelaySamples.setCurrentAndTargetValue ((float) (timeMs->get() * 0.001 * sampleRate));
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void PingPongDelayProcessor::reset()
{
    delayBuffer.clear();
    writePos = 0;
}

void PingPongDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (preparedNumChannels < 2 || buffer.getNumChannels() < 2 || currentSampleRate <= 0.0)
        return; // nothing to bounce between -- pass through unchanged

    smoothedDelaySamples.setTargetValue ((float) (timeMs->get() * 0.001 * currentSampleRate));
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());

    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    auto* lineL = delayBuffer.getWritePointer (0);
    auto* lineR = delayBuffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float delaySamples = smoothedDelaySamples.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();

        float readPos = (float) writePos - delaySamples;
        while (readPos < 0.0f)
            readPos += (float) bufferLength;

        const int readIndex0 = (int) readPos;
        const int readIndex1 = (readIndex0 + 1 >= bufferLength) ? 0 : readIndex0 + 1;
        const float frac = readPos - (float) readIndex0;

        const float delayedL = lineL[readIndex0] + frac * (lineL[readIndex1] - lineL[readIndex0]);
        const float delayedR = lineR[readIndex0] + frac * (lineR[readIndex1] - lineR[readIndex0]);

        const float inputL = left[i];
        const float inputR = right[i];

        // The cross-feed: left's line is fed by the RIGHT channel's dry
        // input plus feedback from the right line's own repeat (and vice
        // versa) -- crossing only the feedback and not the dry input would
        // make a left-only signal's FIRST echo land on the left (wrong;
        // confirmed by a failing test before this fix), since nothing
        // would be in the opposite line yet to bounce off of. Crossing the
        // dry feed too means a left-only input's first repeat lands on the
        // right, exactly as a real ping-pong pedal bounces it.
        lineL[writePos] = inputR + fb * delayedR;
        lineR[writePos] = inputL + fb * delayedL;

        left[i] = inputL * (1.0f - wet) + delayedL * wet;
        right[i] = inputR * (1.0f - wet) + delayedR * wet;

        if (++writePos >= bufferLength)
            writePos = 0;
    }
}

void PingPongDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/ping_pong.svg --
    // the unified icon set's "Ping Pong" glyph (Delay category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::ping_pong_svg, IconData::ping_pong_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
