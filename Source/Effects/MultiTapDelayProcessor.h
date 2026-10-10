#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    ONE delay line read at four fixed points at once (ratios of a single
    Time knob: 0.25x/0.5x/0.75x/1x, decreasing level per tap) -- not
    several independent lines summed (that's DualDelayProcessor). Only the
    longest (1x) tap feeds back into the buffer, so the whole rhythmic
    pattern of taps repeats together on each feedback cycle rather than
    each tap decaying independently.
*/
class MultiTapDelayProcessor : public EffectProcessor
{
public:
    MultiTapDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Multi Tap"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d72b8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 2000.0f;
    static constexpr int numTaps = 4;
    static constexpr std::array<float, numTaps> tapRatios { 0.25f, 0.5f, 0.75f, 1.0f };
    static constexpr std::array<float, numTaps> tapLevels { 1.0f, 0.75f, 0.55f, 0.4f };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeMs = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDelaySamples;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
