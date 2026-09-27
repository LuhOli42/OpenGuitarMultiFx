#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A UREI 1176-style FET limiting amplifier, built the way UREI's own 1176LN manual (section IV, "Theory of operation") describes the
    circuit, with the schematic (Rev F, in the same document) for the values:

    * an "L" section: R6 27K in series and a JFET (Q1) as the VOLTAGE-VARIABLE SHUNT resistor. Below the threshold the FET's quiescent
      bias holds it at a high resistance; above it, the side-chain lowers the bias and the resistance falls: gain reduction. A gate
      feedback of half the signal makes a JFET's resistance linear ("a unique circuit permits severe limiting without added
      distortion"); the residue of that cancellation is the 1176's own distortion, and it grows in all-buttons mode when the bias
      points move.
    * a preamplifier and line amplifier (fixed gain here), the output control after the preamplifier.
    * a FEEDBACK side-chain: the signal at the preamplifier output goes through the ratio divider, is rectified (full wave, diodes
      biased to make the threshold) and filtered by C27 with the ATTACK (20 us .. 800 us) and RELEASE (50 ms .. 1.1 s for 63%)
      resistors; the DC subtracts from the FET's bias. The four RATIO buttons pick the divider (4:1, 8:1, 12:1, 20:1) and the bias
      divider ganged with them: the threshold at maximum Input is -30 / -26 / -25 / -24 dB re 0.775 V for the four ratios (the manual's table).
    * ALL BUTTONS IN ("British mode"): the interlock lets all four dividers in parallel; the ratio goes above 20:1, the bias points move (more FET
      distortion) and attack and release change. The manual does not describe it; this model's numbers for it are estimates.

    Level convention: 0 dB re 0.775 V = -18 dBFS. See docs/circuits/Urei1176StyleCompressor.md.

    Controls: Input, Output, Attack, Release, Ratio (4 / 8 / 12 / 20 / all).
*/
class Urei1176StyleCompressorProcessor : public EffectProcessor
{
public:
    Urei1176StyleCompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "1176-Style Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff9aa3ad); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Gain reduction (dB, >= 0) at the last sample, for tests and meters. */
    double getGainReductionDb() const noexcept { return grDb; }

    struct RatioSpec
    {
        double sensitivity;      // volts of FET control per volt of side-chain signal above the threshold
        double thresholdDb;      // input level at minimum limiting with Input at maximum (dB re 0.775 V)
        double linearityError;   // 2c - 1 of the FET's gate feedback (0 = a perfectly linear resistor)
        double timeScale;        // all buttons changes the attack and release
    };

    static const RatioSpec& specFor (int ratioIndex) noexcept;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* input = nullptr;
    juce::AudioParameterFloat* output = nullptr;
    juce::AudioParameterFloat* attack = nullptr;
    juce::AudioParameterFloat* release = nullptr;
    juce::AudioParameterFloat* ratio = nullptr;

    double sampleRate = 0.0;
    double detector = 0.0; // the side-chain's DC (C27): volts above the threshold
    double grDb = 0.0;
};

} // namespace openguitarmultifx
