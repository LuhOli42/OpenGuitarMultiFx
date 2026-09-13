#include "AnalogDelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

AnalogDelayProcessor::AnalogDelayProcessor()
{
    auto time = std::make_unique<juce::AudioParameterFloat> (
        "analogdelay_time", "Time",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 350.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "analogdelay_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "analogdelay_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "analogdelay_mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);

    timeMs = time.get();
    feedback = fb.get();
    tone = toneParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "analogdelay", "Analog Delay", "|",
        std::move (time), std::move (fb), std::move (toneParam), std::move (mixParam));
}

void AnalogDelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4;
    delayBuffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);

    smoothedDelaySamples.reset (sampleRate, 0.03);
    smoothedFeedback.reset (sampleRate, 0.03);
    smoothedMix.reset (sampleRate, 0.03);

    smoothedDelaySamples.setCurrentAndTargetValue ((float) (timeMs->get() * 0.001 * sampleRate));
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void AnalogDelayProcessor::reset()
{
    delayBuffer.clear();
    writePos = 0;
    filterState.fill (0.0f);
}

void AnalogDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || delayBuffer.getNumSamples() == 0)
        return;

    smoothedDelaySamples.setTargetValue ((float) (timeMs->get() * 0.001 * currentSampleRate));
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());

    // Same decay/tone-to-coefficient convention as SpringReverbProcessor's
    // damping, just applied to a single feedback-loop filter instead of a
    // comb: low tone -> small coefficient -> the lowpass barely tracks the
    // signal -> each repeat loses more top end, the classic BBD darkening.
    const float filterCoeff = 0.05f + tone->get() * 0.95f;

    const int numChannels = juce::jmin (buffer.getNumChannels(), delayBuffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    const int bufferLength = delayBuffer.getNumSamples();

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

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delayData = delayBuffer.getWritePointer (ch);

            const float delayed = delayData[readIndex0] + frac * (delayData[readIndex1] - delayData[readIndex0]);
            const float input = data[i];

            auto& lp = filterState[(size_t) ch];
            lp += filterCoeff * (delayed - lp);
            // std::tanh (saturationDrive) is invariant for the process's whole
            // lifetime -- a `static const` computes it once instead of every
            // sample x channel via a full libm call.
            static const float invTanhDrive = 1.0f / std::tanh (saturationDrive);
            const float saturated = std::tanh (lp * saturationDrive) * invTanhDrive;

            delayData[writePos] = input + fb * saturated;
            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        if (++writePos >= bufferLength)
            writePos = 0;
    }
}

void AnalogDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/analog_delay.svg --
    // the "Analog Delay" glyph (Delay category): decaying rounded humps
    // instead of Digital Delay's discrete dots.
    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::analog_delay_svg, IconData::analog_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
