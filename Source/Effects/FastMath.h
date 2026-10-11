#pragma once

#include <cmath>

namespace openguitarmultifx
{

/**
    Fast math for the per-sample LFO/modulation paths (CPU-pass findings 6/7,
    see the Effects AGENTS.md decisions table).

    libm's full-precision sin/cos/tan/pow run once per sample in every
    LFO-bearing processor. These replacements are minimax-fitted polynomials
    evaluated in double over each call site's real argument range, so their
    error vs libm is small AND bounded -- the bounds below are what
    Tests/FastMathTests.cpp measures and locks:

      sinCycles / sinRadians / cosRadians  |err| < 5e-8 over the whole period
      tanAllpass    |x| <= 0.25 rad        rel err < 1e-10
      pow2          |x| <= 0.5             rel err < 2e-7

    Why those bounds are inaudible at every call site:
      * gain modulation (Tremolo, Rotary, amp bias tremolos): a 5e-8 LFO
        error is a -146 dB artefact;
      * delay modulation (Vibrato/Chorus/Flanger/TapeDelay wow, depths up to
        8 ms): a 5e-8 LFO error is a ~0.4-nanosecond delay-time error, i.e. a
        pitch wobble error of order 1e-4 cents;
      * allpass break frequency (Phaser/UniVibe, 150-2000 Hz): a 1e-10 rel
        error is under a microhertz;
      * playback ratio (PitchMod, +-3 semitones): a 1e-7 rel error is ~1e-4
        cents.
*/
namespace fast
{
    /** sin(2*pi*u) for any real u (wrapped internally), evaluated as a
        degree-9 odd minimax on the folded quarter-cycle, endpoints pinned.
        Measured vs libm on a dense sweep: max |err| 3.4e-8 (bounded below by
        the float result's own rounding; the double poly itself is 3.8e-9);
        max rel err 8.6e-8 where |sin| > 1e-3. */
    inline float sinCycles (double u) noexcept
    {
        const double t = u - std::floor (u);   // [0, 1)
        const double q = t * 4.0;
        const int quad = (int) q;              // quadrant 0..3
        const double f = q - (double) quad;    // fraction inside the quadrant, [0, 1)
        const double g = (quad & 1) != 0 ? 1.0 - f : f;
        const double g2 = g * g;
        // sin (pi/2 * g) on g in [0, 1]
        const double s = g * (1.5707962861 + g2 * (-0.6459632976 + g2 * (0.0796882213 + g2 * (-0.0046718367 + g2 * 0.0001506269))));
        return (float) (quad >= 2 ? -s : s);
    }

    /** sin(r), any real radians argument. Same bound as sinCycles. */
    inline float sinRadians (double r) noexcept
    {
        return sinCycles (r * (1.0 / 6.283185307179586));
    }

    /** cos(r), any real radians argument. Same bound as sinCycles. */
    inline float cosRadians (double r) noexcept
    {
        return sinCycles (r * (1.0 / 6.283185307179586) + 0.25);
    }

    /** tan(x), degree-9 odd minimax, for |x| <= 0.25 rad -- covers
        pi*breakHz/fs for breakHz <= 2 kHz at any fs >= ~25 kHz (the real
        worst case is Phaser's 2 kHz at 32 kHz = 0.196 rad).
        Measured vs libm: max rel err 6.9e-11 over the domain. */
    inline double tanAllpass (double x) noexcept
    {
        const double x2 = x * x;
        return x + x * x2 * (0.33333333221 + x2 * (0.13333390419 + x2 * (0.053922871077 + x2 * 0.023017779358)));
    }

    /** 2^x, degree-5 minimax, for |x| <= 0.5 -- PitchMod's ratio argument is
        semitones/12 with |semitones| <= 3, so x stays inside +-0.25.
        Measured vs libm: max rel err 1.3e-7 over the domain (float result
        rounding included). */
    inline float pow2 (float x) noexcept
    {
        const double xd = (double) x;
        return (float) (1.0000000716 + xd * (0.69314696714 + xd * (0.24022119849 + xd * (0.055507132324 + xd * (0.0096755346627 + xd * 0.0013276459702)))));
    }
}

} // namespace openguitarmultifx
