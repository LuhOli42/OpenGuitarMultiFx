#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Zendrive-style overdrive (Hermida Audio / Lovepedal), modelled component by component on NodalCircuit from Aion FX's
    drawing of the circuit (its "Azimuth" project), cross-checked with the Stomp Box Schematics drawing -- see
    docs/circuits/ZendriveStyleOverdrive.md, which is the file to read to understand this processor, not this comment or
    the .cpp.

    An AD712 non-inverting stage whose feedback is the Drive pot (500K, a rheostat with a 1K to the wiper) with an asymmetric
    clipper across it: BAT41 + 2N7000 body diode one way, BAT41 + 1N34A + body diode the other, so the stage clips at about
    +1.0 V / -1.3 V across its own feedback, then a variable RC treble cut (Tone) and a buffer. The Voice pot sets the bass
    corner of the (-) leg and with it the available gain. One NodalCircuit block per channel.

    Controls, as on the pedal: Drive, Tone, Voice, Volume.
*/
class ZendriveStyleOverdriveProcessor : public EffectProcessor
{
public:
    ZendriveStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Zendrive-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffe07a2e); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the gain stage's output and (-) node (their difference is what the clipper sees). */
    double debugStage1Out() const noexcept { return channels[0].c.voltage (channels[0].nOp1); }
    double debugMinus() const noexcept { return channels[0].c.voltage (channels[0].nMinus); }
    double debugToneNode() const noexcept { return channels[0].c.voltage (channels[0].nTone); }

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
        NodalCircuit::Node nOp1 = 0, nMinus = 0, nTone = 0, nOut = 0;
        int rDriveA = 0, rDriveB = 0; // Drive pot: output-to-wiper and wiper-to-(-) segments
        int rVoice = 0;               // Voice rheostat
        int rTone = 0;                // Tone rheostat
        int rLevelTop = 0, rLevelBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double driveKnob, double toneKnob, double voiceKnob, double levelKnob);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* voice = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedDrive, smoothedTone, smoothedVoice, smoothedLevel;

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
