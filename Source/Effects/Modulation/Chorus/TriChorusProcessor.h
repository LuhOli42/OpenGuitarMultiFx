#pragma once

#include "Effects/EffectProcessor.h"
#include "EnsembleDelay.h"

namespace openguitarmultifx
{

/**
    Tri-Chorus: the three-voice chorus of the Dytronics Tri-Stereo Chorus (the late-'70s/'80s LA studio rack sound) as
    an algorithm, not a circuit model (docs/circuits/TriChorus.md). Three taps on one delay line, each swept by its own
    sine LFO 120 degrees apart from the others, so at any moment one voice is rising, one falling and one turning --
    the sum never collapses into the single-voice "wobble" of a one-tap chorus. Voice 1 sits left, voice 3 right, voice
    2 in the middle; a mono chain gets all three. The wet is band-limited like the BBDs of the original.

    Controls: Rate, Depth, Delay, Mix.
*/
class TriChorusProcessor : public EffectProcessor
{
public:
    TriChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Tri-Chorus"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff5fa8a0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    EnsembleDelay line;
    WetLowpass lowpassL, lowpassR;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* delayParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::SmoothedValue<float> smoothedRate, smoothedDepth, smoothedDelay, smoothedMix;

    double phase = 0.0, sampleRate = 0.0;
};

} // namespace openguitarmultifx
