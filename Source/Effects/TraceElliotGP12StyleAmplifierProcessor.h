#pragma once

#include "Biquad.h"
#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Trace Elliot GP12 SMX-style bass preamp, simplified to its dual-band compressor + 12-band
    graphic EQ character (docs/circuits/TraceElliotGP12StyleAmplifier.md says what is modelled and
    what is simplified). Solid-state throughout -- no valves, no power stage.

    Signal path: input gain -> the two fixed Pre-Shape EQ curves (as shelving/peaking filters) ->
    the SMX crossover (a complementary split so EQ BALANCE is flat at centre) -> the low-band and
    high-band compressors (each with its own detector speed, as on the real unit) -> EQ BALANCE
    crossfade -> the 12-band graphic equaliser, modelled as the same gyrator-into-op-amp topology
    as the Boss-style graphic EQs, the 12 bands distributed across two cascaded stages (which is
    also how real multi-band graphics keep their op-amps' feedback networks manageable) -> Graphic
    Level -> output.

    Page 1: Input Gain, Pre Shape, Low Compression, EQ Balance, High Compression, Graphic,
    Graphic Level, Output. Page 2: the twelve +-15 dB band sliders.
*/
class TraceElliotGP12StyleAmplifierProcessor : public EffectProcessor
{
public:
    static constexpr int numBands = 12;

    TraceElliotGP12StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Trace Elliot GP12-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f8f6f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    bool dcConverged() const noexcept { return dcOk; }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

    /** Diagnostics (channel 0): the two graphic stages' outputs. */
    double debugEq1Out() const noexcept { return channels[0].eq1.voltage (channels[0].nEq1Out); }
    double debugEq2Out() const noexcept { return channels[0].eq2.voltage (channels[0].nEq2Out); }

    static constexpr std::array<double, numBands> bandHz {
        30.0, 40.0, 60.0, 100.0, 180.0, 340.0, 660.0, 1300.0, 2600.0, 5000.0, 10000.0, 15000.0
    };

private:
    struct Channel
    {
        // Pre-shape filters (low shelf, mid cut, high shelf)
        Biquad preLo, preMid, preHi;
        // Crossover + compressor state
        double lpX = 0.0;                    // the complementary crossover's low-pass state
        double envLo = 0.0, envHi = 0.0;     // each band's peak detector
        // Graphic EQ: two cascaded 6-band gyrator stages (each fits one NodalCircuit)
        NodalCircuit eq1, eq2;
        int srcEq1 = 0, srcEq2 = 0;
        NodalCircuit::Node nEq1Out = 0, nEq2Out = 0;
        std::array<int, numBands> rUp {}, rDown {};
    };
    std::array<Channel, 2> channels;

    void buildChannel (Channel& ch);
    void updateSliders();
    void updatePreShape();

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputGain = nullptr;
    juce::AudioParameterFloat* preShape = nullptr;
    juce::AudioParameterFloat* lowCompression = nullptr;
    juce::AudioParameterFloat* eqBalance = nullptr;
    juce::AudioParameterFloat* highCompression = nullptr;
    juce::AudioParameterFloat* graphic = nullptr;
    juce::AudioParameterFloat* graphicLevel = nullptr;
    juce::AudioParameterFloat* output = nullptr;
    std::array<juce::AudioParameterFloat*, numBands> bands {};

    std::array<float, numBands> appliedSliders {};
    int appliedShape = -1;
    bool appliedGraphic = false;

    juce::SmoothedValue<float> smoothedInputGain, smoothedBalance, smoothedOutput;

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

    static constexpr int controlInterval = 64;
};

} // namespace openguitarmultifx
