#pragma once

#include "PhaseVocoderShifter.h"
#include "SpliceShifter.h"
#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Makes a guitar sound like a bass.
      guitar -> down 1 or 2 octaves at an exact ratio, no pitch tracker, so chords shift too:
        MONO: SpliceShifter (~8 ms; single notes and power chords in tune, a chord's third ~14 cents sharp)
        POLY: PhaseVocoderShifter (~85 ms; every chord note in tune -- pads, recording)
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
    gs::SpliceShifter shifter;
    gs::PhaseVocoderShifter poly;
    int lastMode = -1;
    gs::EnvelopeFollower comp;
    mt::Biquad hp, shelf, toneLp[2], pick, blendLp;
    double lastShape = -1.0;
};

} // namespace openguitarmultifx
