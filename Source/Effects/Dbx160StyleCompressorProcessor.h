#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A dbx 160-style compressor: FEED-FORWARD, true-RMS detection, a gain computer that works in dB, and a VCA that follows its control
    exactly. What makes a 160 sound like one is the detector, and its published figures come out of a single first-order RMS averager:
    dbx's spec is an attack of 15 ms for a 10 dB step, 5 ms for 20 dB and 3 ms for 30 dB, and a release of 8 ms for 1 dB, 80 ms for 10 dB
    and 400 ms for 50 dB (a fixed ~125 dB/s). An averager of the signal's POWER with a 34 ms time constant gives 15 / 6.4 / 2.6 ms for
    the attack (the log of a linear average reaches its final value sooner the bigger the step) and 7.7 ms / 78 ms / 390 ms (130 dB/s) for
    the release -- so that is what this is (docs/circuits/Dbx160StyleCompressor.md). Over Easy is the soft knee (10 dB here: the width is
    not published); ratio from 1:1 to infinity.

    Controls: Threshold, Ratio, Output, Over Easy.
*/
class Dbx160StyleCompressorProcessor : public EffectProcessor
{
public:
    Dbx160StyleCompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "dbx 160-Style Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc44b4b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Current gain reduction (dB, >= 0) and RMS level (dBFS), for tests and meters. */
    double getGainReductionDb() const noexcept { return grDb; }
    double getLevelDb() const noexcept { return levelDb; }

    static constexpr double rmsSeconds = 0.034;
    static constexpr double kneeDb = 10.0;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* threshold = nullptr;
    juce::AudioParameterFloat* ratio = nullptr;
    juce::AudioParameterFloat* output = nullptr;
    juce::AudioParameterFloat* overEasy = nullptr;

    double sampleRate = 0.0;
    double power = 0.0;
    double levelDb = -120.0, grDb = 0.0;
};

} // namespace openguitarmultifx
