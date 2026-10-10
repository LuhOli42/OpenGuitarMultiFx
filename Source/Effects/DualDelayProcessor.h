#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    Two independent single-tap feedback delay lines (own time/feedback each)
    summed at the output -- a genuinely different structure from
    DelayProcessor's one tap, not the same delay with an extra knob. Each
    tap is its own fractional-read circular buffer, so Tap A and Tap B can
    land anywhere in time relative to each other (rhythmic subdivisions,
    doubling, etc.) rather than being locked to a ratio.
*/
class DualDelayProcessor : public EffectProcessor
{
public:
    DualDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Dual Delay"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d72b8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 2000.0f;

    struct Tap
    {
        juce::AudioBuffer<float> buffer;
        int writePos = 0;

        void prepare (int numChannels, int bufferLength)
        {
            buffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);
            writePos = 0;
        }

        void clear() { buffer.clear(); writePos = 0; }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeA = nullptr;
    juce::AudioParameterFloat* timeB = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<Tap, 2> taps;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedTimeA;
    juce::SmoothedValue<float> smoothedTimeB;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
