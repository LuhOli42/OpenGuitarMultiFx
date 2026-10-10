#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    Same allpass-cascade phase-shifting core as PhaserProcessor, but two
    deliberate differences that give it the Uni-Vibe's own character
    rather than being a re-skinned Phaser: the LFO shape is asymmetric
    (rises faster than it falls, `sin(phase) + 0.35*sin(2*phase)`) instead
    of a clean sine -- modelling the real pedal's photocell/lamp circuit,
    which doesn't respond symmetrically -- and the wet signal also carries
    a subtle amplitude throb synced to the same LFO, the lamp brightness
    audibly modulating the signal's level as well as its phase. Only 2
    stages, not Phaser's 4 -- a real Uni-Vibe's shift is shallower.
*/
class UniVibeProcessor : public EffectProcessor
{
public:
    UniVibeProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Uni-Vibe"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct AllpassStage
    {
        float state = 0.0f;

        float process (float input, float coeff) noexcept
        {
            const float output = -coeff * input + state;
            state = input + coeff * output;
            return output;
        }

        void clear() { state = 0.0f; }
    };

    static constexpr int numStages = 2;
    static constexpr float minFreqHz = 150.0f;
    static constexpr float maxFreqHz = 900.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<std::array<AllpassStage, numStages>, 2> stages;

    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
