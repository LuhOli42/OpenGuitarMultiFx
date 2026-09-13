#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A BBD-style delay: same fractional-read feedback line as DelayProcessor,
    but the feedback path runs through a one-pole lowpass (Tone) and a soft
    tanh saturator before being fed back. Each repeat comes back darker and
    a touch compressed, the analog bucket-brigade character -- distinct
    from DelayProcessor's clean unfiltered repeats and from
    TapeDelayProcessor's wow/flutter pitch modulation (this delay's timing
    never wavers, only the timbre changes).
*/
class AnalogDelayProcessor : public EffectProcessor
{
public:
    AnalogDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Analog Delay"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d72b8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 2000.0f;
    static constexpr float saturationDrive = 1.4f; // tanh() input gain on the feedback path

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeMs = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double currentSampleRate = 0.0;

    // Feedback-loop lowpass state, one per channel -- shared state across
    // channels would leak left/right repeats into each other.
    std::array<float, 2> filterState {};

    juce::SmoothedValue<float> smoothedDelaySamples;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
