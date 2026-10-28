#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"
#include "WetLevelMatcher.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    Same comb/allpass family as HallReverbProcessor, but 4 short combs
    (7-17ms) and 2 short allpass stages instead of Hall's 8 long combs
    (25-37ms) and 4 allpass -- a small enclosed space's tight, fast early
    reflections rather than a hall's dense, slow-building wash. Decay is
    also capped lower (max feedback ~0.9 vs Hall's ~0.98) since a real
    small room's reflections die out in a few hundred ms, not seconds.
*/
class RoomReverbProcessor : public EffectProcessor
{
public:
    RoomReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Room"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* mix = nullptr;
    juce::AudioParameterFloat* killDry = nullptr;
    std::array<WetLevelMatcher, 2> wetMatch; // keeps Mix = 1 as loud as the dry signal (docs/circuits/MixLaw.md)

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDecay;
    juce::SmoothedValue<float> smoothedTone;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedDry; // the dry path stays at unity; Kill Dry ramps it out
};

} // namespace openguitarmultifx
