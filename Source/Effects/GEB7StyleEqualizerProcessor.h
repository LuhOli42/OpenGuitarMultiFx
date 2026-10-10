#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss GEB-7-style seven-band bass graphic equalizer (50 / 120 / 400 / 500 / 800 Hz, 4.5 / 10 kHz and a Level
    slider), the bass variant of the GE-7: same gyrator graphic-EQ circuit, different band frequencies per the
    manufacturer's spec (the GEB-7 owner's manual publishes its centres; the BOM values used to hit them are
    estimated on the same gyrator topology -- see docs/circuits/GEB7StyleEqualizer.md, the file to read to
    understand this processor).

    Same audio path as the GE-7 model: a non-inverting input stage with a treble pre-emphasis, the Level stage, the
    equaliser (one op-amp whose (+) and (-) nodes are joined, through each band's slider, to an RLC branch to the
    bias), then de-emphasis and an emitter-follower buffer. Four NodalCircuit blocks per channel.

    Controls, as on the pedal: seven band sliders and Level, each -15 .. +15 dB nominal.
*/
class GEB7StyleEqualizerProcessor : public EffectProcessor
{
public:
    static constexpr int numBands = 7;

    GEB7StyleEqualizerProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "GEB-7-Style Equalizer"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f6fa3); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the equaliser op-amp's output. */
    double debugEqOut() const noexcept { return channels[0].c.voltage (channels[0].nEqOut); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

    static constexpr std::array<double, numBands> bandHz { 50.0, 120.0, 400.0, 500.0, 800.0, 4500.0, 10000.0 };

private:
    struct Channel
    {
        NodalCircuit a, b, c, d;

        int srcIn = 0;                       // a: input
        NodalCircuit::Node nA = 0;           // a: stage-1 output
        int srcB = 0;                        // b: driven stage-1 output
        NodalCircuit::Node nB = 0;           // b: Level stage output
        int srcC = 0;                        // c: driven Level output
        NodalCircuit::Node nEqOut = 0;       // c: the equaliser op-amp's output
        int srcD = 0;                        // d: driven equaliser output
        NodalCircuit::Node nOut = 0;         // d: the pedal's output
        int rLevelUp = 0, rLevelDown = 0;    // b: Level slider segments
        std::array<int, numBands> rUp {}, rDown {}; // c: each band's slider segments
    };

    void buildChannel (Channel& ch);
    void updateSliders();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::array<juce::AudioParameterFloat*, numBands> bands {};
    juce::AudioParameterFloat* level = nullptr;
    std::array<float, numBands + 1> appliedSliders {};

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 64;
};

} // namespace openguitarmultifx
