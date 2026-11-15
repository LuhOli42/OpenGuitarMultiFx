#pragma once

#include "SoftCeiling.h"

#include <algorithm>
#include <cmath>

namespace openguitarmultifx
{

/**
    Zero-latency peak limiter for the final output, so a hot chain (a boosted last-in-chain pedal, summed lanes,
    master gain pushed up) is turned down instead of clipped.

    The gain drops to exactly what keeps the loudest channel at `ceiling` on the SAME sample (no lookahead, so it adds
    no latency), then recovers on a ~120 ms release. The channels are linked -- one gain for all of them -- so the
    stereo image does not shift when one side peaks. The ceiling is the model knee (0.88, about -1.1 dBFS): the device
    soft ceiling after it (SoftCeiling.h) then has nothing left to saturate and stays as the last-resort safety net.

    Audio thread only: no allocation, no locks.
*/
class OutputLimiter
{
public:
    void prepare (double sampleRate) noexcept
    {
        releaseCoeff = std::exp (-1.0f / (releaseSeconds * (float) std::max (8000.0, sampleRate)));
        gain = 1.0f;
    }

    void reset() noexcept { gain = 1.0f; }

    /** Lowest gain applied since the last call (1 = untouched): the "limiting" indicator. */
    float takeMinimumGain() noexcept
    {
        const float g = minGain;
        minGain = 1.0f;
        return g;
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        constexpr float ceiling = (float) deviceKnee;
        float lowest = minGain;

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (channels[ch][i]));

            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            gain = needed < gain ? needed : 1.0f - (1.0f - gain) * releaseCoeff; // instant attack, smooth release
            lowest = std::min (lowest, gain);

            if (gain < 1.0f)
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] *= gain;
        }

        minGain = lowest;
    }

private:
    static constexpr float releaseSeconds = 0.12f;
    float releaseCoeff = 0.9998f;
    float gain = 1.0f;
    float minGain = 1.0f;
};

} // namespace openguitarmultifx
