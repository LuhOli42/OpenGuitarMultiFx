#pragma once

#include "AsymmetricDiodePair.h"
#include "EbersMollBJT.h"
#include "EffectProcessor.h"
#include "TrapezoidalCapacitor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A BOSS OD-1-style overdrive -- modelled directly from the OD-1's
    factory board schematic ("ET-23D"), the historically-credited
    ancestor of the Ibanez Tube Screamer's "diode(s) in the op-amp's
    negative feedback loop" clipping topology, but with a genuinely
    ASYMMETRIC diode count (1 one way, 2 in series the other) giving it a
    documented smoother/warmer character than a Tube Screamer's symmetric
    pair. See docs/circuits/OD1StyleOverdrive.md for the full circuit
    writeup -- read that file to understand this processor, not this
    comment or the .cpp.

    Signal path: Q6 (NPN emitter-follower input buffer) -> op-amp 1 (the
    clipper: an inverting stage whose feedback resistance -- fixed R5 in
    series with the Drive pot -- has an asymmetric diode pair bridged
    directly across it, solved via a genuine 1D Newton-Raphson each
    sample since the feedback network itself is nonlinear) -> op-amp 2
    (a fixed-gain, fully linear/closed-form treble-cut buffer) -> Level
    pot -> Q1 (JFET, modelled as a closed bypass switch -- see the docs
    file for why) -> Q7 (NPN emitter-follower output buffer).

    Only two controls, matching the real pedal exactly: Drive (VR1) and
    Level (VR2) -- the OD-1 has no Tone knob (that came later, with the
    SD-1/DS-1), so don't add one here.
*/
class OD1StyleOverdriveProcessor : public EffectProcessor
{
public:
    OD1StyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "OD-1-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8622a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Exposes channel 0's last-solved Q6 (input buffer) terminal voltages
    // -- same permanent verification hook as the booster/DS-1's own
    // getDebugBiasPoint(): boundedness/no-NaN tests alone can't
    // distinguish a correctly-biased buffer from one silently stuck at
    // cutoff. See Tests/OD1StyleOverdriveProcessorTests.cpp.
    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        double vb, ve, vc;
        channels[0].q6.getLastSolved (vb, ve, vc);
        return { (float) vb, (float) ve, (float) vc };
    }

private:
    /** Per-channel circuit state -- every reactive element's history plus
        every nonlinear device's own Newton-Raphson warm-start point, so
        channel 1/2 never leak state into each other (stereo in, but the
        real circuit this models is mono -- run identically per channel,
        same convention every other circuit-modelled processor here uses). */
    struct ChannelState
    {
        TrapezoidalCapacitor c1, c2;
        EbersMollBJT q6;

        TrapezoidalCapacitor c4; // op-amp 2's feedback treble-cut

        AsymmetricDiodePair clipper;

        TrapezoidalCapacitor c5, c7, c8;
        EbersMollBJT q7;
    };
    std::array<ChannelState, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    // Every pot-derived resistance below feeds directly into this
    // sample's Thevenin-network solve -- computing it fresh from the raw
    // knob value once per BLOCK (as the DS-1/booster's very first pass
    // did) means a knob move is a step discontinuity in circuit topology
    // between blocks, not just in the signal: an instant, unsmoothed
    // jump in feedback/divider resistance, which is exactly what
    // produces an audible zipper/crackle -- the same reasoning
    // PositiveGroundBoosterProcessor's own smoothedBoostPotResistance
    // already exists for. rTopToWiper alone is smoothed for the Level
    // pot (not rWiperToBottom too) -- deriving the other from
    // `levelMax - rTopToWiper` keeps the pot's own physical constraint
    // (the two segments always sum to its total resistance) exact,
    // rather than letting two independently-smoothed values drift apart
    // mid-ramp.
    juce::SmoothedValue<float> smoothedRFeedback;
    juce::SmoothedValue<float> smoothedRTopToWiper;

    double sampleRate = 0.0;
    double settledSampleRate = -1.0; // see prepare()'s doc comment: guards against re-settling on a same-rate re-prepare

    // See docs/circuits/OD1StyleOverdrive.md for where every one of these
    // numbers comes from and what simplifications/assumptions each one
    // carries. "r6OpAmpBias" is genuinely R6 on the real schematic --
    // renamed here only because the schematic reuses "R6" for the
    // unrelated 33K BIAS1-divider resistor, which C++ can't do.
    static constexpr float r1 = 1.0e3f;
    static constexpr float r2 = 470.0e3f;
    static constexpr float r3 = 10.0e3f;
    static constexpr float r4 = 100.0e3f;
    static constexpr float r5 = 33.0e3f;
    static constexpr float r7 = 10.0e3f;
    static constexpr float r8 = 10.0e3f;
    static constexpr float r10 = 4.7e3f;
    static constexpr float r12 = 1.0e6f;
    static constexpr float r13 = 10.0e3f;

    static constexpr float c1Value = 0.047e-6f;
    static constexpr float c2Value = 0.0047e-6f;
    static constexpr float c4Value = 0.018e-6f;
    static constexpr float c5Value = 1.0e-6f;
    static constexpr float c7Value = 0.047e-6f;
    static constexpr float c8Value = 1.0e-6f;

    static constexpr float driveMax = 1.0e6f;
    static constexpr float levelMax = 10.0e3f;

    static constexpr float closedSwitchResistance = 10.0f; // Q1 modelled as a closed bypass switch, see docs
    static constexpr float outputLoadResistance = 1.0e6f;  // assumed downstream input impedance
    static constexpr float supplyVoltage = 9.0f;
    static constexpr float bias1 = supplyVoltage * 0.5f; // R6/R7+R30 divider, see docs -- treated as an ideal fixed rail

    static constexpr double diodeSaturationCurrent = 2.52e-9; // 1N4148-class, matches D5/D6/D7's schematic role
    static constexpr double diodeThermalVoltage = 25.85e-3;
};

} // namespace openguitarmultifx
