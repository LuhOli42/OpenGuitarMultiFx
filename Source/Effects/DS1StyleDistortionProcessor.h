#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A BOSS DS-1-style distortion -- modelled from the original (pre-1994,
    TA7136P-based) DS-1 factory board schematic, cross-checked stage-by-stage
    against ElectroSmash's independent analysis of a later revision. See
    docs/circuits/DS1StyleDistortion.md for the full circuit writeup
    (topology, every stage's role, every simplification made and why).

    Runs as a netlist on NodalCircuit (see docs/circuits/NodalCircuitSolver.md).
    The first version of this processor was hand-derived, with three couplings
    approximated by ONE-SAMPLE DELAYS (Q1's emitter into the JFET's node, Q2's
    collector-to-base shunt feedback, the JFET's coupling into Q2). Downstream
    gain multiplies whatever error a delay makes, and the measured result was
    a genuinely unstable/noisy output (~5% non-periodic error, a spike after
    every edge of the clipped waveform) and hundreds of failed transistor
    solves per second. A netlist solves those couplings exactly.

    Signal path: Q1 (NPN emitter-follower input buffer) -> Q6 (JFET used as a
    voltage-controlled resistor, biased at Vgs ~ 0) -> Q2 (NPN common-emitter
    gain stage with collector-to-base shunt feedback) -> an ideal op-amp
    non-inverting gain stage (Drive pot in its feedback) -> R14 + C9 -> the
    anti-parallel diode clipper shared with the Big Muff-style Tone network
    -> Level pot -> Q3 (NPN emitter-follower output buffer). (Q7, the
    bypass JFET, is modelled as a closed switch.)

    Three controls, matching the three pots the real pedal has: Drive (VR1),
    Tone (VR2), Level (VR3), all linear.
*/
class DS1StyleDistortionProcessor : public EffectProcessor
{
public:
    DS1StyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "DS-1-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8622a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Channel 0's Q2 terminal voltages -- Q2 is the main gain stage, so this is the permanent verification hook
    // that a correctly-biased gain stage can be told from one silently stuck at cutoff.
    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        const auto& ch = channels[0];
        return { (float) ch.pre.voltage (ch.nB2), (float) ch.pre.voltage (ch.nE2), (float) ch.pre.voltage (ch.nC2) };
    }

    /** Channel 0's op-amp output (after the transistor booster and the Drive stage), for gain verification. */
    double debugOpAmpOut() const noexcept { return channels[0].pre.voltage (channels[0].nO); }

    bool dcConverged() const noexcept { return dcOk; }
    double getSolveFailureRate() const noexcept { return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0; }

private:
    struct Channel
    {
        NodalCircuit pre, post; // pre: buffer + JFET VCR + Q2 + op-amp stage; post: clipper/tone/level/output buffer

        int srcIn = 0;
        NodalCircuit::Node nB2 = 0, nE2 = 0, nC2 = 0, nO = 0; // pre
        int srcO = 0;                                          // post: driven from the op-amp output
        NodalCircuit::Node nOut = 0;                           // post
        int rDrive = 0;                                        // pre: Drive pot (op-amp feedback)
        int rToneA = 0, rToneB = 0, rLevelTop = 0, rLevelBottom = 0; // post
    };

    void buildChannel (Channel& ch);
    void updatePots (double drive, double tone, double level);

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
    bool channel1Stale = false;
    bool channelsSynced = true;
    long long identicalRun = 0;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
