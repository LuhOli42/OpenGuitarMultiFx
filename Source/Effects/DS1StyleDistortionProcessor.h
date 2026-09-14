#pragma once

#include "EbersMollBJT.h"
#include "EffectProcessor.h"
#include "ShichmanHodgesJFET.h"
#include "TrapezoidalCapacitor.h"

#include <chowdsp_wdf/chowdsp_wdf.h>

#include <array>

namespace openguitarmultifx
{

/**
    A BOSS DS-1-style distortion -- modelled directly from the original
    (pre-1994, TA7136P-based) DS-1 factory board schematic, cross-checked
    stage-by-stage against ElectroSmash's independent analysis of a later
    revision. See docs/circuits/DS1StyleDistortion.md for the full circuit
    writeup (topology, every stage's role, every simplification made and
    why) -- read that file to understand this processor, not this comment
    or the .cpp.

    Signal path: Q1 (NPN emitter-follower input buffer) -> Q6 (JFET used
    as a voltage-controlled resistor, not a gain stage -- biased at
    Vgs≈0) -> Q2 (NPN common-emitter gain stage with collector-to-base
    shunt feedback) -> an ideal-op-amp non-inverting gain stage (Drive pot
    sets closed-loop gain, solved in closed form -- no Newton-Raphson
    needed for an ideal op-amp) -> anti-parallel diode-to-AC-ground hard
    clipping (chowdsp_wdf's DiodePairT, the one genuine WDF use in this
    processor) -> a passive Big Muff-style Tone stack -> Level pot ->
    Q7 (JFET, modelled as a closed bypass switch -- see the docs file for
    why) -> Q3 (NPN emitter-follower output buffer).

    Three controls, matching the three pots the real pedal has: Drive
    (VR1), Tone (VR2), Level (VR3) -- all linear ("B") taper per the
    schematic's own pot markings, unlike the booster's single log-taper
    pot.
*/
class DS1StyleDistortionProcessor : public EffectProcessor
{
public:
    DS1StyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "DS-1-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8622a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Exposes channel 0's last-solved Q2 terminal voltages -- Q2 is this
    // circuit's main gain stage, so this is the same kind of permanent
    // verification hook PositiveGroundBoosterProcessor's
    // getDebugBiasPoint() is: boundedness/no-NaN tests alone can't
    // distinguish a correctly-biased gain stage from one silently stuck
    // at cutoff. See Tests/DS1StyleDistortionProcessorTests.cpp.
    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        double vb, ve, vc;
        channels[0].q2.getLastSolved (vb, ve, vc);
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
        // Pregain: Q1 (input buffer) -> Q6 (JFET VCR) -> Q2 (gain stage).
        TrapezoidalCapacitor c1, c2, c3, c4, c5;
        EbersMollBJT q1, q2;
        ShichmanHodgesJFET q6;
        double lastQ2Vb = 0.0, lastQ2Vc = 0.0; // for R7/C4's one-sample-delayed feedback, see docs

        // Op-amp gain stage + diode clipper.
        TrapezoidalCapacitor c8, c9, c10;
        chowdsp::wdft::ResistiveVoltageSourceT<double> diodeSource;
        std::unique_ptr<chowdsp::wdft::DiodePairT<double, chowdsp::wdft::ResistiveVoltageSourceT<double>>> diodePair;

        // Tone stack + Level.
        TrapezoidalCapacitor c11, c12;

        // Output buffer: Q7 (closed switch) -> Q3.
        TrapezoidalCapacitor c13, c14;
        EbersMollBJT q3;
    };
    std::array<ChannelState, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    double sampleRate = 0.0;

    // See docs/circuits/DS1StyleDistortion.md for where every one of
    // these numbers comes from and what simplifications/assumptions each
    // one carries.
    static constexpr float r1 = 1.0e3f;
    static constexpr float r2 = 470.0e3f;
    static constexpr float r3 = 10.0e3f;
    static constexpr float r4 = 100.0e3f;
    static constexpr float r5 = 1.0e6f;
    static constexpr float r6 = 100.0e3f;
    static constexpr float r7 = 470.0e3f;
    static constexpr float r8 = 10.0e3f;
    static constexpr float r9 = 22.0f;
    static constexpr float r11 = 100.0e3f;
    static constexpr float r13 = 4.7e3f;
    static constexpr float r14 = 2.2e3f;
    static constexpr float r15 = 2.2e3f;
    static constexpr float r16 = 6.8e3f;
    static constexpr float r17 = 6.8e3f;
    static constexpr float r18 = 10.0e3f;
    static constexpr float r19 = 1.0e6f;
    static constexpr float r21 = 10.0e3f;

    static constexpr float c1Value = 0.047e-6f;
    static constexpr float c2Value = 0.47e-6f;
    static constexpr float c3Value = 0.047e-6f;
    static constexpr float c4Value = 250.0e-12f;
    static constexpr float c5Value = 0.47e-6f;
    static constexpr float c8Value = 1.0e-6f;
    static constexpr float c9Value = 0.47e-6f;
    static constexpr float c10Value = 0.01e-6f;
    static constexpr float c11Value = 0.022e-6f;
    static constexpr float c12Value = 0.1e-6f;
    static constexpr float c13Value = 0.047e-6f;
    static constexpr float c14Value = 1.0e-6f;

    static constexpr float driveMax = 100.0e3f;
    static constexpr float toneMax = 100.0e3f;
    static constexpr float levelMax = 20.0e3f;

    static constexpr float closedSwitchResistance = 10.0f; // Q7 modelled as a closed bypass switch, see docs
    static constexpr float outputLoadResistance = 1.0e6f; // assumed downstream input impedance
    static constexpr float supplyVoltage = 9.0f;
    static constexpr float bias1 = supplyVoltage * 0.5f; // R24/R25 divider, see docs -- treated as an ideal fixed rail

    static constexpr double diodeSaturationCurrent = 2.52e-9; // 1N4148-class, matches D4/D5's schematic role
    static constexpr double diodeThermalVoltage = 25.85e-3;
};

} // namespace openguitarmultifx
