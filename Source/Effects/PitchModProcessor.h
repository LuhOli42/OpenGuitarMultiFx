#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    True pitch-bending, not a modulated delay line. The two-grain
    time-domain shifter from ShimmerReverbProcessor::OctaveUpShifter,
    generalised to a continuously-variable ratio instead of a fixed
    octave: an LFO sweeps the playback ratio up and down (musically, in
    semitones) each sample, so what comes out is genuinely re-pitched --
    Chorus/Vibrato/Flanger only ever LOOK like pitch modulation as a side
    effect of a moving delay time, which is audibly gentler and can't
    cover a wide musical interval the way real pitch-shifting can.
*/
class PitchModProcessor : public EffectProcessor
{
public:
    PitchModProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Pitch Mod"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    // Two-grain doubler generalised to a continuously variable ratio --
    // see the class doc comment. `delayA -= (ratio - 1)` per sample: at
    // ratio=1 the delay never moves (no shift); ratio>1 decreases delay
    // (reading forward through history faster = pitch up); ratio<1
    // increases it (pitch down). Wrapping in BOTH directions is needed
    // here, unlike the fixed-ratio-always->1 case in ShimmerReverbProcessor.
    static constexpr float maxSemitones = 3.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<TwoGrainShifter, 2> shifters;

    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
