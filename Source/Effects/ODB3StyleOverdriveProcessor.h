#pragma once

#include "AsymmetricDiodePair.h"
#include "EffectProcessor.h"
#include "TrapezoidalCapacitor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss ODB-3-style bass overdrive, modelled with the same Thevenin-chain + Newton technique as
    the Tube Screamer family (see docs/circuits/ODB3StyleOverdrive.md for what is real and what is
    estimated -- the published ODB-3 schematic is only partly legible, so component values follow
    the documented topology rather than a full BOM).

    Signal path: JFET-style input buffer -> op-amp gain stage with the Gain pot and a symmetric
    silicon diode pair in its feedback loop (1D Newton per sample) -> a harder second clipper, the
    LED pair shunting the signal path (also Newton) -> a Low/High active tone stage (the same
    bridged-feedback Baxandall arrangement as the TS9B's) -> the Balance blend of the driven signal
    against a low-passed clean feed (the ODB-3's signature "bass stays clean" mix) -> Level pot ->
    output buffer.

    Five controls, matching the real pedal: Level, High, Low, Balance, Gain.
*/
class ODB3StyleOverdriveProcessor : public EffectProcessor
{
public:
    ODB3StyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "ODB-3-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc23c3c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct DebugBiasPoint { float vBase, vEmitter, vCollector; };
    DebugBiasPoint getDebugBiasPoint() const noexcept
    {
        return { (float) channels[0].debugQ1Vb, (float) channels[0].debugQ1Ve, (float) supplyVoltage };
    }

    double debugOp1Out() const noexcept { return channels[0].debugOp1Out; }

    double getClipperFailureRate() const noexcept
    {
        return solveCount > 0 ? (double) solveFailures / (double) solveCount : 0.0;
    }

private:
    struct ChannelState
    {
        TrapezoidalCapacitor c1, c2, c3, c4, c5, cLow, cHigh, cLp, c7, c8, c9;
        double debugOp1Out = 0.0;
        double debugQ1Vb = 0.0, debugQ1Ve = 0.0;
        AsymmetricDiodePair clipper;  // feedback diode pair (silicon)
        double ledV = 0.0;            // shunt LED clipper's Newton warm-start state
    };
    std::array<ChannelState, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterFloat* high = nullptr;
    juce::AudioParameterFloat* low = nullptr;
    juce::AudioParameterFloat* balance = nullptr;
    juce::AudioParameterFloat* gain = nullptr;

    juce::SmoothedValue<float> smoothedRGain;
    juce::SmoothedValue<float> smoothedRBalance;
    juce::SmoothedValue<float> smoothedRLowWiperToPlus;
    juce::SmoothedValue<float> smoothedRHighWiperToPlus;
    juce::SmoothedValue<float> smoothedRLevelWiperToBottom;

    double sampleRate = 0.0;
    double settledSampleRate = -1.0;

    long long solveCount = 0;
    long long solveFailures = 0;

    // Component values -- docs/circuits/ODB3StyleOverdrive.md. Where the published schematic is
    // illegible the values are estimated on the documented topology and flagged as such.
    static constexpr float r1 = 1.0e3f;
    static constexpr float r2 = 510.0e3f;
    static constexpr float r3 = 10.0e3f;
    static constexpr float r4 = 4.7e3f;
    static constexpr float r5 = 10.0e3f;
    static constexpr float r6 = 51.0e3f;
    static constexpr float rSeriesLed = 4.7e3f;  // resistor ahead of the LED shunt clipper (estimated)
    static constexpr float r7 = 1.0e3f;
    static constexpr float r8l = 470.0f;         // Low shunt resistor
    static constexpr float r8h = 220.0f;         // High shunt resistor
    static constexpr float r9 = 1.0e3f;
    static constexpr float r10 = 10.0e3f;
    static constexpr float r11 = 1.0e3f;
    static constexpr float r12 = 510.0e3f;
    static constexpr float r13 = 10.0e3f;
    static constexpr float rDry = 22.0e3f;       // the clean low path's mix resistor (estimated)
    static constexpr float rLp = 10.0e3f;        // clean path's low-pass R (fc ~ 480 Hz with cLp)

    static constexpr float c1Value = 0.02e-6f;
    static constexpr float c2Value = 1.0e-6f;
    static constexpr float c3Value = 0.047e-6f;
    static constexpr float c4Value = 51.0e-12f;
    static constexpr float c5Value = 0.22e-6f;
    static constexpr float cLowValue = 0.47e-6f;   // Low band shunt cap
    static constexpr float cHighValue = 0.22e-6f;  // High band shunt cap
    static constexpr float cLpValue = 0.033e-6f;   // clean path's low-pass cap (fc ~ 480 Hz)
    static constexpr float c7Value = 1.0e-6f;
    static constexpr float c8Value = 0.1e-6f;
    static constexpr float c9Value = 10.0e-6f;

    static constexpr float gainMax = 500.0e3f;
    static constexpr float toneMax = 20.0e3f;
    static constexpr float levelMax = 100.0e3f;
    static constexpr float balanceSum = 1.0e6f; // blend track; >> every source resistance so the ends truly isolate

    static constexpr float closedSwitchResistance = 100.0f;
    static constexpr float outputLoadResistance = 1.0e6f;
    static constexpr float supplyVoltage = 9.0f;
    static constexpr double followerDrop = 0.62;
    static constexpr float bias = supplyVoltage * 0.5f;

    // Feedback diodes: the 1N914-class silicon pair.
    static constexpr double diodeSaturationCurrent = 2.52e-9;
    static constexpr double diodeIdealityFactor = 1.752;
    static constexpr double diodeThermalVoltage = 25.85e-3;

    // The shunt LEDs: Vf ~1.7 V at ~1 mA. Is = 6e-12, nVt = 0.09 reproduces that knee.
    static constexpr double ledSaturationCurrent = 6.0e-12;
    static constexpr double ledNVt = 0.09;
};

} // namespace openguitarmultifx
