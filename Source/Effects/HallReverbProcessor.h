#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A large-space algorithmic reverb -- the classic Schroeder-Moorer/Freeverb
    architecture (8 parallel damped combs summed, then 4 series allpass
    filters for diffusion), tuned for a big, dense hall rather than
    SpringReverbProcessor's short metallic "boing" (3 allpass into one comb)
    or ReverbProcessor/"Ambient"'s juce::dsp::Reverb wash. The 8 parallel
    combs at staggered lengths is what gives a hall its dense, smooth echo
    build-up instead of a single audible repeat.

    Decay controls each comb's feedback (how long the tail rings); Tone
    controls the in-loop damping (brightness of the tail as it decays).
*/
class HallReverbProcessor : public EffectProcessor
{
public:
    HallReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Hall"; }
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

    static constexpr int numCombs = 8;
    static constexpr int numAllpassStages = 4;
    // Per-channel banks -- shared state across channels would collapse the
    // stereo image entirely (every channel would ring in lockstep).
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
