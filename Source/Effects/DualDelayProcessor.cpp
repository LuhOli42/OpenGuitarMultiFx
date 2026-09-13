#include "DualDelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

DualDelayProcessor::DualDelayProcessor()
{
    auto timeAParam = std::make_unique<juce::AudioParameterFloat> (
        "dualdelay_timeA", "Time A",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 250.0f);
    auto timeBParam = std::make_unique<juce::AudioParameterFloat> (
        "dualdelay_timeB", "Time B",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 375.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "dualdelay_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.3f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "dualdelay_mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);

    timeA = timeAParam.get();
    timeB = timeBParam.get();
    registerTempoSyncParam (timeA);
    registerTempoSyncParam (timeB);
    feedback = fb.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "dualdelay", "Dual Delay", "|",
        std::move (timeAParam), std::move (timeBParam), std::move (fb), std::move (mixParam));
}

void DualDelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4;
    for (auto& tap : taps)
        tap.prepare (numChannels, bufferLength);

    smoothedTimeA.reset (sampleRate, 0.03);
    smoothedTimeB.reset (sampleRate, 0.03);
    smoothedFeedback.reset (sampleRate, 0.03);
    smoothedMix.reset (sampleRate, 0.03);

    smoothedTimeA.setCurrentAndTargetValue ((float) (timeA->get() * 0.001 * sampleRate));
    smoothedTimeB.setCurrentAndTargetValue ((float) (timeB->get() * 0.001 * sampleRate));
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void DualDelayProcessor::reset()
{
    for (auto& tap : taps)
        tap.clear();
}

void DualDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || taps[0].buffer.getNumSamples() == 0)
        return;

    smoothedTimeA.setTargetValue ((float) (timeA->get() * 0.001 * currentSampleRate));
    smoothedTimeB.setTargetValue ((float) (timeB->get() * 0.001 * currentSampleRate));
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());

    const int numChannels = juce::jmin (buffer.getNumChannels(), taps[0].buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = taps[0].buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const float delaySamplesA = smoothedTimeA.getNextValue();
        const float delaySamplesB = smoothedTimeB.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();

        // Read position/fraction depend only on each tap's own writePos/
        // delaySamples/bufferLength, not on channel -- computed once per tap
        // here instead of once per (channel, tap), halving this math for
        // stereo.
        const std::array<float, 2> delaySamples { delaySamplesA, delaySamplesB };
        std::array<int, 2> readIndex0 {};
        std::array<int, 2> readIndex1 {};
        std::array<float, 2> frac {};

        for (size_t t = 0; t < taps.size(); ++t)
        {
            float readPos = (float) taps[t].writePos - delaySamples[t];
            while (readPos < 0.0f)
                readPos += (float) bufferLength;

            readIndex0[t] = (int) readPos;
            readIndex1[t] = readIndex0[t] + 1;
            if (readIndex1[t] >= bufferLength)
                readIndex1[t] = 0;
            frac[t] = readPos - (float) readIndex0[t];
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float input = data[i];
            float wetSum = 0.0f;

            for (size_t t = 0; t < taps.size(); ++t)
            {
                auto& tap = taps[t];
                auto* tapData = tap.buffer.getWritePointer (ch);

                const float delayed = tapData[readIndex0[t]] + frac[t] * (tapData[readIndex1[t]] - tapData[readIndex0[t]]);
                tapData[tap.writePos] = input + fb * delayed;
                wetSum += delayed;
            }

            data[i] = input * (1.0f - wet) + (wetSum * 0.5f) * wet;
        }

        for (auto& tap : taps)
            if (++tap.writePos >= bufferLength)
                tap.writePos = 0;
    }
}

void DualDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/dual_delay.svg --
    // the "Dual Delay" glyph (Delay category): two offset dot-rows, two
    // independent taps instead of Digital Delay's single row of three.
    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::dual_delay_svg, IconData::dual_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
