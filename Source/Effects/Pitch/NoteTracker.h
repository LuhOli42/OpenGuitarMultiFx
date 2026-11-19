#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::pitchparts
{

/**
    Monophonic note tracker for the Mono Synth: keeps the played note through the whole decay instead of chasing
    every period the comparator measures.

    Front end: DC block, then a 4-pole low-pass whose cutoff follows the locked note (about 1.4x its fundamental), so
    the upper harmonics a decaying string is left with cannot make extra crossings. A hysteresis comparator with
    thresholds scaled to the note's peaks gives rising zero crossings, timed to the sub-sample.

    The period is the smallest number of crossings whose span repeats (within 3 %), so a strong 2nd harmonic that adds
    a crossing inside each period still reads as the fundamental. The note is then locked:
    - an estimate near the locked pitch (within ~1 semitone) refines it, so vibrato and bends still follow;
    - a jump away from it must repeat for several consecutive estimates before it is taken (a legato note change),
      and while the note is decaying an octave/fifth-ratio jump (the fundamental beating away, a harmonic taking
      over) is not taken at all;
    - a new attack (fast level rising well above the slow level) unlocks, so the next note is taken at once;
    - below ~-48 dB under the note's peak, or under the noise floor, the pitch is held, not re-measured.
    Fixed cost per sample, no allocation.
*/
class NoteTracker
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        minPeriod = sr / 1600.0;
        maxPeriod = sr / 38.0;
        peakDecay = std::exp (-1.0 / (0.05 * sr));
        fastA = 1.0 - std::exp (-1.0 / (0.002 * sr));
        fastR = 1.0 - std::exp (-1.0 / (0.03 * sr));
        slowA = 1.0 - std::exp (-1.0 / (0.06 * sr));
        noteDecay = std::exp (-1.0 / (10.0 * sr));
        reset();
    }

    void reset() noexcept
    {
        dcx = dcy = 0.0;
        lp[0] = lp[1] = {};
        setCutoff (acquireHz);
        peakP = peakN = prev = 0.0;
        armed = false;
        t = 0.0; lastCross = -1.0;
        numCross = 0;
        fast = slow = notePeak = 0.0;
        locked = anchor = 0.0;
        pending = 0.0; pendingCount = 0;
        onset = false;
    }

    /** Feeds one sample; true when the locked pitch changed on it. */
    bool process (double x) noexcept
    {
        // level followers on the raw input: attack detection and the note's own reference level
        const double ax = std::abs (x);
        fast += (ax > fast ? fastA : fastR) * (ax - fast);
        slow += slowA * (fast - slow);
        notePeak = std::max (fast, notePeak * noteDecay);
        if (fast > 0.004 && fast > 2.2 * slow + 0.002)
        {
            if (! onset) { notePeak = fast; pendingCount = 0; setCutoff (acquireHz); }
            onset = true;
        }

        dcy = x - dcx + 0.995 * dcy;
        dcx = x;
        const double y = lp[1].process (lp[0].process (dcy, ca), cb); // 4-pole Butterworth low-pass

        peakP = std::max (y, peakP * peakDecay);
        peakN = std::min (y, peakN * peakDecay);
        t += 1.0;
        bool changed = false;
        if (y < 0.25 * peakN) armed = true;
        if (armed && prev < 0.0 && y >= 0.0)
        {
            armed = false;
            const double tc = t - 1.0 + prev / (prev - y);
            if (lastCross >= 0.0)
            {
                if (tc - lastCross > maxPeriod)
                    numCross = 0; // a gap: the old crossings say nothing about this note
            }
            lastCross = tc;
            for (int k = kMaxCross - 1; k > 0; --k)
                cross[k] = cross[k - 1];
            cross[0] = tc;
            numCross = std::min (numCross + 1, kMaxCross);
            changed = measure();
        }
        prev = y;
        return changed;
    }

    /** The period is the smallest crossing count k whose span repeats: k = 1 for a clean fundamental, k = 2..4 when
        strong harmonics add crossings inside each period (a pattern that repeats every k crossings). */
    bool measure() noexcept
    {
        for (int k = 1; 2 * k < numCross; ++k)
        {
            const double a = cross[0] - cross[k], b = cross[k] - cross[2 * k];
            if (a > maxPeriod)
                break;
            if (a >= minPeriod && std::abs (a - b) < 0.03 * a)
                return consider (0.5 * (a + b));
        }
        return false;
    }

    /** The locked note, Hz (0 = none yet). */
    double hz() const noexcept { return locked > 0.0 ? sr / locked : 0.0; }
    double level() const noexcept { return fast; }

private:
    bool consider (double period) noexcept
    {
        const bool audible = fast > 1.0e-3 && fast > 0.016 * notePeak; // ~-36 dB under the note's peak: below it, hold
        if (! audible)
            return false;
        if (locked <= 0.0 || onset)
        {
            onset = false;
            return lock (period);
        }
        const double ratio = locked / period; // > 1: estimate is higher
        const double cents = 1200.0 * std::log2 (ratio);
        const bool decaying = fast < 0.35 * notePeak;
        if (std::abs (cents) < 110.0)
        {
            pendingCount = 0;
            if (! decaying)
                anchor = period;
            // while the note rings out, follow vibrato only within +-40 cents of the pitch it had at full level, so
            // hum and noise taking over the tail cannot walk it away
            if (decaying && anchor > 0.0 && std::abs (1200.0 * std::log2 (anchor / period)) > 40.0)
                return false;
            locked += 0.35 * (period - locked); // follow vibrato / bends, smooth jitter
            setCutoffFor (locked);
            return true;
        }
        // a jump: take it only when it repeats, and never a harmonic-ratio jump while the note decays
        const double r = ratio >= 1.0 ? ratio : 1.0 / ratio;
        const bool harmonicRatio = std::abs (r - 2.0) < 0.06 || std::abs (r - 3.0) < 0.09 || std::abs (r - 1.5) < 0.04 || std::abs (r - 4.0) < 0.12;
        if (decaying && harmonicRatio)
        {
            pendingCount = 0;
            return false;
        }
        if (pendingCount > 0 && std::abs (period - pending) < 0.04 * pending)
            ++pendingCount;
        else
            pendingCount = 1;
        pending = period;
        if (pendingCount >= (decaying ? 6 : 3))
        {
            pendingCount = 0;
            return lock (period);
        }
        return false;
    }

    bool lock (double period) noexcept
    {
        locked = anchor = period;
        pendingCount = 0;
        setCutoffFor (period);
        return true;
    }

    void setCutoffFor (double period) noexcept
    {
        const double fc = std::clamp (1.4 * sr / period, 70.0, 2500.0);
        if (std::abs (fc - cutoff) > 0.08 * cutoff)
            setCutoff (fc);
    }

    void setCutoff (double f) noexcept
    {
        cutoff = f;
        const double g = std::tan (3.14159265358979323846 * f / sr);
        ca.set (g, 1.0 / 0.5412);
        cb.set (g, 1.0 / 1.3066);
    }

    struct Coeffs
    {
        double c1 = 0.0, c2 = 0.0, c3 = 0.0;
        void set (double g, double k) noexcept { c1 = 1.0 / (1.0 + g * (g + k)); c2 = g * c1; c3 = g * c2; }
    };
    struct Stage // TPT state-variable low-pass, safe while the cutoff moves
    {
        double s1 = 0.0, s2 = 0.0;
        double process (double x, const Coeffs& c) noexcept
        {
            const double v3 = x - s2, v1 = c.c1 * s1 + c.c2 * v3, v2 = s2 + c.c2 * s1 + c.c3 * v3;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            return v2;
        }
    };
    static constexpr int kMaxCross = 9;
    static constexpr double acquireHz = 700.0;

    double sr = 48000.0, minPeriod = 30.0, maxPeriod = 1263.0, peakDecay = 0.999;
    double fastA = 0.0, fastR = 0.0, slowA = 0.0, noteDecay = 0.0;
    double dcx = 0.0, dcy = 0.0, cutoff = acquireHz;
    Stage lp[2];
    Coeffs ca, cb;
    double cross[kMaxCross] {};
    int numCross = 0;
    double peakP = 0.0, peakN = 0.0, prev = 0.0, t = 0.0, lastCross = -1.0;
    double fast = 0.0, slow = 0.0, notePeak = 0.0, locked = 0.0, anchor = 0.0, pending = 0.0;
    int pendingCount = 0;
    bool armed = false, onset = false;
};

} // namespace openguitarmultifx::pitchparts
