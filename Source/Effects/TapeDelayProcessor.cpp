#include "TapeDelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

TapeDelayProcessor::TapeDelayProcessor()
{
    auto time = std::make_unique<juce::AudioParameterFloat> (
        "tapedelay_time", "Time",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 350.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "tapedelay_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "tapedelay_mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);

    timeMs = time.get();
    registerTempoSyncParam (timeMs);
    feedback = fb.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "tapedelay", "Tape Delay", "|",
        std::move (time), std::move (fb), std::move (mixParam));
}

void TapeDelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    // +wowDepthMs of extra headroom so the wobbling read position never
    // reads past the write position even at the shortest configured delay.
    const int bufferLength = (int) std::ceil ((maxDelayMs + wowDepthMs) * 0.001 * sampleRate) + 4;
    line.prepare (numChannels, bufferLength);

    smoothedDelaySamples.reset (sampleRate, 0.03);
    smoothedFeedback.reset (sampleRate, 0.03);
    smoothedMix.reset (sampleRate, 0.03);

    smoothedDelaySamples.setCurrentAndTargetValue ((float) (timeMs->get() * 0.001 * sampleRate));
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void TapeDelayProcessor::reset()
{
    line.clear();
    wowPhase = 0.0f;
}

void TapeDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || line.getLength() == 0)
        return;

    smoothedDelaySamples.setTargetValue ((float) (timeMs->get() * 0.001 * currentSampleRate));
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());

    const int numChannels = juce::jmin (buffer.getNumChannels(), line.buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    const float wowDepthSamples = wowDepthMs * 0.001f * (float) currentSampleRate;
    const float wowPhaseInc = juce::MathConstants<float>::twoPi * wowRateHz / (float) currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const float baseDelaySamples = smoothedDelaySamples.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();

        const float wobble = std::sin (wowPhase) * wowDepthSamples;
        wowPhase += wowPhaseInc;
        if (wowPhase > juce::MathConstants<float>::twoPi)
            wowPhase -= juce::MathConstants<float>::twoPi;

        int readIndex0, readIndex1;
        float frac;
        line.getReadIndices (baseDelaySamples + wobble, readIndex0, readIndex1, frac);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delayData = line.getChannelData (ch);

            const float delayed = FractionalDelayLine::interpolate (delayData, readIndex0, readIndex1, frac);
            const float input = data[i];

            // Soft-saturate what feeds back, not the dry input or the wet
            // output -- successive repeats warm up and compress the way a
            // physical tape's repeated print-throughs do, without coloring
            // the very first (loudest, most audible) echo as heavily.
            // std::tanh (saturationDrive) is invariant for the process's whole
            // lifetime -- a `static const` computes it once instead of every
            // sample x channel via a full libm call.
            static const float invTanhDrive = 1.0f / std::tanh (saturationDrive);
            const float saturatedFeedback = std::tanh (delayed * saturationDrive) * invTanhDrive;
            delayData[line.writePos] = input + fb * saturatedFeedback;

            data[i] = input * (1.0f - wet) + delayed * wet;
        }

        line.advance();
    }
}

void TapeDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/tape_delay.svg --
    // the unified icon set's "Tape Delay" glyph (Delay category).
    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::tape_delay_svg, IconData::tape_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
