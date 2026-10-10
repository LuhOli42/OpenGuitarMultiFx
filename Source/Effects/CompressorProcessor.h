#pragma once

#include "EffectProcessor.h"
#include "EnvelopeFollower.h"

namespace openguitarmultifx
{

/**
    Feed-forward compressor with a log-domain static curve: a peak envelope
    follower tracks the input level, and above the threshold the output is
    pulled toward the threshold by 1/ratio, in dB. Attack/Release shape the
    envelope follower itself (the classic topology), not a second gain
    smoother -- there's no separate "target" to smooth toward here.
*/
class CompressorProcessor : public EffectProcessor
{
public:
    CompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6a4c93); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* thresholdDb = nullptr;
    juce::AudioParameterFloat* ratio = nullptr;
    juce::AudioParameterFloat* attackMs = nullptr;
    juce::AudioParameterFloat* releaseMs = nullptr;
    juce::AudioParameterFloat* makeupDb = nullptr;

    EnvelopeFollower detector;

    juce::SmoothedValue<float> smoothedThresholdDb;
    juce::SmoothedValue<float> smoothedRatio;
    juce::SmoothedValue<float> smoothedMakeupDb;
    juce::SmoothedValue<float> smoothedAttackMs;
    juce::SmoothedValue<float> smoothedReleaseMs;
};

} // namespace openguitarmultifx
