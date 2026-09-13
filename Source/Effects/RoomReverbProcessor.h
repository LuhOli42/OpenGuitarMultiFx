#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    Same comb/allpass family as HallReverbProcessor, but 4 short combs
    (7-17ms) and 2 short allpass stages instead of Hall's 8 long combs
    (25-37ms) and 4 allpass -- a small enclosed space's tight, fast early
    reflections rather than a hall's dense, slow-building wash. Decay is
    also capped lower (max feedback ~0.9 vs Hall's ~0.98) since a real
    small room's reflections die out in a few hundred ms, not seconds.
*/
class RoomReverbProcessor : public EffectProcessor
{
public:
    RoomReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Room"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct CombFilter
    {
        std::vector<float> buffer;
        int pos = 0;
        float filterStore = 0.0f;
        float feedback = 0.5f;
        float damp1 = 0.5f;
        float damp2 = 0.5f;

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
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
