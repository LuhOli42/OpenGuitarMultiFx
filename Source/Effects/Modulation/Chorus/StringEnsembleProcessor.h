#pragma once

#include "Effects/EffectProcessor.h"
#include "EnsembleDelay.h"

namespace openguitarmultifx
{

/**
    String Ensemble: the "ensemble" of the ARP/Eminent Solina String Ensemble and its descendants, as an algorithm
    (docs/circuits/StringEnsemble.md). Three taps on one delay line, each swept by the SUM of two sine LFOs -- a slow
    one (~0.6 Hz) for the broad chorus and a fast one (~6 Hz) for a vibrato-like shimmer -- with both LFOs' phases
    spread 120 degrees across the three taps. That double modulation is what turns one voice into a section. The
    original is wet-only; Mix blends the dry back in. Voices 1 and 3 go left and right in stereo.

    Controls: Speed (scales both LFOs), Depth (scales both sweeps), Mix.
*/
class StringEnsembleProcessor : public EffectProcessor
{
public:
    StringEnsembleProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "String Ensemble"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc97fb8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    EnsembleDelay line;
    WetLowpass lowpassL, lowpassR;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* speedParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::SmoothedValue<float> smoothedSpeed, smoothedDepth, smoothedMix;

    double slowPhase = 0.0, fastPhase = 0.0, sampleRate = 0.0;
};

} // namespace openguitarmultifx
