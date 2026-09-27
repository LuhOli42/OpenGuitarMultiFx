#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss GE-7-style seven-band graphic equalizer (100 / 200 / 400 / 800 Hz, 1.6 / 3.2 / 6.4 kHz and a Level slider), modelled
    component by component on NodalCircuit from the manufacturer's circuit diagram (published on hobby-hour.com) -- see
    docs/circuits/GE7StyleEqualizer.md, which is the file to read to understand this processor, not this comment or the .cpp.

    The audio path is: a non-inverting input stage with a treble pre-emphasis (x1 .. x11 above 2.3 kHz), the Level stage (a
    non-inverting stage whose slider moves a 2.2K / 10 uF between the (+) input and the feedback node: +-15 dB), and the equaliser
    -- one op-amp whose (+) input node and (-) node are joined, through each band's slider, to an RLC branch to the bias
    (a gyrator: an inductor simulated by a follower, 0.06 .. 1.85 H) -- then the matching de-emphasis and an emitter-follower buffer.
    Pre- and de-emphasis cancel, and with every slider centred the whole circuit is flat: the test checks it.

    Four NodalCircuit blocks per channel (each holds at most one op-amp macro and 32 nodes), cut where the next stage's input draws
    (almost) nothing.
    Controls, as on the pedal: seven band sliders and Level, each -15 .. +15 dB nominal (the slider position; the real gain at the
    band's centre is what the circuit gives, in the doc).
*/
class GE7StyleEqualizerProcessor : public EffectProcessor
{
public:
    static constexpr int numBands = 7;

    GE7StyleEqualizerProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "GE-7-Style Equalizer"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2fa36b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the equaliser op-amp's output. */
    double debugEqOut() const noexcept { return channels[0].c.voltage (channels[0].nEqOut); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

    static constexpr std::array<double, numBands> bandHz { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };

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
        std::array<int, numBands> rUp {}, rDown {}; // c: each band's slider segments (to the (+) node, to the (-) node)
    };

    void buildChannel (Channel& ch);
    void updateSliders();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::array<juce::AudioParameterFloat*, numBands> bands {};
    juce::AudioParameterFloat* level = nullptr;
    std::array<float, numBands + 1> appliedSliders {};

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 64; // samples between slider -> resistance checks (a change refactors a block)
};

} // namespace openguitarmultifx
