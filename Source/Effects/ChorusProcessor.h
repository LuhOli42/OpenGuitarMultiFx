#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"

namespace openguitarmultifx
{

/**
    A single modulated delay line (LFO sine sweeping the read position
    around a ~15ms centre) mixed with the dry signal, no feedback -- the
    classic chorus "doubled, slightly detuned voice" effect. Distinct from
    VibratoProcessor, which uses the same modulated-delay-line core but at
    100% wet (no dry blend) and a deeper sweep: chorus needs the dry/wet
    beating to create the doubling illusion, vibrato is pure pitch wobble
    with nothing to beat against. Also distinct from FlangerProcessor,
    which adds feedback and sweeps a much shorter (sub-10ms) delay range.
*/
class ChorusProcessor : public EffectProcessor
{
public:
    ChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Chorus"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float centreDelayMs = 15.0f;
    static constexpr float maxDepthMs = 8.0f;
    static constexpr float bufferHeadroomMs = 40.0f; // centre + max depth + margin

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    FractionalDelayLine line;
    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
