#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Marshall Blues Breaker-style overdrive (the 1992 original, 27K/33K version), modelled component by component on
    NodalCircuit from the schematic in the GM Arts / Stomp Box Schematics collection -- see
    docs/circuits/BluesBreakerStyleOverdrive.md, which is the file to read to understand this processor, not this comment
    or the .cpp.

    Two TL072 stages: a non-inverting gain stage whose Drive pot is both its feedback and a series resistance into the
    next stage, then an inverting stage with four silicon diodes (two in series each way, behind a 6.8K) in its feedback
    -- Tube Screamer-style feedback clipping, but softer because of the series pairs -- then a passive Tone and a Level
    pot. Two NodalCircuit blocks per channel, cut at the first op-amp's output.

    Controls, as on the pedal: Drive, Tone, Volume.
*/
class BluesBreakerStyleOverdriveProcessor : public EffectProcessor
{
public:
    BluesBreakerStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Blues Breaker-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3a78c8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): gain stage output and clipping stage output. */
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugStage2Out() const noexcept { return channels[0].b.voltage (channels[0].nOp2); }

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
        NodalCircuit::Node nOp2 = 0, nOut = 0;  // b: op-amp 2 output, pedal output
        int rDriveFeedback = 0;                 // a: Drive pot, (-) side to wiper
        int rDriveSeries = 0;                   // b: Drive pot, wiper to the coupling cap
        int rToneTop = 0, rToneBottom = 0;      // b: Tone pot segments
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

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 16; // samples between pot -> resistance updates
};

} // namespace openguitarmultifx
