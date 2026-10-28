#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"
#include "WetLevelMatcher.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A large-space algorithmic reverb -- the classic Schroeder-Moorer/Freeverb
    architecture (8 parallel damped combs summed, then 4 series allpass
    filters for diffusion), tuned for a big, dense hall rather than
    SpringReverbProcessor's short metallic "boing" (3 allpass into one comb)
    or ReverbProcessor/"Ambient"'s juce::dsp::Reverb wash. The 8 parallel
    combs at staggered lengths is what gives a hall its dense, smooth echo
    build-up instead of a single audible repeat.

    Decay controls each comb's feedback (how long the tail rings); Tone
    controls the in-loop damping (brightness of the tail as it decays).
*/
class HallReverbProcessor : public EffectProcessor
{
public:
    HallReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Hall"; }
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

    static constexpr int numCombs = 8;
    static constexpr int numAllpassStages = 4;
    // Per-channel banks -- shared state across channels would collapse the
    // stereo image entirely (every channel would ring in lockstep).
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedDecay;
    juce::SmoothedValue<float> smoothedTone;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedDry; // the dry path stays at unity; Kill Dry ramps it out
};

} // namespace openguitarmultifx
