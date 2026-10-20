#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"

#include <array>

namespace openguitarmultifx
{

enum class AlgorithmicFlanger
{
    throughZero,   // two-tape flanging: the modulated tape passes the reference, so the comb sweeps through zero delay
    barberpole,    // an endless sweep: overlapping ramps crossfaded so it rises (or falls) forever
    instant,       // after the Eventide Instant Flanger: Manual, LFO and an envelope all move the delay
    envelope       // the sweep follows the pick: dynamics, not an LFO, move the delay
};

/**
    The flangers that are algorithms rather than pedal circuits (docs/circuits/AlgorithmicFlangers.md). All run on
    Hermite-interpolated delay lines with feedback; none band-limit the wet the way a BBD does except the Instant
    Flanger, whose hardware was analog.
*/
class AlgorithmicFlangerProcessor : public EffectProcessor
{
public:
    explicit AlgorithmicFlangerProcessor (AlgorithmicFlanger kind);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff58c0a8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Channel
    {
        EnsembleDelay reference, modulated;
        WetLowpass lowpass;
        double lastOut = 0.0, envelope = 0.0;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    AlgorithmicFlanger kind;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *manual = nullptr, *feedback = nullptr, *mix = nullptr,
                              *direction = nullptr, *envAmount = nullptr, *sensitivity = nullptr, *decay = nullptr;
    juce::SmoothedValue<float> smoothedDepth, smoothedManual, smoothedFeedback, smoothedMix;

    double phase = 0.0, sampleRate = 0.0;
};

} // namespace openguitarmultifx
