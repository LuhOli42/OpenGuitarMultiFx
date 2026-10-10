#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An EHX Bass Big Muff Pi-style fuzz, modelled at component level on NodalCircuit from the traced schematic
    (see docs/circuits/BassBigMuffStyleFuzz.md). It is the Sovtek/Russian-family Big Muff circuit (the same four
    NPN shunt-feedback cells as BigMuffStyleFuzzProcessor's russian model) with the two bass-specific additions:

      - BASS BOOST toggle: switches a much larger input-coupling capacitor in, letting the full bass register
        into the clipping stages (modelled as the C1 value switch documented in the doc);
      - DRY toggle: sums a constant-level dry path (tapped pre-Sustain) into the output node, the Volume knob
        then adding distortion on top of the dry -- exactly the real pedal's "Volume becomes a blend" behaviour.

    Controls: Sustain, Tone, Volume + Bass Boost and Dry toggles.
*/
class BassBigMuffStyleFuzzProcessor : public EffectProcessor
{
public:
    BassBigMuffStyleFuzzProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Bass Big Muff-Style Fuzz"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff5f6fb4); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugCollector (int stage) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit c;
        int srcIn = 0;
        NodalCircuit::Node nCollector[4] {};
        NodalCircuit::Node nOut = 0;
        int cIn = 0;                          // input coupling cap, switched by Bass Boost
        int rDry = 0;                         // dry-path resistor, switched by the DRY toggle
        int rSustainTop = 0, rSustainBottom = 0;
        int rToneTreble = 0, rToneBass = 0;
        int rVolumeTop = 0, rVolumeBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double sustain, double tone, double volume);
    void updateSwitches();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* bassBoostParam = nullptr;
    juce::AudioParameterFloat* dryParam = nullptr;
    int appliedBoost = -1, appliedDry = -1;

    juce::SmoothedValue<float> smoothedSustain, smoothedTone, smoothedVolume;

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

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
