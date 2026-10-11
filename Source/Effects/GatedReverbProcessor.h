#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"
#include "WetLevelMatcher.h"
#include "EnvelopeFollower.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A lush comb/allpass tank (same family as HallReverbProcessor, 4 combs +
    2 allpass, feedback tuned for a noticeably big tail) with a noise-gate
    (EnvelopeFollower, same primitive GateProcessor uses) wrapped around
    the WET signal only. The gate opens the instant the dry input crosses
    the threshold, stays open for Hold milliseconds after the input drops
    back below it, then closes fast -- chopping the reverb tail off
    abruptly instead of letting it decay naturally. That hard cut is the
    entire "gated reverb" effect (classic 80s drum sound); the dry signal
    itself is never gated, only what's blended in on top of it.
*/
class GatedReverbProcessor : public EffectProcessor
{
public:
    GatedReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Gated"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* holdMs = nullptr;
    juce::AudioParameterFloat* mix = nullptr;
    std::array<WetLevelMatcher, 2> wetMatch; // keeps Mix = 1 as loud as the dry signal (docs/circuits/MixLaw.md)

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    EnvelopeFollower inputDetector;
    EnvelopeFollower gateSmoother;
    double holdRemainingSamples = 0.0;
    bool gateOpen = false;

    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDecay;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
