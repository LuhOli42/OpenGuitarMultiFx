#pragma once

#include <cmath>

namespace openguitarmultifx
{

/** One second-order section (Robert Bristow-Johnson's cookbook designs, bilinear transform), direct form II transposed, in double
    precision. Plain data and no allocation: coefficients can be recomputed on the audio thread. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }

    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    /** Magnitude response (linear) at `freq`. */
    double magnitude (double freq, double sampleRate) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        b2 = nb2 / na0;
        a1 = na1 / na0;
        a2 = na2 / na0;
    }

    /** Peaking EQ: `gainDb` at `freq`, bandwidth set by `q`. */
    void makePeak (double sampleRate, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    /** Second-order Butterworth-class low pass / high pass (Q = 0.7071 unless given). */
    void makeLowPass (double sampleRate, double freq, double q = 0.70710678) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void makeHighPass (double sampleRate, double freq, double q = 0.70710678) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    /** True notch (band-reject): `q` sets the width of the rejected band around `freq`. */
    void makeNotch (double sampleRate, double freq, double q) noexcept
    {
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        set (1.0, -2.0 * c, 1.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    /** Low shelf: `gainDb` below `freq` (the midpoint of the transition). `s` is the RBJ
        shelf slope (0.2..2: below 1 the shelf overshoots, above 1 the transition steepens).
        S has a gain-dependent ceiling: the radicand (A + 1/A)(1/S - 1) + 2 must stay positive
        or alpha collapses to 0 and the poles land ON the unit circle (undamped ringing).
        sEff clamps S so the radicand keeps a 0.2 floor -- an extreme S saturates at the
        steepest still-damped transition instead of ringing forever. */
    void makeLowShelf (double sampleRate, double freq, double gainDb, double s = 1.0) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double sum = A + 1.0 / A;
        const double sEff = std::min (s, sum / (sum - 1.8));
        const double c = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (sum * (1.0 / sEff - 1.0) + 2.0), sq = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) - (A - 1.0) * c + sq), 2.0 * A * ((A - 1.0) - (A + 1.0) * c), A * ((A + 1.0) - (A - 1.0) * c - sq),
             (A + 1.0) + (A - 1.0) * c + sq, -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - sq);
    }

    /** High shelf: `gainDb` above `freq`. `s` clamps like `makeLowShelf`. */
    void makeHighShelf (double sampleRate, double freq, double gainDb, double s = 1.0) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const double sum = A + 1.0 / A;
        const double sEff = std::min (s, sum / (sum - 1.8));
        const double c = std::cos (w0), alpha = std::sin (w0) / 2.0 * std::sqrt (sum * (1.0 / sEff - 1.0) + 2.0), sq = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) + (A - 1.0) * c + sq), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sq),
             (A + 1.0) - (A - 1.0) * c + sq, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sq);
    }
};

} // namespace openguitarmultifx
