#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A diode ring modulator: the guitar multiplied by a carrier through the four-diode bridge of the classic units (Maestro RM-1, Moog
    MF-102, the Dalek voice), not by an ideal multiplier -- so it has their harsh, slightly gritty sidebands and a carrier that is
    cancelled without being perfectly gone. The bridge is Julian Parker's published digital model (DAFx-11, "A simple digital model of
    the diode-based ring-modulator"): out = D (c + x/2) - D (c - x/2), with D the piecewise diode shaper of his eq. (2) (bias vb = 0.2 V,
    knee vL = 0.4 V, slope h = 1, values of the paper's own reference implementation). See docs/circuits/RingMod.md.

    Controls: Frequency (the carrier, 0.5 Hz .. 5 kHz), Drive (how hard the guitar hits the diodes, x0.5 .. x8), Shape (sine, triangle or a
    rounded square carrier), Mix. The nonlinearity generates harmonics that fall 20 dB per octave, so the registry runs it oversampled.
*/
class RingModProcessor : public EffectProcessor
{
public:
    RingModProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Ring Mod"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8a5cd6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Parker's diode shaper, exposed for tests. */
    static double diode (double v, double vb = 0.2, double vl = 0.4, double h = 1.0) noexcept;

private:
    double carrier (double phase, int shape) const noexcept;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* frequency = nullptr;
    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* shape = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::SmoothedValue<float> smoothedFrequency, smoothedDrive, smoothedMix;
    double sampleRate = 0.0;
    double phase = 0.0;
};

} // namespace openguitarmultifx
