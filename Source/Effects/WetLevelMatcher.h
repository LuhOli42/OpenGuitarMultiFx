#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace openguitarmultifx
{

/**
    Keeps a reverb's wet path as loud as its dry input, so that a Mix knob is a real crossfade: 0 = the dry signal, 1 = the wet
    signal at the SAME loudness, 0.5 = half of each. Without this a reverb's wet level is whatever its structure happens to give:
    measured on the guitar-like reference (Tests/MixLawTests.cpp) the Hall was +10 dB, the Plate +15 dB and the Ambient +6 dB
    louder than the dry at Mix = 1, so "noon" was 90%+ reverb; and the level moved by 12 dB across the Decay knob alone.

    How: the wet path's mean square and the input's are averaged over the same ~10 s window (a comb bank rings differently for each note of a phrase: a short window follows the last note, not the reverb), only over blocks where the input is
    above -70 dB (a tail after silence does not drag the gain up), and the wet gain follows sqrt (P_in / P_wet) slowly (2 s),
    starting from a per-effect calibrated `initialGain` and not moving at all until two seconds of playing have been heard. It sits
    AFTER the reverb's loops (only the output is scaled), so it cannot affect stability, and a fixed setting settles to a fixed
    gain: there is no audible pumping. The cost is that turning Decay up no longer makes the reverb louder, which is the point.

    One matcher PER CHANNEL: the reverbs' left and right banks are tuned differently, so a mono guitar came out of them at levels
    up to 7 dB apart; each channel's wet is matched to its own dry.

    Use: per sample `accumulate (dry, rawWet)`, multiply the wet by `gain()`, per block `endBlock (numSamples, 1)`.
*/
class WetLevelMatcher
{
public:
    void prepare (double newSampleRate, float newInitialGain) noexcept
    {
        sampleRate = newSampleRate;
        initialGain = newInitialGain;
        reset();
    }

    void reset() noexcept
    {
        gainNow = initialGain;
        numIn = numWet = weight = 0.0;
        activeSeconds = 0.0;
        sumIn = sumWet = 0.0;
    }

    float gain() const noexcept { return gainNow; }

    void accumulate (float dry, float rawWet) noexcept
    {
        sumIn += (double) dry * dry;
        sumWet += (double) rawWet * rawWet;
    }

    void endBlock (int numSamples, int numChannels) noexcept
    {
        const double count = (double) std::max (1, numSamples * std::max (1, numChannels));
        const double msIn = sumIn / count, msWet = sumWet / count;
        sumIn = sumWet = 0.0;

        if (sampleRate <= 0.0 || msIn < silenceMeanSquare)
            return; // no input: freeze (the averages and the gain), so a tail is not counted against the next phrase

        const double dt = (double) numSamples / sampleRate;
        const double keep = std::exp (-dt / powerSeconds);
        numIn = numIn * keep + msIn * (1.0 - keep);
        numWet = numWet * keep + msWet * (1.0 - keep);
        weight = weight * keep + (1.0 - keep);
        activeSeconds += dt;

        if (activeSeconds < settleSeconds || numWet < 1.0e-14)
            return;

        const double target = std::clamp (std::sqrt ((numIn / weight) / (numWet / weight)), 0.01, 40.0);
        gainNow += (float) ((target - gainNow) * (1.0 - std::exp (-dt / gainSeconds)));
    }

private:
    static constexpr double silenceMeanSquare = 1.0e-7; // -70 dBFS RMS
    static constexpr double powerSeconds = 10.0;
    static constexpr double gainSeconds = 2.0;
    static constexpr double settleSeconds = 2.0;

    double sampleRate = 0.0;
    float initialGain = 1.0f, gainNow = 1.0f;
    double numIn = 0.0, numWet = 0.0, weight = 0.0, activeSeconds = 0.0;
    double sumIn = 0.0, sumWet = 0.0;
};

} // namespace openguitarmultifx
