#pragma once

#include "Biquad.h"
#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Drawmer DS201-style noise gate: the first frequency-conscious gate. Its controls and their ranges are the ones in the DS201's
    manual (Drawmer, "DS201 Dual Noise Gate": threshold -54 dB to infinity; attack 10 us .. 1 s; hold 2 ms .. 2 s; decay 2 ms .. 4 s;
    range 0 .. -80 dB; a side-chain low-cut filter 25 Hz .. 4 kHz and high-cut filter 250 Hz .. 35 kHz; key listen). The gate follows the
    manual's own description of the envelope: the hold cycle restarts while the key is above the threshold and starts to run when it falls
    below it, then the gain travels the whole range in the decay time; the attack is the time the gain takes to travel back. The threshold
    here is in dBFS (a DS201 has no digital full scale; 0 dBu is taken as -18 dBFS elsewhere in this project's studio units).
    See docs/circuits/DS201StyleNoiseGate.md.
*/
class DS201StyleNoiseGateProcessor : public EffectProcessor
{
public:
    DS201StyleNoiseGateProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "DS201-Style Noise Gate"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc8483a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The gate's current gain in dB (0 = open), for tests. */
    double debugGainDb() const noexcept { return gainDb; }

private:
    void updateFilters() noexcept;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* threshold = nullptr;
    juce::AudioParameterFloat* range = nullptr;
    juce::AudioParameterFloat* attack = nullptr;
    juce::AudioParameterFloat* hold = nullptr;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* lowCut = nullptr;
    juce::AudioParameterFloat* highCut = nullptr;
    juce::AudioParameterFloat* keyListen = nullptr;

    std::array<Biquad, 2> hpf, lpf; // per channel
    float appliedLow = -1.0f, appliedHigh = -1.0f;

    double sampleRate = 0.0;
    double detector = 0.0;   // the key's peak envelope (linear)
    double gainDb = 0.0;
    long long holdCounter = 0;
    bool open = false;
};

} // namespace openguitarmultifx
