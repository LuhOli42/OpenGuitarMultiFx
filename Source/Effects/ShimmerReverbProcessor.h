#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"
#include "WetLevelMatcher.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A comb/allpass reverb tank (same family as HallReverbProcessor, just
    4 combs + 2 allpass instead of 8+4 -- a smaller tank, since the
    interesting part here isn't the space size) whose feedback loop also
    gets a fraction of an octave-up-shifted copy of itself injected back
    in every cycle. That's what makes a shimmer reverb: each pass through
    the tank comes back both as itself and pitched up an octave, building
    an ascending, bell-like halo on top of an ordinary tail -- Shimmer
    controls how much of that shifted copy gets fed back in (0 = an
    ordinary small reverb).

    The pitch shifter is a classic two-grain time-domain octave doubler:
    two triangular-windowed read pointers into a short circular buffer,
    each sweeping backward through it at 2x the write rate (so what comes
    back out is being "replayed" twice as fast = an octave up), 180
    degrees out of phase so one grain's window fade-out is masked by the
    other's fade-in.
*/
class ShimmerReverbProcessor : public EffectProcessor
{
public:
    ShimmerReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Shimmer"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    // Two-grain octave-up doubler -- see the class doc comment above.
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* shimmerAmount = nullptr;
    juce::AudioParameterFloat* mix = nullptr;
    std::array<WetLevelMatcher, 2> wetMatch; // keeps Mix = 1 as loud as the dry signal (docs/circuits/MixLaw.md)

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;
    std::array<TwoGrainShifter, 2> shifters;
    std::array<float, 2> lastCombSum {};

    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDecay;
    juce::SmoothedValue<float> smoothedShimmerAmount;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
