#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/DelayLine.h"

namespace openguitarmultifx
{

/**
    The same modulated-delay-line core as Chorus/Vibrato, but swept over a
    much shorter range (sub-10ms, versus Chorus's ~15ms centre) and WITH
    feedback -- that combination is what turns a modulated delay into the
    metallic, resonant "jet plane" sweep instead of a chorus-style
    doubling. Feedback can go negative, which real flangers support too
    (a different, hollower tonal colour than positive feedback).
*/
class FlangerProcessor : public EffectProcessor
{
public:
    FlangerProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Flanger"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float centreDelayMs = 4.0f;
    static constexpr float maxDepthMs = 3.5f;
    static constexpr float bufferHeadroomMs = 20.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    FractionalDelayLine line;
    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
