#pragma once

#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Makes a guitar sound like a bass.
      guitar -> PitchTracker -> two-grain shifter (down 1 or 2 octaves), the grain locked to a whole number of
      the tracked period so the two reads stay in phase and the shifted note keeps its body
      -> bass voicing: 35 Hz high-pass, low shelf (body), tone low-pass (the string's lost highs), a pick-attack
      shelf for PICK, soft asymmetric grit -> squash compressor
      + a little of the dry guitar low-passed at 200 Hz for the string's own attack. */
class GuitarToBassProcessor : public SynthEffect
{
public:
    GuitarToBassProcessor();
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

private:
    juce::AudioParameterFloat *octaves, *voicing, *tone, *body, *grit, *squash, *guitarBlend;
    double fs = 48000.0;
    gs::PitchTracker tracker;
    mt::DelayLine line;
    gs::EnvelopeFollower comp;
    mt::Biquad hp, shelf, toneLp[2], pick, blendLp;
    double phi = 0.0, grainSamples = 0.0, lastShape = -1.0;
    int controlCounter = 0;
};

} // namespace openguitarmultifx
