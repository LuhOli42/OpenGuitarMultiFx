#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A feedback delay line with tape-flavoured colouration: a slow LFO
    wobbles the read position (wow/flutter, the pitch drift of a physical
    tape transport) and the feedback path is soft-saturated (std::tanh),
    same as DelayProcessor's clean digital line otherwise. No extra
    user-facing parameters over DelayProcessor's Time/Feedback/Mix -- the
    wobble/saturation amounts are fixed, characterful defaults rather than
    more knobs, matching how a real tape delay pedal doesn't expose "wow
    depth" as a control either.
*/
class TapeDelayProcessor : public EffectProcessor
{
public:
    TapeDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Tape Delay"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff5a8fd0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxDelayMs = 2000.0f;
    static constexpr float wowRateHz = 0.6f;      // slow transport-speed drift
    static constexpr float wowDepthMs = 1.2f;      // +/- delay-time wobble
    static constexpr float saturationDrive = 1.8f; // tanh() input gain on the feedback path

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeMs = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double currentSampleRate = 0.0;
    float wowPhase = 0.0f;

    juce::SmoothedValue<float> smoothedDelaySamples;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
