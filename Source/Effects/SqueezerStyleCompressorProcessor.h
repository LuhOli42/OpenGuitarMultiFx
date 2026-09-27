#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Dan Armstrong Orange Squeezer-style compressor, modelled component by component from DryBell's circuit description (DM1044,
    Kristijan Golub, 2022: the schematic with every value and a written analysis) -- see docs/circuits/SqueezerStyleCompressor.md, which
    is the file to read to understand this processor, not this comment or the .cpp.

    A FEEDBACK FET compressor with no input buffer: a 2N5457 (Q2) is the lower leg of a divider with R2 82K, so it is a voltage-controlled
    resistor at the pedal's input; a JRC4558 gain stage (x23 = 27 dB) makes up the level, and a germanium half-wave rectifier (D1, R3, C6,
    R8: attack ~6 ms, release ~200-470 ms) feeds the JFET's gate. Q1 is a 500 uA current source and the VR2 trimmer sets Q2's operating point
    near its cut-off, which sets the compression threshold. One NodalCircuit block per channel.

    Controls: Volume (the only knob of the pedal) and Bias (its internal VR2 trimmer: light compression to limiting).
*/
class SqueezerStyleCompressorProcessor : public EffectProcessor
{
public:
    SqueezerStyleCompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Orange Squeezer-Style Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffe8792b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the op-amp output, the JFET's gate control voltage and the divider node. */
    double debugOpAmpOut() const noexcept { return channels[0].c.voltage (channels[0].nOp); }
    double debugGateControl() const noexcept { return channels[0].c.voltage (channels[0].nControl); }
    double debugDivider() const noexcept { return channels[0].c.voltage (channels[0].nDivider); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit c;

        int srcIn = 0;
        NodalCircuit::Node nOp = 0, nControl = 0, nDivider = 0, nOut = 0;
        int rBias = 0;                          // VR2
        int rVolTop = 0, rVolBottom = 0;        // VR1
    };

    void buildChannel (Channel& ch);
    void updatePots (double volumeKnob, double biasKnob);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volume = nullptr;
    juce::AudioParameterFloat* bias = nullptr;

    juce::SmoothedValue<float> smoothedVolume;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
