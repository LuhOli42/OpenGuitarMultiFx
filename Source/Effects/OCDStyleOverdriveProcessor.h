#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Fulltone OCD-style overdrive (version 1.7, the one in production longest), modelled component by component on
    NodalCircuit from Aion FX's drawing of the circuit (its "Titan" project, whose default parts list is v1.7),
    cross-checked with the Analog Is Not Dead analysis -- see docs/circuits/OCDStyleOverdrive.md, which is the file to
    read to understand this processor, not this comment or the .cpp.

    Two TL082 non-inverting stages with a clipper between them that is referenced to the 4.5 V bias, not to ground: two
    2N7000s (each with its gate and drain on one net, so a two-terminal device: a channel that needs ~2 V and a body diode)
    and two red LEDs, all across the stage-1 output. A Clipping switch takes the 2N7000s out (LED clipping, a common mod),
    an HP/LP switch changes the treble network's bleed, and the Tone control is a passive treble shunt after stage 2. Two
    NodalCircuit blocks per channel, cut at the clipper node (the second op-amp's (+) input draws nothing).

    Controls, as on the pedal: Drive, Tone, Volume, Clipping (MOSFET / LED) and HP/LP.
*/
class OCDStyleOverdriveProcessor : public EffectProcessor
{
public:
    OCDStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "OCD-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f8f5b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): stage 1's output, the clipper node and stage 2's output. */
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugClipNode() const noexcept { return channels[0].a.voltage (channels[0].nClip); }
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

        int srcIn = 0;                                   // a: input
        NodalCircuit::Node nOp1 = 0, nClip = 0;          // a: stage-1 output, clipper node
        int rDrive = 0;                                  // a: Drive rheostat
        int rClipSwitch = 0;                             // a: the Clipping switch (the 2N7000s' common node to the bias)
        int srcClip = 0;                                 // b: driven clipper node
        NodalCircuit::Node nOp2 = 0, nOut = 0;           // b: stage-2 output, pedal output
        int rHp = 0;                                     // b: R10 in series with the HP/LP switch
        int rTone = 0;                                   // b: Tone rheostat
        int rLevelTop = 0, rLevelBottom = 0;             // b: Volume pot segments
    };

    void buildChannel (Channel& ch);
    void updatePots (double driveKnob, double toneKnob, double levelKnob);
    void updateSwitches();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterFloat* clipping = nullptr;
    juce::AudioParameterFloat* peak = nullptr;

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
