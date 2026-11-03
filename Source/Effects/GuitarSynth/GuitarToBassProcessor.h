#pragma once

#include "PhaseVocoderShifter.h"
#include "SpliceShifter.h"
#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Makes a guitar sound like a bass.
      guitar -> down 1 or 2 octaves at an exact ratio, no pitch tracker, so chords shift too:
        MONO: SpliceShifter (~8 ms; single notes and power chords in tune, a chord's third ~14 cents sharp)
        POLY: PhaseVocoderShifter (~40 ms; every chord note in tune)
        HYBRID: both run; each note's attack comes from MONO, then ~90 ms in it crossfades to POLY, whose partials were
                started in phase with MONO's (PhaseVocoderShifter::alignToReference) so the two do not cancel
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
    juce::AudioParameterFloat *octaves, *mode, *voicing, *tone, *body, *grit, *squash, *guitarBlend;
    double fs = 48000.0;
    void resetHybrid();
    gs::SpliceShifter shifter;
    gs::PhaseVocoderShifter poly;
    int lastMode = -1;
    gs::EnvelopeFollower onsetFast, onsetSlow;
    int sinceOnset = 0, alignAt = 960, handoffAt = 2880, refractory = 1440;
    bool aligned = false, wasRising = false;
    double polyWeight = 0.0, fadeUp = 0.001, fadeDown = 0.005;
    double pM = 0.0, pP = 0.0, pX = 0.0, compGain = 1.0, powA = 0.999, compA = 0.99;
    gs::EnvelopeFollower comp;
    mt::Biquad hp, shelf, toneLp[2], pick, blendLp;
    double lastShape = -1.0;
};

} // namespace openguitarmultifx
