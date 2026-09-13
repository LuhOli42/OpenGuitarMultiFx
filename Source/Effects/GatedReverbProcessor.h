#pragma once

#include "EffectProcessor.h"
#include "EnvelopeFollower.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A lush comb/allpass tank (same family as HallReverbProcessor, 4 combs +
    2 allpass, feedback tuned for a noticeably big tail) with a noise-gate
    (EnvelopeFollower, same primitive GateProcessor uses) wrapped around
    the WET signal only. The gate opens the instant the dry input crosses
    the threshold, stays open for Hold milliseconds after the input drops
    back below it, then closes fast -- chopping the reverb tail off
    abruptly instead of letting it decay naturally. That hard cut is the
    entire "gated reverb" effect (classic 80s drum sound); the dry signal
    itself is never gated, only what's blended in on top of it.
*/
class GatedReverbProcessor : public EffectProcessor
{
public:
    GatedReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Gated"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct CombFilter
    {
        std::vector<float> buffer;
        int pos = 0;
        float filterStore = 0.0f;
        float feedback = 0.5f;
        float damp1 = 0.3f;
        float damp2 = 0.7f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
            filterStore = 0.0f;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; filterStore = 0.0f; }

        float process (float input) noexcept
        {
            const float output = buffer[(size_t) pos];
            filterStore = output * damp2 + filterStore * damp1;
            buffer[(size_t) pos] = input + filterStore * feedback;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return output;
        }
    };

    struct AllpassFilter
    {
        std::vector<float> buffer;
        int pos = 0;
        float feedback = 0.5f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float process (float input) noexcept
        {
            const float bufOut = buffer[(size_t) pos];
            const float output = -input + bufOut;
            buffer[(size_t) pos] = input + bufOut * feedback;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return output;
        }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* holdMs = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    EnvelopeFollower inputDetector;
    EnvelopeFollower gateSmoother;
    double holdRemainingSamples = 0.0;
    bool gateOpen = false;

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
