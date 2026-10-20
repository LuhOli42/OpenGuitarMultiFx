#pragma once

#include "Effects/EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A cascade of first-order allpass filters whose break frequency is
    swept by a sine LFO, mixed with the dry signal -- genuinely different
    machinery from Flanger/Chorus/Vibrato's modulated DELAY LINE. An
    allpass filter passes every frequency at unity gain but shifts phase
    by a frequency-dependent amount; several in series plus the dry signal
    produces a series of moving notches (frequencies where the shifted and
    dry signals land 180 degrees apart and cancel) -- the phaser's
    characteristic sweep, without ever delaying or repeating the signal
    the way a time-domain effect would.
*/
class PhaserProcessor : public EffectProcessor
{
public:
    PhaserProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Phaser"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // Swept allpass break-frequency range -- public so the FastMath domain
    // tests can check the tan() argument stays inside its fit domain.
    static constexpr float minFreqHz = 200.0f;
    static constexpr float maxFreqHz = 2000.0f;

private:
    struct AllpassStage
    {
        float state = 0.0f;

        /** `coeff` in (-1, 1); see the class doc comment for the classic
            first-order allpass form this implements. */
        float process (float input, float coeff) noexcept
        {
            const float output = -coeff * input + state;
            state = input + coeff * output;
            return output;
        }

        void clear() { state = 0.0f; }
    };

    static constexpr int numStages = 4;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    // One cascade per channel -- shared state would leak left/right phase
    // shifts into each other.
    std::array<std::array<AllpassStage, numStages>, 2> stages;
    std::array<float, 2> lastCascadeOutput {};

    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;

    juce::SmoothedValue<float> smoothedRateHz;
    juce::SmoothedValue<float> smoothedDepth;
    juce::SmoothedValue<float> smoothedFeedback;
    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
