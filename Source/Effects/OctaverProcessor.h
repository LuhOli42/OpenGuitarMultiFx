#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    Same two-grain shifter as PitchShiftProcessor, fixed at one octave
    down (ratio 0.5), but ADDED underneath the dry signal rather than
    crossfaded against it -- a real octaver pedal never removes your
    actual note, it only layers a sub-octave under it. Level controls how
    loud that added layer is; the dry signal's own level never changes.
*/
class OctaverProcessor : public EffectProcessor
{
public:
    OctaverProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Octaver"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a9e5c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* level = nullptr;

    std::array<TwoGrainShifter, 2> shifters;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedLevel;
};

} // namespace openguitarmultifx
