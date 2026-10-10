#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    Plain amplitude modulation -- no delay line at all, unlike every other
    modulation effect here. A sine LFO scales the signal's gain between
    (1-Depth) and 1.0; Rate is the LFO frequency in Hz. The simplest
    possible modulation effect, and deliberately built that way: Chorus/
    Flanger/Vibrato all share a modulated-delay-line core, and Tremolo is
    the one member of the category that doesn't touch pitch/delay at all.
*/
class TremoloProcessor : public EffectProcessor
{
public:
    TremoloProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Tremolo"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;

    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
};

} // namespace openguitarmultifx
