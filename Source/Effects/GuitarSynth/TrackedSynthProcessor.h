#pragma once

#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Monophonic guitar synth: a time-domain pitch tracker and an envelope follower drive an oscillator (+ sub and a
    detuned twin) through an envelope-swept low-pass.
      guitar -> PitchTracker (one estimate per period, ~1-2 periods of latency)
             -> EnvelopeFollower -> gate + amplitude + filter sweep
      oscillator (saw / square / triangle / sine, PolyBLEP) at tracked pitch * octave * detune, glided
      + sub-oscillator one octave down -> SVF low-pass -> amplitude -> mix with the dry guitar. */
class TrackedSynthProcessor : public SynthEffect
{
public:
    TrackedSynthProcessor();
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

private:
    juce::AudioParameterFloat *wave, *octave, *sub, *detune, *glide, *cutoff, *resonance, *envAmount, *release, *dynamics,
        *sensitivity, *dryGuitar;
    double fs = 48000.0;
    gs::PitchTracker tracker;
    gs::EnvelopeFollower env;
    gs::Osc osc, osc2, subOsc;
    gs::Svf filter;
    double curHz = 0.0, amp = 0.0;
    int controlCounter = 0;
};

} // namespace openguitarmultifx
