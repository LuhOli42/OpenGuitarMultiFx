#pragma once

#include "AsymmetricDiodePair.h"
#include "EbersMollBJT.h"
#include "EffectProcessor.h"
#include "TrapezoidalCapacitor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A TS808 / TS9 / TS10-style overdrive -- one processor class, three
    registered models, because the three pedals share one circuit: per R.G.
    Keen's "The Technology of the Tube Screamer" (Geofex), the TS808 and
    TS9 differ ONLY in the op-amp type and two output-buffer resistors, and
    the TS10 differs from the TS9 in a few small ways -- modelled from a
    real TS-10 schematic (Q1's higher bias voltage, a 220ohm ahead of the
    op-amp's bias node, an extra 1uF + JFET-bias resistors after the Level
    pot). See docs/circuits/TubeScreamerStyleOverdrive.md, which is the
    file to read to understand this processor, not this comment or the .cpp.

    Signal path: emitter-follower input buffer (Q1) -> non-inverting op-amp
    clipper (diodes + a small cap in the op-amp's OWN feedback loop,
    solved with a 1D Newton-Raphson each sample -- same family as the
    OD-1, but a SYMMETRIC diode pair) -> passive lowpass + active tone
    stage (op-amp 2, fully linear/closed-form) -> Level pot -> emitter-
    follower output buffer (Q2).

    Three controls, matching the real pedals: Drive, Tone, Level.
*/
class TubeScreamerStyleOverdriveProcessor : public EffectProcessor
{
public:
    enum class Model { ts808, ts9, ts10 };

    explicit TubeScreamerStyleOverdriveProcessor (Model model = Model::ts808);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4f9a3c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Channel 0's last-solved Q1 (input buffer) terminal voltages -- same
    // permanent verification hook as the booster/DS-1/OD-1: a bounded,
    // NaN-free output can't tell a correctly-biased buffer from one
    // silently stuck at cutoff.
    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        return { (float) channels[0].debugQ1Vb, (float) channels[0].debugQ1Ve, (float) supplyVoltage };
    }

    /** Fraction of the failed-to-converge diode solves since prepare() --
        a test hook: the clipper must converge on essentially every sample,
        or the "held previous value" fallback becomes audible. */
    double getClipperFailureRate() const noexcept
    {
        return solveCount > 0 ? (double) solveFailures / (double) solveCount : 0.0;
    }

private:
    /** What actually differs between the models (everything else is shared).
        TS808/TS9 differ only in R14/R15 (Geofex). The TS10 fields come from
        a real TS-10 schematic (I. Nuvoli, 1997) -- see the doc. */
    struct ModelSpec
    {
        const char* displayName;
        const char* idPrefix;
        const char* groupId;
        float outputSeriesResistance;      // R14 -- emitter to the 10uF coupling cap
        float outputShuntResistance;       // R15 -- after the cap, to ground
        float q1BiasVoltage;               // rail Q1's 510K base-bias resistor returns to
        float clipperInputSeriesResistance; // resistor between C2 and the (+) pin's 10K bias node
        float toneBiasRail;                // rail op-amp 2's 10K (R10) returns to
        float levelBottomVoltage;          // what the Level pot's grounded end is tied to
        bool hasWiperCoupling;             // extra 1uF after Level + 510K JFET-bias resistors (TS10)
    };

    static const ModelSpec& specFor (Model model) noexcept;

    /** Per-channel circuit state -- every reactive element's history plus
        every nonlinear device's own Newton-Raphson warm-start point. */
    struct ChannelState
    {
        TrapezoidalCapacitor c1, c2, c3, c4, c5, c6, c7, c8, c9, cWiper;
        double debugQ1Vb = 0.0, debugQ1Ve = 0.0; // Q1 is an ideal follower now (see process()); kept for the bias-point test hook
        AsymmetricDiodePair clipper;
    };
    std::array<ChannelState, 2> channels;

    const ModelSpec& spec;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    // Every pot-derived resistance feeds straight into the per-sample
    // solve, so a per-block jump would be a step change in circuit
    // topology (audible zipper) -- smoothed, same reasoning as the OD-1's
    // smoothedRFeedback. Only one segment of each pot is smoothed; the
    // other is derived as (total - segment) so the pot's physical
    // constraint stays exact mid-ramp.
    juce::SmoothedValue<float> smoothedRDrive;
    juce::SmoothedValue<float> smoothedRToneWiperToPlus;   // tone pot: pin 1 ((+) input side) to wiper
    juce::SmoothedValue<float> smoothedRLevelWiperToBottom; // level pot: wiper to ground

    double sampleRate = 0.0;
    double settledSampleRate = -1.0; // see prepare(): guards against re-settling on a same-rate re-prepare

    long long solveCount = 0;
    long long solveFailures = 0;

    // Component values -- see docs/circuits/TubeScreamerStyleOverdrive.md
    // for the source of every one. Designators follow the TS808 schematic
    // used there (R1..R15 / C1..C9); R14/R15 live in ModelSpec.
    static constexpr float r1 = 1.0e3f;
    static constexpr float r2 = 510.0e3f;
    static constexpr float r3 = 10.0e3f;
    static constexpr float r4 = 4.7e3f;
    static constexpr float r5 = 10.0e3f;
    static constexpr float r6 = 51.0e3f;
    static constexpr float r7 = 1.0e3f;
    static constexpr float r8 = 220.0f;
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
    static constexpr float c6Value = 0.22e-6f;
    static constexpr float c7Value = 1.0e-6f;
    static constexpr float c8Value = 0.1e-6f;
    static constexpr float c9Value = 10.0e-6f;
    static constexpr float cWiperValue = 1.0e-6f;         // TS10 only: coupling cap right after the Level wiper
    static constexpr float jfetBiasResistance = 510.0e3f; // TS10 only: the bypass JFET's source/drain bias resistors to 4.5V

    static constexpr float driveMax = 500.0e3f; // A500K (audio taper, approximated as knob^2)
    static constexpr float toneMax = 20.0e3f;   // B20K (linear)
    static constexpr float levelMax = 100.0e3f; // A100K (audio taper, approximated as knob^2)

    static constexpr float closedSwitchResistance = 100.0f; // JFET bypass switch, "on": ~100ohm per Geofex
    static constexpr float outputLoadResistance = 1.0e6f;   // assumed downstream input impedance
    static constexpr float supplyVoltage = 9.0f;
    static constexpr double followerDrop = 0.62; // Vbe of Q1/Q2 (emitter followers at ~0.35 mA), see process()
    static constexpr float bias = supplyVoltage * 0.5f; // R16/R17 divider + C11, treated as an ideal 4.5V rail

    // Standard SPICE model shared by the 1N914 / 1N4148 (the diodes the
    // real pedals use): Is = 2.52nA, emission coefficient N = 1.752. The
    // ideality factor matters: without it a 1N4148 would turn on at
    // ~0.33V instead of the real ~0.6V.
    static constexpr double diodeSaturationCurrent = 2.52e-9;
    static constexpr double diodeIdealityFactor = 1.752;
    static constexpr double diodeThermalVoltage = 25.85e-3;
};

} // namespace openguitarmultifx
