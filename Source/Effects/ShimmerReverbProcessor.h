#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A comb/allpass reverb tank (same family as HallReverbProcessor, just
    4 combs + 2 allpass instead of 8+4 -- a smaller tank, since the
    interesting part here isn't the space size) whose feedback loop also
    gets a fraction of an octave-up-shifted copy of itself injected back
    in every cycle. That's what makes a shimmer reverb: each pass through
    the tank comes back both as itself and pitched up an octave, building
    an ascending, bell-like halo on top of an ordinary tail -- Shimmer
    controls how much of that shifted copy gets fed back in (0 = an
    ordinary small reverb).

    The pitch shifter is a classic two-grain time-domain octave doubler:
    two triangular-windowed read pointers into a short circular buffer,
    each sweeping backward through it at 2x the write rate (so what comes
    back out is being "replayed" twice as fast = an octave up), 180
    degrees out of phase so one grain's window fade-out is masked by the
    other's fade-in.
*/
class ShimmerReverbProcessor : public EffectProcessor
{
public:
    ShimmerReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Shimmer"; }
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

        /**
            `shiftedSample` adds INTO the loop on top of the normal
            feedback, scaled by its own small, independently-capped
            `shimmerGain` -- not blended in place of it. A blend (replacing
            a fraction of the normal feedback with the shifted copy) is
            provably safe but actually makes the tail decay FASTER as
            shimmer increases, since it steals gain from the comb's own
            coherent same-pitch reinforcement without replacing that
            reinforcement in kind -- confirmed empirically, and the
            opposite of what a shimmer control should do. Keeping
            `feedback`'s own ceiling (~0.85, see prepare()) and
            `shimmerGain`'s ceiling (~0.1) each small enough that their sum
            still stays safely under 1 gets both real stability and a
            tail that actually gets longer as Shimmer increases.
        */
        float process (float input, float shiftedSample, float shimmerGain) noexcept
        {
            const float output = buffer[(size_t) pos];
            filterStore = output * damp2 + filterStore * damp1;
            buffer[(size_t) pos] = input + filterStore * feedback + shiftedSample * shimmerGain;
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

    // Two-grain octave-up doubler -- see the class doc comment above.
    struct OctaveUpShifter
    {
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float grainSamples = 1.0f;
        float delayA = 0.0f;
        float delayB = 0.0f;

        void prepare (double sampleRate)
        {
            grainSamples = (float) (0.04 * sampleRate); // 40ms grain
            bufferSize = (int) grainSamples * 2 + 8;
            buffer.assign ((size_t) bufferSize, 0.0f);
            clear();
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            delayA = grainSamples;
            delayB = grainSamples * 0.5f;
        }

        static float windowGain (float delay, float grain) noexcept
        {
            const float t = delay / grain;
            return 1.0f - std::abs (2.0f * t - 1.0f);
        }

        float readAt (float delaySamples) const noexcept
        {
            float readPos = (float) writePos - delaySamples;
            while (readPos < 0.0f)
                readPos += (float) bufferSize;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1 >= bufferSize) ? 0 : i0 + 1;
            const float frac = readPos - (float) i0;
            return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
        }

        float process (float input) noexcept
        {
            buffer[(size_t) writePos] = input;

            const float outA = readAt (delayA) * windowGain (delayA, grainSamples);
            const float outB = readAt (delayB) * windowGain (delayB, grainSamples);

            delayA -= 1.0f;
            if (delayA <= 0.0f)
                delayA += grainSamples;
            delayB -= 1.0f;
            if (delayB <= 0.0f)
                delayB += grainSamples;

            if (++writePos >= bufferSize)
                writePos = 0;
            return outA + outB;
        }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* shimmerAmount = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    static constexpr int numCombs = 4;
    static constexpr int numAllpassStages = 2;
    std::array<std::array<CombFilter, numCombs>, 2> combs;
    std::array<std::array<AllpassFilter, numAllpassStages>, 2> allpass;
    std::array<OctaveUpShifter, 2> shifters;
    std::array<float, 2> lastCombSum {};

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
