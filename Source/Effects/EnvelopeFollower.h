#pragma once

#include <cmath>

namespace openguitarmultifx
{

/**
    One-pole peak envelope follower with independent attack/release times.
    Reused for two different jobs across the built-in effects: tracking an
    input's level (GateProcessor's internal detector, CompressorProcessor's
    detector) and smoothing a target gain toward 0/1 (GateProcessor's gate
    transition itself). Same math, different callers.

    Realtime-safe: no allocation, no branching beyond a single comparison,
    safe to call once per sample from process().
*/
class EnvelopeFollower
{
public:
    void prepare (double sampleRateToUse) noexcept
    {
        sampleRate = sampleRateToUse;
        updateCoefficients();
    }

    // Callers (Gate/CompressorProcessor) call these unconditionally every
    // block even when the knob hasn't moved -- skip the std::exp() in
    // calcCoeff() when the time genuinely hasn't changed since last time.
    void setAttackTime (float milliseconds) noexcept
    {
        if (milliseconds == attackMs)
            return;
        attackMs = milliseconds;
        attackCoeff = calcCoeff (attackMs);
    }

    void setReleaseTime (float milliseconds) noexcept
    {
        if (milliseconds == releaseMs)
            return;
        releaseMs = milliseconds;
        releaseCoeff = calcCoeff (releaseMs);
    }

    void reset (float startingValue = 0.0f) noexcept { envelope = startingValue; }

    /** Feed the next target value (an input level, or a target gain); returns the smoothed result. */
    float processSample (float target) noexcept
    {
        const float coeff = (target > envelope) ? attackCoeff : releaseCoeff;
        envelope = coeff * envelope + (1.0f - coeff) * target;
        return envelope;
    }

    float getCurrentValue() const noexcept { return envelope; }

private:
    void updateCoefficients() noexcept
    {
        attackCoeff = calcCoeff (attackMs);
        releaseCoeff = calcCoeff (releaseMs);
    }

    float calcCoeff (float timeMs) const noexcept
    {
        if (sampleRate <= 0.0 || timeMs <= 0.0f)
            return 0.0f;
        return std::exp (-1.0f / (float) (0.001 * timeMs * sampleRate));
    }

    double sampleRate = 0.0;
    float attackMs = 1.0f, releaseMs = 100.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float envelope = 0.0f;
};

} // namespace openguitarmultifx
