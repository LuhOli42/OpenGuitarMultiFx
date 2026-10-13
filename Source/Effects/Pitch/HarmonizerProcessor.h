#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/DelayLine.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    Same two-grain shifter and additive-layering philosophy as
    OctaverProcessor (dry always stays at full level, the shifted layer
    is only added on top), but the interval is a variable Semitones
    parameter instead of a fixed octave down -- a harmony voice at a
    musical interval, not specifically a sub-octave doubler.
*/
class HarmonizerProcessor : public EffectProcessor
{
public:
    HarmonizerProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Harmonizer"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a9e5c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* semitones = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    std::array<TwoGrainShifter, 2> shifters;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedSemitones;
    juce::SmoothedValue<float> smoothedLevel;
};

} // namespace openguitarmultifx
