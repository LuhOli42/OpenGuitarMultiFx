#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Marshall Guv'nor-style distortion, modelled component by component on NodalCircuit from ElectroSmash's schematic --
    see docs/circuits/GuvnorStyleDistortion.md, which is the file to read to understand this processor, not this comment
    or the .cpp.

    Two TL072 op-amp stages (a non-inverting gain stage with the Gain pot as its feedback AND as a series resistance into
    the second stage, then an inverting stage), a pair of back-to-back red LEDs to ground, and a passive Bass / Middle /
    Treble stack of the Marshall/Big-Muff kind (highly interactive), then a Level pot. Two NodalCircuit blocks per
    channel, cut at the first op-amp's output; each holds one macro op-amp (NodalCircuit::addOpAmpMacro).

    Controls, as on the pedal: Gain, Bass, Middle, Treble, Level.
*/
class GuvnorStyleDistortionProcessor : public EffectProcessor
{
public:
    GuvnorStyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Guv'nor-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb02020); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): gain stage output, clipping stage output, the LED node (= the tone stack's input). */
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugStage2Out() const noexcept { return channels[0].b.voltage (channels[0].nOp2); }
    double debugLedNode() const noexcept { return channels[0].b.voltage (channels[0].nLed); }

    /** Average Newton iterations per sample of channel 0's two blocks (a runaway solver shows here before it fails). */
    double debugIterationsA() const noexcept { return channels[0].a.averageIterations(); }
    double debugIterationsB() const noexcept { return channels[0].b.averageIterations(); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit a, b;

        int srcIn = 0;                                   // a: input
        NodalCircuit::Node nOp1 = 0;                     // a: op-amp 1 output (= the Gain pot's wiper)
        int srcOp1 = 0;                                  // b: driven wiper node
        NodalCircuit::Node nOp2 = 0, nLed = 0, nOut = 0; // b: op-amp 2 output, LED node, pedal output
        int rGainFeedback = 0;                           // a: Gain pot, (-) side to wiper
        int rGainSeries = 0;                             // b: Gain pot, wiper to the coupling cap
        int rBassTop = 0, rBassBottom = 0;               // b: Bass pot segments
        int rMidTop = 0, rMidBottom = 0;                 // b: Middle pot segments
        int rTrebleTop = 0, rTrebleBottom = 0;           // b: Treble pot segments
        int rLevelTop = 0, rLevelBottom = 0;             // b: Level pot segments
    };

    void buildChannel (Channel& ch);
    void updatePots (double gainKnob, double bassKnob, double midKnob, double trebleKnob, double levelKnob);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* bass = nullptr;
    juce::AudioParameterFloat* mid = nullptr;
    juce::AudioParameterFloat* treble = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedBass, smoothedMid, smoothedTreble, smoothedLevel;

        /** JUCE's reset() must return the circuit to its DC operating point -- an
        empty reset() (the bug this fixes) left stale capacitor state forever.
        prepare() deliberately no-ops on a same-rate re-prepare to protect live
        state during the UI's chain-reorder, so reset() re-arms the rate and
        forces the rebuild. Control thread only. */
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

    static constexpr int controlInterval = 16; // samples between pot -> resistance updates
};

} // namespace openguitarmultifx
