#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A stereo delay where each channel's line is written from the OTHER
    channel's dry input plus its OWN prior repeat (not the same channel's
    dry input) -- a left-only signal's first echo lands on the right, and
    every repeat after that keeps alternating, the classic ping-pong
    bounce. Crossing only the feedback and not the dry input (an earlier
    version of this class did exactly that) makes the FIRST repeat land on
    the same side it came from, which isn't ping-pong -- caught by a test
    checking which channel the first echo actually lands in, not just that
    a delay happens. Genuinely different topology from DelayProcessor's
    independent per-channel lines, not "DelayProcessor run twice".
    Inherently a stereo effect: with fewer than 2 channels there's nothing
    to bounce between, so process() passes audio through unchanged then.
*/
class PingPongDelayProcessor : public EffectProcessor
{
public:
    PingPongDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Ping Pong"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4779c4); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 1500.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeMs = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::AudioBuffer<float> delayBuffer; // channel 0 = left line, channel 1 = right line
    int writePos = 0;
    int preparedNumChannels = 0;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDelaySamples;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
