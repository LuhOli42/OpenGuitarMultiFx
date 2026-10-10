#pragma once

#include "EffectProcessor.h"
#include "EnvelopeFollower.h"

namespace openguitarmultifx
{

/**
    Simple noise gate: a fast internal level detector decides whether the
    input is above or below the threshold, and a second envelope follower
    (this one driven by the user-facing Attack/Release parameters) smooths
    the resulting 0/1 gate gain so opening and closing never clicks.
*/
class GateProcessor : public EffectProcessor
{
public:
    GateProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Noise Gate"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2b6ea8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* thresholdDb = nullptr;
    juce::AudioParameterFloat* attackMs = nullptr;
    juce::AudioParameterFloat* releaseMs = nullptr;

    EnvelopeFollower levelDetector; // fixed, fast timing -- not user-exposed
    EnvelopeFollower gainSmoother;  // timing == the Attack/Release parameters above

    juce::SmoothedValue<float> smoothedThresholdDb;
    juce::SmoothedValue<float> smoothedAttackMs;
    juce::SmoothedValue<float> smoothedReleaseMs;
};

} // namespace openguitarmultifx
