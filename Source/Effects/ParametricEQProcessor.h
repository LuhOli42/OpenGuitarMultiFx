#pragma once

#include "Biquad.h"
#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A four-band parametric equaliser: low shelf, two peaking bands (frequency, gain, Q) and a high shelf, plus an output Level. The
    "advanced" companion to the GE-7-style graphic equaliser. Not a copy of one unit: it is the classic console / rack layout (a low
    shelf, a low-mid and a high-mid bell, a high shelf) built from the standard cookbook biquads (bilinear transform, so the gain at the
    stated frequency is exact at any sample rate; docs/circuits/ParametricEQ.md). Each band's coefficients follow its smoothed knobs
    every 16 samples, so sweeping a knob does not zipper.

    Page 1: Low (Gain, Freq), Low-Mid (Gain, Freq, Q), Level. Page 2: High-Mid (Gain, Freq, Q), High (Gain, Freq).
*/
class ParametricEQProcessor : public EffectProcessor
{
public:
    ParametricEQProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Parametric EQ"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d8fd9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The four bands' combined magnitude (linear) at `freq` with the current knobs, for tests. */
    double responseAt (double freq) const noexcept;

private:
    void updateCoefficients() noexcept;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* lowGain = nullptr; juce::AudioParameterFloat* lowFreq = nullptr;
    juce::AudioParameterFloat* lmGain = nullptr; juce::AudioParameterFloat* lmFreq = nullptr; juce::AudioParameterFloat* lmQ = nullptr;
    juce::AudioParameterFloat* hmGain = nullptr; juce::AudioParameterFloat* hmFreq = nullptr; juce::AudioParameterFloat* hmQ = nullptr;
    juce::AudioParameterFloat* highGain = nullptr; juce::AudioParameterFloat* highFreq = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    // smoothed knob values (log domain for frequencies, dB for gains)
    juce::SmoothedValue<float> sLowGain, sLowFreq, sLmGain, sLmFreq, sLmQ, sHmGain, sHmFreq, sHmQ, sHighGain, sHighFreq, sLevel;

    std::array<std::array<Biquad, 4>, 2> sections; // [channel][low, low-mid, high-mid, high]
    double sampleRate = 0.0;
    double outputGain = 1.0;
    int controlCounter = 0;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
