#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"

#include <array>

namespace openguitarmultifx
{

enum class AlgorithmicDelay
{
    ducking,  // after the TC 2290's dynamic delay: the repeats duck while you play and swell back when you stop
    shimmer,  // a pitch shifter inside the feedback loop: every repeat an octave (or a fifth) higher
    lofi,     // every pass loses sample rate, bits and bandwidth -- a degrading memory
    granular  // the repeats chopped into windowed grains, scattered in time, some reversed or an octave up
};

/**
    The delays that are algorithms rather than pedal circuits (docs/circuits/AlgorithmicDelays.md). Level adds the
    repeats to the full dry signal, like a delay pedal's E.Level -- the dry path is never touched.
*/
class AlgorithmicDelayProcessor : public EffectProcessor
{
public:
    explicit AlgorithmicDelayProcessor (AlgorithmicDelay kind);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6f9fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Grain
    {
        bool active = false;
        double start = 0.0, rate = 1.0, length = 0.0, age = 0.0;
    };

    struct Channel
    {
        EnsembleDelay line, pitchLine;
        WetLowpass tone;
        double lastWet = 0.0, envelope = 0.0, held = 0.0, holdCount = 0.0, pitchPhase = 0.0;
        std::array<Grain, 8> grains;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});
    double shimmerShift (Channel& ch, double x) noexcept;

    AlgorithmicDelay kind;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *time = nullptr, *feedback = nullptr, *level = nullptr, *amount = nullptr, *release = nullptr,
                              *pitch = nullptr, *grainSize = nullptr, *density = nullptr, *reverse = nullptr;
    juce::SmoothedValue<float> smoothedTime;
    juce::Random random { 0x5eed };
    double sampleRate = 0.0, grainClock = 0.0, lastAmount = -1.0;
};

} // namespace openguitarmultifx
