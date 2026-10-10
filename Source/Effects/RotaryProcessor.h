#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A simplified Leslie-speaker emulation: a one-pole crossover splits the
    signal into a low band ("drum"/woofer) and a high band ("horn"), each
    independently amplitude-modulated at its OWN rate -- real Leslies spin
    the horn and drum at related but distinct effective speeds, which is
    what gives the characteristic swirl rather than a single uniform
    tremolo. Genuinely different from TremoloProcessor (one band, one
    rate) despite both being pure amplitude modulation underneath.
*/
class RotaryProcessor : public EffectProcessor
{
public:
    RotaryProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Rotary"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float crossoverHz = 800.0f;
    static constexpr float hornRateMultiplier = 1.6f;
    static constexpr float woofRateMultiplier = 0.8f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    double currentSampleRate = 0.0;
    double hornPhase = 0.0;
    double woofPhase = 0.0;
    float crossoverCoeff = 0.0f;
    // One-pole lowpass filter state, per channel -- shared state would
    // leak left/right crossover filtering into each other.
    std::array<float, 2> lowState {};

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
