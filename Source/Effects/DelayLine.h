#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/**
    Shared delay-line primitives -- extracted from the per-processor copies
    that grew up independently across the Delay/Reverb/Pitch effects (see
    the CPU-pass note in this directory's AGENTS.md). Each processor's
    numeric behaviour is unchanged: same linear interpolation, same
    `if (++x >= len) x = 0` wraparound, same state layout.
*/

/**
    A multi-channel circular buffer with a linearly-interpolated
    fractional read position. All channels share one write position --
    every delay/modulation processor in the codebase already kept exactly
    this (buffer, writePos) pair inline as members.
*/
struct FractionalDelayLine
{
    juce::AudioBuffer<float> buffer;
    int writePos = 0;

    void prepare (int numChannels, int bufferLength)
    {
        buffer.setSize (juce::jmax (1, numChannels), bufferLength, false, true, true);
        writePos = 0;
    }

    void clear()
    {
        buffer.clear();
        writePos = 0;
    }

    int getLength() const noexcept { return buffer.getNumSamples(); }

    float* getChannelData (int ch) noexcept { return buffer.getWritePointer (ch); }
    const float* getChannelData (int ch) const noexcept { return buffer.getReadPointer (ch); }

    // Channel-independent half of the fractional read: the wrapped read
    // position only depends on writePos / delaySamples / length, so the
    // index pair + fraction are computed once per sample and shared
    // across channels (the optimisation MultiTap/DualDelay already did
    // per-tap by hand).
    void getReadIndices (float delaySamples, int& i0, int& i1, float& frac) const noexcept
    {
        const int len = buffer.getNumSamples();
        float readPos = (float) writePos - delaySamples;
        while (readPos < 0.0f)
            readPos += (float) len;
        i0 = (int) readPos;
        i1 = (i0 + 1 >= len) ? 0 : i0 + 1;
        frac = readPos - (float) i0;
    }

    static float interpolate (const float* data, int i0, int i1, float frac) noexcept
    {
        return data[i0] + frac * (data[i1] - data[i0]);
    }

    float readInterpolated (int ch, int i0, int i1, float frac) const noexcept
    {
        return interpolate (buffer.getReadPointer (ch), i0, i1, frac);
    }

    void advance() noexcept
    {
        if (++writePos >= buffer.getNumSamples())
            writePos = 0;
    }
};

/**
    Damped feedback comb, the Freeverb-style primitive behind the
    Hall/Room/Gated/Shimmer reverb tanks. The three-argument overload is
    ShimmerReverbProcessor's variant: `shiftedSample` adds INTO the loop
    on top of the normal feedback, scaled by `shimmerGain` (see that
    processor's doc comment for why it is additive rather than blended).
*/
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

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
        filterStore = 0.0f;
    }

    float process (float input) noexcept
    {
        const float output = buffer[(size_t) pos];
        filterStore = output * damp2 + filterStore * damp1;
        buffer[(size_t) pos] = input + filterStore * feedback;
        if (++pos >= (int) buffer.size())
            pos = 0;
        return output;
    }

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
        `feedback`'s own ceiling (~0.85) and `shimmerGain`'s ceiling
        (~0.1) each small enough that their sum still stays safely
        under 1 gets both real stability and a tail that actually
        gets longer as Shimmer increases.
        (ShimmerReverbProcessor is the only caller.)
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

/**
    The `output = -input + delayed; buffer = input + delayed * feedback`
    allpass used as a diffusion stage in the comb tanks and PlateReverb's
    input diffuser. SpringReverbProcessor::AllpassStage is a DIFFERENT
    allpass form (feedback applied to the output too) and intentionally
    stays local to that processor.
*/
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

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        pos = 0;
    }

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

/**
    The classic two-grain time-domain pitch shifter shared by
    PitchShiftProcessor (GrainShifter), OctaverProcessor, HarmonizerProcessor,
    PitchModProcessor (VariableRateShifter) and ShimmerReverbProcessor
    (OctaveUpShifter): two triangular-windowed read pointers into a short
    circular buffer, half a grain apart, each sweeping through the buffer
    at `ratio - 1` samples per input sample.

    `grainSeconds` / `capacitySeconds` let a caller size the grain and the
    backing buffer independently -- PitchShiftProcessor needs a buffer
    larger than its default grain (its adaptive grain can grow to two
    20 Hz periods); everyone else passes just the grain, which also sizes
    the buffer at 2x + 8.

    `delayAPhase`/`delayBPhase` are the grains' initial read offsets as
    fractions of the grain -- 0.5/0.0 for the standard 180-degree split,
    1.0/0.5 for ShimmerReverbProcessor's shifter, which starts a full
    grain behind.
*/
struct TwoGrainShifter
{
    std::vector<float> buffer;
    int bufferSize = 0;
    int writePos = 0;
    float grainSamples = 1.0f;
    float delayA = 0.0f;
    float delayB = 0.0f;
    float delayAPhase = 0.5f;
    float delayBPhase = 0.0f;

    void prepare (double sampleRate, double grainSeconds = 0.05, double capacitySeconds = 0.0,
                  float initialDelayAPhase = 0.5f, float initialDelayBPhase = 0.0f)
    {
        grainSamples = (float) (grainSeconds * sampleRate);
        bufferSize = capacitySeconds > 0.0
            ? (int) (capacitySeconds * sampleRate) * 2 + 8
            : (int) grainSamples * 2 + 8;
        buffer.assign ((size_t) bufferSize, 0.0f);
        delayAPhase = initialDelayAPhase;
        delayBPhase = initialDelayBPhase;
        clear();
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        delayA = grainSamples * delayAPhase;
        delayB = grainSamples * delayBPhase;
    }

    static float windowGain (float delay, float grain) noexcept
    {
        const float t = delay / grain;
        return 1.0f - std::abs (2.0f * t - 1.0f);
    }

    // Moves the grain length toward target while preserving both
    // windows' phases: scaling the read delays by the same factor
    // keeps the half-grain offset, so the only artefact is a slow
    // glide toward the new window length rather than a splice.
    // (PitchShiftProcessor's adaptive grain is the only caller.)
    void slewGrain (float target) noexcept
    {
        const float diff = target - grainSamples;
        if (diff > 0.01f || diff < -0.01f)
        {
            const float next = grainSamples + diff * 0.0007f;
            const float scale = next / grainSamples;
            grainSamples = next;
            delayA *= scale;
            delayB *= scale;
            while (delayA >= grainSamples) delayA -= grainSamples;
            while (delayA < 0.0f) delayA += grainSamples;
            while (delayB >= grainSamples) delayB -= grainSamples;
            while (delayB < 0.0f) delayB += grainSamples;
        }
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

    float process (float input, float ratio) noexcept
    {
        buffer[(size_t) writePos] = input;

        const float outA = readAt (delayA) * windowGain (delayA, grainSamples);
        const float outB = readAt (delayB) * windowGain (delayB, grainSamples);

        const float step = ratio - 1.0f;
        delayA -= step;
        if (delayA <= 0.0f)
            delayA += grainSamples;
        else if (delayA >= grainSamples)
            delayA -= grainSamples;

        delayB -= step;
        if (delayB <= 0.0f)
            delayB += grainSamples;
        else if (delayB >= grainSamples)
            delayB -= grainSamples;

        if (++writePos >= bufferSize)
            writePos = 0;
        return outA + outB;
    }
};

} // namespace openguitarmultifx
