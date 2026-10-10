#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A feedback delay line with a linearly-interpolated fractional read
    position (so twisting Time doesn't zipper/click) and smoothed
    Time/Feedback/Mix (so parameter changes don't click either). One
    circular buffer sized for the maximum delay time, allocated in
    prepare() -- process() never allocates.
*/
class DelayProcessor : public EffectProcessor
{
public:
    DelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Digital Delay"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d72b8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 2000.0f;

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
