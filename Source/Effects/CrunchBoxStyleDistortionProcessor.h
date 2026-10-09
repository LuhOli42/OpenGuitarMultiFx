#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An MI Audio Crunch Box-style distortion (the 2006 drawing, "MI audio CRUNCH BOX DISTORTION" by matsumin), modelled
    component by component on NodalCircuit -- see docs/circuits/CrunchBoxStyleDistortion.md, which is the file to read to
    understand this processor, not this comment or the .cpp.

    Two LM833 stages in the Blues Breaker's arrangement -- a non-inverting first stage whose Drive pot is both its
    feedback and a series resistance into the second, an inverting second stage (1M || 100 pF, so up to x100) -- then two
    red LEDs to ground behind 1K, a passive Tone (a 10K "C" rheostat into 39 nF), a 10K + 4K7/22 nF shelf and the Volume
    pot. Two NodalCircuit blocks per channel, cut at the first op-amp's output.

    Controls, as on the pedal: Drive, Tone, Volume.
*/
class CrunchBoxStyleDistortionProcessor : public EffectProcessor
{
public:
    CrunchBoxStyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Crunch Box-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd9482b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): gain stage output, second stage output and the LED node. */
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugStage2Out() const noexcept { return channels[0].b.voltage (channels[0].nOp2); }
    double debugLedNode() const noexcept { return channels[0].b.voltage (channels[0].nLed); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit a, b;

        int srcIn = 0;                          // a: input
        NodalCircuit::Node nOp1 = 0;            // a: op-amp 1 output (= the Drive pot's wiper)
        int srcOp1 = 0;                         // b: driven wiper node
        NodalCircuit::Node nOp2 = 0, nLed = 0, nOut = 0; // b: op-amp 2 output, LED node, pedal output
        int rDriveFeedback = 0;                 // a: Drive pot, (-) side to wiper
        int rDriveSeries = 0;                   // b: Drive pot, wiper to the coupling cap
        int rTone = 0;                          // b: Tone rheostat
        int rLevelTop = 0, rLevelBottom = 0;    // b: Volume pot segments
    };

    void buildChannel (Channel& ch);
    void updatePots (double driveKnob, double toneKnob, double levelKnob);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedDrive, smoothedTone, smoothedLevel;

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
