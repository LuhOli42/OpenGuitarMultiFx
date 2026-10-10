#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Mu-Tron III-style envelope filter (the circuit the MXR M82 Bass Envelope Filter also derives from):
    an inverting input amplifier -> a three-op-amp state-variable filter whose two integrator legs are
    swept by the LDR of a lamp/photocell optocoupler -> a precision-rectifier + RC envelope detector
    driving the optocoupler's LED. The SVF runs as an exact closed-form trapezoidal discretisation of
    the summer + two inverting integrators (derived in docs/circuits/MuTronIIIStyleFilter.md).

    Front-panel controls, as on the real unit: Gain (input amp + sensitivity), Peak (resonance,
    the positive-feedback path), Mode (LP / BP / HP output select), Range (Lo / Hi integrator caps),
    Drive (Up / Down sweep direction).
*/
class MuTronIIIStyleFilterProcessor : public EffectProcessor
{
public:
    MuTronIIIStyleFilterProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Mu-Tron III-Style Filter"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8f6fb0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics (channel 0): the current sweep resistance and the detector's envelope voltage. */
    double debugSweepOhms() const noexcept { return channels[0].debugReff; }
    double debugEnvelope() const noexcept { return channels[0].debugEnv; }

private:
    struct Channel
    {
        // Envelope detector + optocoupler state
        double env = 0.0;       // precision-rectifier output held by the attack/release RC
        double ldr = 0.0;       // the CdS cell's slow response (second one-pole)
        // SVF state (previous-sample integrator outputs and summer output)
        double bp = 0.0, lp = 0.0, hp = 0.0;
        double debugReff = 0.0, debugEnv = 0.0;
    };
    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* peak = nullptr;
    juce::AudioParameterFloat* mode = nullptr;   // 0 LP, 1 BP, 2 HP
    juce::AudioParameterFloat* range = nullptr;  // 0 Lo, 1 Hi
    juce::AudioParameterFloat* drive = nullptr;  // 0 Up, 1 Down

    juce::SmoothedValue<float> smoothedGain;

    double sampleRate = 0.0;

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }
};

} // namespace openguitarmultifx
