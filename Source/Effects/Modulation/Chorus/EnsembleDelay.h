#pragma once

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/**
    A delay line read at any fractional delay with 4-point Hermite interpolation -- the algorithmic choruses' voice. One
    write per sample, any number of reads (the three taps of an ensemble share one line). Sized in prepare().
*/
class EnsembleDelay
{
public:
    void prepare (double sampleRate, double maxSeconds)
    {
        size_t n = 4;
        while (n < (size_t) (maxSeconds * sampleRate) + 8)
            n <<= 1;
        buffer.assign (n, 0.0f);
        mask = n - 1;
        write = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[write] = x;
        write = (write + 1) & mask;
    }

    /** The sample `delaySamples` behind the most recent push (>= 1). */
    float read (double delaySamples) const noexcept
    {
        const double pos = (double) write - 1.0 - delaySamples;
        const double fl = std::floor (pos);
        const float t = (float) (pos - fl);
        const size_t i = (size_t) (long long) fl;
        const float y0 = buffer[(i - 1) & mask], y1 = buffer[i & mask], y2 = buffer[(i + 1) & mask], y3 = buffer[(i + 2) & mask];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

private:
    std::vector<float> buffer;
    size_t mask = 0, write = 0;
};

/** RBJ 2-pole low-pass: the band limit a BBD's reconstruction filter puts on an analog ensemble's wet signal. */
class WetLowpass
{
public:
    void prepare (double sampleRate, double hz, double q = 0.7071)
    {
        const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate, cw = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - cw) * 0.5 / a0;
        b1 = (1.0 - cw) / a0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - al) / a0;
        reset();
    }
    void reset() noexcept { z1 = z2 = 0.0; }
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b0 * x - a2 * y;
        return y;
    }

private:
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

} // namespace openguitarmultifx
