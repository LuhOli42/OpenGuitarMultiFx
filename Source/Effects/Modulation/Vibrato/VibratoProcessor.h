#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/DelayLine.h"

namespace openguitarmultifx
{

/**
    Same modulated-delay-line core as ChorusProcessor, but 100% wet (no
    dry blend -- real vibrato pedals like the Boss VB-2 have no mix knob
    at all, it's inherently wet) and a deeper sweep. Chorus needs the dry
    signal to beat against the delayed copy to create its doubling
    illusion; vibrato has nothing to beat against, so what comes out is
    pure pitch wobble, not a doubled voice.
*/
class VibratoProcessor : public EffectProcessor
{
public:
    VibratoProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Vibrato"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float centreDelayMs = 6.0f;
    static constexpr float maxDepthMs = 5.0f;
    static constexpr float bufferHeadroomMs = 20.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;

    FractionalDelayLine line;
    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
};

} // namespace openguitarmultifx
