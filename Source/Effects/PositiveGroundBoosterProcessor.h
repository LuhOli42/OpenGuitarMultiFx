#pragma once

#include "EbersMollBJT.h"
#include "EffectProcessor.h"
#include "TrapezoidalCapacitor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Rangemaster-style single-transistor "positive ground" treble
    booster -- modelled directly from General Guitar Gadgets' GEB
    schematic (https://generalguitargadgets.com/pdf/ggg_geb_pos_sc.pdf),
    per explicit user request for maximum circuit fidelity, not the
    easiest-to-implement approximation. See docs/circuits/
    PositiveGroundBooster.md for the full circuit writeup (topology,
    every component's role, why this modelling approach was chosen) --
    that file is the one to read to understand this processor, not this
    comment or the .cpp.

    Summary of the modelling approach (detail in the docs/circuits file):
    a genuine large-signal Ebers-Moll PNP transistor (EbersMollBJT.h,
    solved via 3D Newton-Raphson every sample) plus trapezoidal-
    discretized companion models for every capacitor (TrapezoidalCapacitor.h),
    combined via direct nodal analysis -- NOT chowdsp_wdf's generic WDF
    tree, which was researched and found unnecessary for a single-
    transistor circuit where every passive part connects directly to one
    of the transistor's three terminals (see the docs file for the full
    reasoning, informed by researching Chowdhury-DSP's own BYOD
    RangeBooster -- a near-identical circuit -- which makes the same
    architectural choice).

    Only one control exists on the real pedal -- Boost (the collector-
    load pot) -- so that's the only parameter exposed here; adding an
    extra mix/output-trim knob the real circuit doesn't have would work
    against the fidelity this processor exists for.
*/
class PositiveGroundBoosterProcessor : public EffectProcessor
{
public:
    PositiveGroundBoosterProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Rangemaster-Style Booster"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8622a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Exposes channel 0's last-solved (real, unmirrored) terminal voltages
    // -- a permanent verification hook, not leftover debug scaffolding:
    // circuit-modelled processors need a way for their tests to assert
    // the DC operating point is physically sane (forward-biased junction,
    // real collector current) rather than only checking output is
    // finite/bounded, which alone can't distinguish a correctly-biased
    // stage from one stuck at cutoff. See Tests/PositiveGroundBoosterProcessorTests.cpp.
    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept;

private:
    /** Per-channel circuit state -- every reactive element's history plus
        the transistor's own Newton-Raphson warm-start point, so
        channel 1/2 never leak state into each other (stereo in, but the
        real circuit this models is mono -- run identically per channel,
        same convention every other per-channel processor here uses). */
    struct ChannelState
    {
        TrapezoidalCapacitor c1, c4, c2;
        EbersMollBJT transistor;
        double lastVb = 0.0, lastVe = 0.0, lastVc = 0.0; // real (unmirrored), see getDebugBiasPoint()
    };
    std::array<ChannelState, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* boost = nullptr;

    juce::SmoothedValue<float> smoothedBoostPotResistance;

    double sampleRate = 0.0;

    // See docs/circuits/PositiveGroundBooster.md for where every one of
    // these numbers comes from (component values off the schematic,
    // supply/transistor parameters documented as explicit, adjustable
    // assumptions where the schematic itself doesn't pin them down).
    static constexpr float r1 = 470.0e3f;
    static constexpr float r2 = 68.0e3f;
    static constexpr float r4 = 3.9e3f;
    static constexpr float c1Value = 0.0047e-6f;
    static constexpr float c4Value = 47.0e-6f;
    static constexpr float c2Value = 0.01e-6f;
    static constexpr float boostPotMax = 10.0e3f;
    static constexpr float outputLoadResistance = 1.0e6f; // assumed downstream input impedance
    static constexpr float supplyVoltage = 8.4f;           // 9V battery minus D1's ~0.6V forward drop
};

} // namespace openguitarmultifx
