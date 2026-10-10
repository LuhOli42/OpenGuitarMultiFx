#pragma once

#include "AsymmetricDiodePair.h"
#include "EffectProcessor.h"
#include "TrapezoidalCapacitor.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Ibanez TS9B-style bass Tube Screamer -- the bass variant of the TS9, modelled with the same
    hand-derived Thevenin-chain + Newton clipper technique as TubeScreamerStyleOverdriveProcessor
    (see docs/circuits/TS9BStyleOverdrive.md for what is real and what is estimated).

    Signal path: emitter-follower input buffer (Q1) -> non-inverting op-amp clipper (symmetric
    diode pair + a small cap in the feedback loop, 1D Newton-Raphson per sample) -> an active
    two-band tone stage (op-amp 2, the same bridged-feedback Baxandall arrangement as the TS tone
    stage but with separate Bass and Treble pots each carrying its own shunt leg) -> a resistive
    Mix of the driven signal with the Q1-buffered dry signal -> Level pot -> emitter-follower
    output buffer (Q2).

    Five controls, matching the real pedal: Drive, Mix, Bass, Treble, Level.
*/
class TS9BStyleOverdriveProcessor : public EffectProcessor
{
public:
    TS9BStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "TS9B-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4f9a3c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        return { (float) channels[0].debugQ1Vb, (float) channels[0].debugQ1Ve, (float) supplyVoltage };
    }

    /** Channel 0's clipping op-amp output, for gain verification against the real circuit. */
    double debugOp1Out() const noexcept { return channels[0].debugOp1Out; }

    /** Fraction of the failed-to-converge diode solves since prepare(). */
    double getClipperFailureRate() const noexcept
    {
        return solveCount > 0 ? (double) solveFailures / (double) solveCount : 0.0;
    }

private:
    struct ChannelState
    {
        TrapezoidalCapacitor c1, c2, c3, c4, c5, c6, c6b, c7, c8, c9, cDry;
        double debugOp1Out = 0.0;
        double debugQ1Vb = 0.0, debugQ1Ve = 0.0;
        AsymmetricDiodePair clipper;
    };
    std::array<ChannelState, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* mix = nullptr;
    juce::AudioParameterFloat* bass = nullptr;
    juce::AudioParameterFloat* treble = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    // Pot-derived resistances feed the per-sample solve; a block jump would be an audible topology
    // step, so they are smoothed (same reasoning as the TS model).
    juce::SmoothedValue<float> smoothedRDrive;
    juce::SmoothedValue<float> smoothedRMixWet;            // mix pot: wet-leg resistance
    juce::SmoothedValue<float> smoothedRTrebleWiperToPlus; // treble pot: pin 1 to wiper
    juce::SmoothedValue<float> smoothedRBassWiperToPlus;   // bass pot: pin 1 to wiper
    juce::SmoothedValue<float> smoothedRLevelWiperToBottom;

    double sampleRate = 0.0;
    double settledSampleRate = -1.0;

    long long solveCount = 0;
    long long solveFailures = 0;

    // Component values -- see docs/circuits/TS9BStyleOverdrive.md. The input buffer, clipper and
    // output buffer keep the TS9's BOM; the tone stage keeps the TS9's structure and doubles it
    // for the second band, and the Mix summer is the published function of the real control.
    static constexpr float r1 = 1.0e3f;
    static constexpr float r2 = 510.0e3f;
    static constexpr float r3 = 10.0e3f;
    static constexpr float r4 = 4.7e3f;
    static constexpr float r5 = 10.0e3f;
    static constexpr float r6 = 51.0e3f;
    static constexpr float r7 = 1.0e3f;
    static constexpr float r8 = 220.0f;    // treble shunt resistor (TS9 value)
    static constexpr float r8b = 470.0f;   // bass shunt resistor (estimated on the same topology)
    static constexpr float r9 = 1.0e3f;
    static constexpr float r10 = 10.0e3f;
    static constexpr float r11 = 1.0e3f;
    static constexpr float r12 = 510.0e3f;
    static constexpr float r13 = 10.0e3f;

    static constexpr float c1Value = 0.02e-6f;
    static constexpr float c2Value = 1.0e-6f;
    static constexpr float c3Value = 0.047e-6f;
    static constexpr float c4Value = 51.0e-12f;
    static constexpr float c5Value = 0.22e-6f;
    static constexpr float c6Value = 0.22e-6f;   // treble shunt cap (TS9 value)
    static constexpr float c6bValue = 0.47e-6f;  // bass shunt cap: bigger C puts the band low (estimated)
    static constexpr float c7Value = 1.0e-6f;
    static constexpr float c8Value = 0.1e-6f;
    static constexpr float c9Value = 10.0e-6f;
    static constexpr float cDryValue = 1.0e-6f;  // the clean path's coupling cap (estimated)

    static constexpr float driveMax = 500.0e3f;   // A500K audio taper
    static constexpr float trebleMax = 20.0e3f;   // B20K linear (TS9 value)
    static constexpr float bassMax = 20.0e3f;     // B20K linear (estimated on the same part)
    static constexpr float levelMax = 100.0e3f;   // A100K audio taper
    static constexpr float mixSum = 100.0e3f;     // the blend pot's track (estimated); >> the ~10K source
                                                  // impedances so each end of the knob really does mute its leg

    static constexpr float closedSwitchResistance = 100.0f;
    static constexpr float outputLoadResistance = 1.0e6f;
    static constexpr float supplyVoltage = 9.0f;
    static constexpr double followerDrop = 0.62;
    static constexpr float bias = supplyVoltage * 0.5f;

    // 1N914 / 1N4148 SPICE pair, as in the TS model.
    static constexpr double diodeSaturationCurrent = 2.52e-9;
    static constexpr double diodeIdealityFactor = 1.752;
    static constexpr double diodeThermalVoltage = 25.85e-3;
};

} // namespace openguitarmultifx
