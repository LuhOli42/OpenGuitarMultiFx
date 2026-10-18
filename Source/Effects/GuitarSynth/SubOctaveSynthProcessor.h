#pragma once

#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Analogue-style octave synth: no pitch tracking, so no tracking latency and it follows whatever you play.
      guitar -> low-pass -> hysteresis comparator -> flip-flop (÷2, one octave down) -> flip-flop (÷4, two down)
      square subs -> envelope-swept low-pass, level follows the guitar's envelope
      + ring modulator (guitar x internal sine) + drive on the dry path.
    Works best on single notes; chords make the comparator chatter, as on the analogue originals. */
class SubOctaveSynthProcessor : public SynthEffect
{
public:
    SubOctaveSynthProcessor();
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

private:
    juce::AudioParameterFloat *sub1, *sub2, *dryLevel, *ringAmount, *ringFreq, *filterCut, *resonance, *sweep, *drive, *dynamics;
    double fs = 48000.0;
    mt::Biquad trackLp[2];
    gs::EnvelopeFollower env;
    gs::Svf filter;
    double prev = 0.0, peak = 0.0, ringPhase = 0.0, amp = 0.0;
    bool armed = false, ff1 = false, ff2 = false;
    int controlCounter = 0;
};

} // namespace openguitarmultifx
