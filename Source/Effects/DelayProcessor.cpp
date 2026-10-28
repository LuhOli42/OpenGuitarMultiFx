#include "DelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

DelayProcessor::DelayProcessor()
{
    auto time = std::make_unique<juce::AudioParameterFloat> (
        "delay_time", "Time",
        juce::NormalisableRange<float> (1.0f, maxDelayMs, 0.0f, 0.4f), 350.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "delay_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f), 0.35f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "delay_wet", "Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.54f);

    timeMs = time.get();
    feedback = fb.get();
    mix = mixParam.get();
    auto killDryParam = std::make_unique<juce::AudioParameterFloat> (
        "delay_kill_dry", "Kill Dry", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return v > 0.5f ? juce::String ("On") : juce::String ("Off"); }));
    killDry = killDryParam.get();
    registerLegacyMix (mix, killDry, "delay_mix");
    registerTempoSyncParam (timeMs);

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "delay", "Digital Delay", "|",
        std::move (time), std::move (fb), std::move (mixParam));
    parameters->addChild (std::move (killDryParam));
}

void DelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int bufferLength = (int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4;
    line.prepare (numChannels, bufferLength);

    smoothedDelaySamples.reset (sampleRate, 0.03);
    smoothedFeedback.reset (sampleRate, 0.03);
    smoothedMix.reset (sampleRate, 0.03);
    smoothedDry.reset (sampleRate, 0.02);

    smoothedDelaySamples.setCurrentAndTargetValue ((float) (timeMs->get() * 0.001 * sampleRate));
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.setCurrentAndTargetValue (mix->get());
    smoothedDry.setCurrentAndTargetValue (killDry->get() > 0.5f ? 0.0f : 1.0f);

    reset();
}

void DelayProcessor::reset()
{
    line.clear();
}

void DelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || line.getLength() == 0)
        return;

    smoothedDelaySamples.setTargetValue ((float) (timeMs->get() * 0.001 * currentSampleRate));
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());
    smoothedDry.setTargetValue (killDry->get() > 0.5f ? 0.0f : 1.0f);

    const int numChannels = juce::jmin (buffer.getNumChannels(), line.buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const float delaySamples = smoothedDelaySamples.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float dry = smoothedDry.getNextValue();

        int readIndex0, readIndex1;
        float frac;
        line.getReadIndices (delaySamples, readIndex0, readIndex1, frac);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delayData = line.getChannelData (ch);

            const float delayed = FractionalDelayLine::interpolate (delayData, readIndex0, readIndex1, frac);
            const float input = data[i];

            delayData[line.writePos] = input + fb * delayed;
            data[i] = input * dry + delayed * wet;
        }

        line.advance();
    }
}

void DelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/digital_delay.svg
    // -- the unified icon set's "Digital Delay" glyph (Delay category).
    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::digital_delay_svg, IconData::digital_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
