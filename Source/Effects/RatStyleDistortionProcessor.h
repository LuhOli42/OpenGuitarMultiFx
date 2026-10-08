#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A ProCo RAT-style distortion (the original, single-op-amp circuit), modelled component by component on NodalCircuit
    from the reverse-engineered schematic in the GitHub guitar-effects-schematics collection (rev P, 2006), cross-checked
    against the older BF245A-buffer drawing -- see docs/circuits/RatStyleDistortion.md, which is the file to read to
    understand this processor, not this comment or the .cpp.

    An LM308 (30 pF compensation: ~1 MHz gain-bandwidth) as a non-inverting stage with the Distortion pot as a rheostat
    in the feedback (gain up to ~3600 = 71 dB, so the op-amp's own bandwidth, ~300 Hz-1 kHz at high gain, IS the
    pedal's tone) into a 1K + 4.7 uF and two silicon diodes to ground, then the passive Filter (a rheostat into a 3.3 nF
    low-pass), a JFET source follower and the Volume pot. One NodalCircuit block per channel.

    Controls, as on the pedal: Distortion, Filter, Volume.

    One class, three models: the original (Rev P values), the RAT 2 and the Turbo RAT. The three share the topology and
    differ in a handful of values (and, for the Turbo, the clipping diodes and the op-amp) -- the Effects Layouts bill
    of materials for the shared PCB is the source for the last two; see the doc's "Versions" table.
*/
class RatStyleDistortionProcessor : public EffectProcessor
{
public:
    enum class Model { original, rat2, turbo };

    explicit RatStyleDistortionProcessor (Model model = Model::original);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8a8f96); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the op-amp's output, the diode node and the filter node. */
    double debugOpAmpOut() const noexcept { return channels[0].c.voltage (channels[0].nOpOut); }
    double debugDiodeNode() const noexcept { return channels[0].c.voltage (channels[0].nDiodes); }
    double debugFilterNode() const noexcept { return channels[0].c.voltage (channels[0].nFilter); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    /** What differs between the models (the doc's "Versions" table). */
    struct Spec
    {
        const char* displayName;
        double inputBiasOhms;       // R4: the node after the input cap to the 4.5 V bias
        double resistorLegA;        // R7, with 4.7 uF
        double distortionPotOhms;
        double filterFixedOhms;     // R10, in series with the Filter rheostat
        double outputCap;           // C13
        bool ledClipping;           // the Turbo's red LEDs instead of 1N4148s
        NodalCircuit::OpAmpMacro opAmp;
    };

    static const Spec& specFor (Model) noexcept;

    struct Channel
    {
        NodalCircuit c;

        int srcIn = 0;
        NodalCircuit::Node nOpOut = 0, nDiodes = 0, nFilter = 0, nOut = 0;
        int rGain = 0;                       // Distortion pot, as the rheostat in the feedback
        int rFilter = 0;                     // Filter pot, as the rheostat ahead of the 1K6 + 3.3 nF
        int rLevelTop = 0, rLevelBottom = 0; // Volume pot segments
    };

    void buildChannel (Channel& ch);
    void updatePots (double distortionKnob, double filterKnob, double volumeKnob);

    Spec spec;
    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* distortion = nullptr;
    juce::AudioParameterFloat* filter = nullptr;
    juce::AudioParameterFloat* volume = nullptr;

    juce::SmoothedValue<float> smoothedDistortion, smoothedFilter, smoothedVolume;

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
