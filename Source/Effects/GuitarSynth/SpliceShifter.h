#pragma once

#include "SynthParts.h"

#include <array>
#include <cstdint>
#include <vector>

namespace openguitarmultifx::gs
{

/** Fixed-ratio pitch shifter for going DOWN (ratio < 1) that needs no pitch tracker, so chords shift as cleanly as
    single notes and the pitch is exactly the input times the ratio (bends and vibrato included).

    One read tap walks the input history at `ratio` samples per sample, so the delay grows; once it has grown by J the
    tap jumps J samples forward (a short crossfade covers the seam). J is chosen where the input repeats itself: the lag
    with the highest normalised correlation between the audio at the tap and the audio J earlier, searched on a 4x
    decimated copy and refined at full rate. For a single note that is a whole number of periods (an inaudible
    splice); for a chord it is the chord's common period when there is one, the best partial match when there is not.
    The delay never exceeds J plus a few samples, so the latency is J/2 on average (about 5-15 ms).

    Cost per sample: one or two cubic reads plus a few multiplies; one search of ~30k multiply-adds per splice
    (25-50 splices per second). No allocation after prepare(). */
class SpliceShifter
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dec = juce::jmax (1, (int) std::lround (fs / 12000.0));
        int size = 1;
        while (size < (int) (0.35 * fs)) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        int dsize = 1;
        while (dsize < size / dec + 8) dsize <<= 1;
        dbuf.assign ((size_t) dsize, 0.0f);
        dmask = dsize - 1;
        aa[0].lowpass (2800.0, fs, 0.54);
        aa[1].lowpass (2800.0, fs, 1.31);
        jMin = 0.010 * fs;
        jMax = 0.040 * fs;
        corrLen = juce::jmax (16, (int) std::lround (0.008 * fs));
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::fill (dbuf.begin(), dbuf.end(), 0.0f);
        aa[0].reset(); aa[1].reset();
        written = 0;
        tapA = tapB = -kGuard;
        fading = false; fadePos = 0.0; fadeInc = 0.0; powerLaw = 0.0;
        pendingJ = 0.0;
    }

    /** Push one input sample and return one shifted sample. */
    float process (float x, double ratio)
    {
        buf[(size_t) (written & mask)] = x;
        const double y = aa[1].process (aa[0].process (x));
        if (written % dec == 0) dbuf[(size_t) ((written / dec) & dmask)] = (float) y;
        ++written;

        const double now = (double) written - 1.0;
        double out = read (tapA);
        if (fading)
        {
            const double t = fadePos, lin = 0.5 - 0.5 * std::cos (kPi * t);
            const double gB = lin + powerLaw * (std::sqrt (lin) - lin);
            const double gA = (1.0 - lin) + powerLaw * (std::sqrt (1.0 - lin) - (1.0 - lin));
            out = gA * out + gB * read (tapB);
            tapB += ratio;
            fadePos += fadeInc;
            if (fadePos >= 1.0) { fading = false; tapA = tapB; }
        }
        tapA += ratio;
        if (fading) return (float) out;

        const double delay = now - tapA;
        if (pendingJ <= 0.0 && delay >= jMin + kGuard && delay >= corrLen + kGuard)
            pendingJ = chooseJump (now, ratio);
        if (pendingJ > 0.0 && delay >= pendingJ + kGuard)
        {
            tapB = tapA + pendingJ;
            fading = true;
            fadePos = 0.0;
            const double fadeSamples = juce::jmin ((lastScore > 0.85 ? 0.006 : 0.012) * fs, 0.8 * pendingJ / (1.0 - ratio));
            fadeInc = 1.0 / juce::jmax (16.0, fadeSamples);
            powerLaw = juce::jlimit (0.0, 1.0, (0.8 - lastScore) / 0.5);
            pendingJ = 0.0;
        }
        else if (delay > (double) buf.size() - 8.0 * dec - jMax) // cannot happen at ratio <= 1; guards a bad state
            tapA = now - kGuard;
        return (float) out;
    }

    double fs = 48000.0;
    double lastScore = 0.0;

private:
    static constexpr double kGuard = 4.0;

    float read (double pos) const
    {
        const auto i1 = (int64_t) std::floor (pos);
        const double f = pos - (double) i1;
        auto at = [&] (int64_t i) { return i < 0 ? 0.0f : buf[(size_t) (i & mask)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return (float) (((c3 * f + c2) * f + c1) * f + y1);
    }

    float dAt (int64_t i) const { return i < 0 ? 0.0f : dbuf[(size_t) (i & dmask)]; }
    float at (int64_t i) const { return i < 0 ? 0.0f : buf[(size_t) (i & mask)]; }

    /** Normalised correlation between x[b0 .. b0+n) and x[b0-lag .. b0-lag+n) on the given accessor. */
    template <typename Fn>
    static double ncc (Fn&& get, int64_t b0, int64_t lag, int n, int step)
    {
        double ab = 0, aa2 = 0, bb = 0;
        for (int k = 0; k < n; k += step)
        {
            const double b = get (b0 + k), a = get (b0 + k - lag);
            ab += a * b; aa2 += a * a; bb += b * b;
        }
        return ab / std::sqrt (aa2 * bb + 1e-24);
    }

    /** Picks the jump: the shortest lag in [jMin, jMax] whose correlation is within 0.04 of the best one. */
    double chooseJump (double now, double)
    {
        const auto anchor = (int64_t) std::floor (tapA);
        const int64_t dAnchor = anchor / dec;
        const int dn = juce::jmax (4, corrLen / dec);
        const int lo = (int) std::ceil (jMin / dec), hi = (int) std::floor (jMax / dec);
        auto dget = [this] (int64_t i) { return (double) dAt (i); };

        std::array<double, 2048> score {};
        const int count = juce::jmin ((int) score.size(), hi - lo + 1);
        double best = -1.0;
        for (int k = 0; k < count; ++k)
        {
            score[(size_t) k] = ncc (dget, dAnchor, lo + k, dn, 1);
            best = juce::jmax (best, score[(size_t) k]);
        }
        int pick = 0;
        for (int k = 0; k < count; ++k)
        {
            const bool peak = (k == 0 || score[(size_t) k] >= score[(size_t) k - 1]) && (k == count - 1 || score[(size_t) k] >= score[(size_t) k + 1]);
            if (peak && score[(size_t) k] >= best - 0.04) { pick = k; break; }
        }

        // full-rate refinement around the coarse lag, then a parabola through the top three
        auto get = [this] (int64_t i) { return (double) at (i); };
        const int64_t centre = (int64_t) (lo + pick) * dec;
        int64_t bestLag = centre;
        double s[3] {}, top = -2.0;
        for (int64_t lag = juce::jmax<int64_t> ((int64_t) jMin, centre - dec); lag <= juce::jmin<int64_t> ((int64_t) jMax, centre + dec); ++lag)
        {
            const double c = ncc (get, anchor, lag, corrLen, 2);
            if (c > top) { top = c; bestLag = lag; }
        }
        s[0] = ncc (get, anchor, bestLag - 1, corrLen, 2);
        s[1] = top;
        s[2] = ncc (get, anchor, bestLag + 1, corrLen, 2);
        const double den = s[0] - 2.0 * s[1] + s[2];
        const double frac = den < -1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (s[0] - s[2]) / den) : 0.0;
        lastScore = top;
        juce::ignoreUnused (now);
        return (double) bestLag + frac;
    }

    std::vector<float> buf, dbuf;
    int64_t mask = 0, dmask = 0;
    int dec = 4;
    mt::Biquad aa[2];
    int64_t written = 0;
    double tapA = 0.0, tapB = 0.0, fadePos = 0.0, fadeInc = 0.0, powerLaw = 0.0, pendingJ = 0.0;
    bool fading = false;
    double jMin = 480.0, jMax = 1920.0;
    int corrLen = 384;
};

} // namespace openguitarmultifx::gs
