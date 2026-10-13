#pragma once

#include "Effects/EffectProcessor.h"
#include "PhaserParts.h"

#include <array>

namespace openguitarmultifx
{

enum class AlgorithmicPhaser
{
    barberpole, // an endless sweep: three phasers ramped a third of a cycle apart and crossfaded
    envelope,   // the notches follow the pick
    quadrature, // two 8-stage phasers whose LFOs are 90 degrees apart, one per side
    step        // a sample-and-hold sweep: the notches jump, with an adjustable glide
};

/** Phasers that are algorithms rather than pedal circuits (docs/circuits/AlgorithmicPhasers.md). Mix crossfades the
    input into the all-passed signal, so 0.5 gives the deepest notches and Mix 1 the pure phase-shifted (vibrato)
    signal, at the input's level. */
class AlgorithmicPhaserProcessor : public EffectProcessor
{
public:
    explicit AlgorithmicPhaserProcessor (AlgorithmicPhaser kind);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff9f6fc0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr int stagesPerChain = 8;

    struct Chain
    {
        std::array<phaser::AllpassStage, stagesPerChain> stages;
        double lastOut = 0.0;
        double run (double x, double cornerHz, int n, double feedback, double sampleRate) noexcept;
        void reset() noexcept;
    };

    struct Channel
    {
        std::array<Chain, 3> chains;
        double envelope = 0.0;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    AlgorithmicPhaser kind;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *feedback = nullptr, *mix = nullptr, *direction = nullptr,
                              *sensitivity = nullptr, *smooth = nullptr;
    juce::SmoothedValue<float> smoothedMix;
    juce::Random random { 0x9a5e };
    double phase = 0.0, held = 0.0, glided = 0.0, sampleRate = 0.0;
};

} // namespace openguitarmultifx
