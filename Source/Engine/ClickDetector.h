#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx
{

/** Counts audible clicks in a signal: a sample where the second difference is far above what the surrounding signal
    normally produces (a smooth waveform, however bright, has a small second difference; a step, a dropped or repeated
    block, or a solver blow-up has a huge one). Audio-thread safe -- no allocation, no locks. Used on the device input
    and the device output so a pop can be placed: in the source, or created by the processing. */
class ClickDetector
{
public:
    struct Result
    {
        int clicks = 0;
        float biggest = 0.0f; // largest second difference among the clicks
    };

    Result process (const float* data, int numSamples) noexcept
    {
        Result r;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = data[i];
            const float d2 = std::abs (x - 2.0f * p1 + p2);
            p2 = p1;
            p1 = x;
            if (d2 > std::max (0.2f, 10.0f * typical))
            {
                ++r.clicks;
                r.biggest = std::max (r.biggest, d2);
                holdOff = 48; // one event per click, not one per sample of its tail
            }
            else if (holdOff > 0)
                --holdOff;
            if (holdOff == 0 || d2 <= std::max (0.2f, 10.0f * typical))
                typical += 0.002f * (d2 - typical); // follows the signal's own roughness, not the click
        }
        return r;
    }

    void reset() noexcept { p1 = p2 = typical = 0.0f; holdOff = 0; }

private:
    float p1 = 0.0f, p2 = 0.0f, typical = 0.0f;
    int holdOff = 0;
};

} // namespace openguitarmultifx
