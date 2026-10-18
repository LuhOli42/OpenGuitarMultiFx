#pragma once

#include "SynthEffect.h"

namespace openguitarmultifx
{

/** Cross-synthesis without tracking, so it takes chords: the guitar is split into 16 band-pass bands, each band is
    flattened by its own envelope (Flatten = how "synthetic" the result is) and re-weighted by a target spectral
    shape. Shapes are formant sets (vowels, brass, strings, ...); Morph sweeps from shape A to shape B, optionally
    driven by an LFO.
      guitar -> 16 x band-pass -> per band: x * target_b / (1 - flatten + flatten * envelope_b / ref)
      -> sum -> gate on the input envelope -> mix. */
class SpectralMorphProcessor : public SynthEffect
{
public:
    SpectralMorphProcessor();
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    static constexpr int kBands = 16;
    static juce::StringArray shapeNames();

private:
    void computeShape (int shapeIndex, std::array<double, kBands>& out) const;

    juce::AudioParameterFloat *shapeA, *shapeB, *morph, *lfoRate, *lfoDepth, *flatten, *dynamics, *response;
    double fs = 48000.0;
    std::array<double, kBands> centres {};
    std::array<mt::Biquad, kBands> bands;
    std::array<gs::EnvelopeFollower, kBands> followers;
    std::array<double, kBands> shapeAGains {}, shapeBGains {}, current {};
    int cachedA = -1, cachedB = -1;
    gs::EnvelopeFollower inputEnv;
    double lfoPhase = 0.0;
};

} // namespace openguitarmultifx
