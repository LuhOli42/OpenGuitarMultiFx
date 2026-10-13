#pragma once

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::pitchparts
{

/**
    The front end every analog octave divider and guitar synth shares: a low-pass that leaves (mostly) the
    fundamental, positive and negative peak detectors that follow the note's level, and a comparator whose thresholds
    are a fraction of those peaks (hysteresis that scales with the note, so a decaying string keeps tracking). Its
    output is a square at the played fundamental; edges() reports each rising one.
*/
class Analyser
{
public:
    void prepare (double sampleRate, double lowpassHz, int poles, double hysteresis, double peakDecaySeconds) noexcept
    {
        sr = sampleRate;
        a = 1.0 - std::exp (-2.0 * 3.14159265358979323846 * lowpassHz / sampleRate);
        numPoles = std::clamp (poles, 1, 4);
        hyst = hysteresis;
        decay = std::exp (-1.0 / (peakDecaySeconds * sampleRate));
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : lp) s = 0.0;
        peakP = peakN = 0.0;
        high = false;
        samplesSinceEdge = 0.0;
        period = 0.0;
    }

    /** Returns true on a rising edge of the fundamental square. */
    bool process (double x) noexcept
    {
        double y = x;
        for (int k = 0; k < numPoles; ++k)
            y = lp[k] += a * (y - lp[k]);
        filtered = y;
        peakP = std::max (y, peakP * decay);
        peakN = std::min (y, peakN * decay);
        samplesSinceEdge += 1.0;
        bool rose = false;
        if (! high && y > hyst * peakP && peakP > 1.0e-4)
        {
            high = true;
            rose = true;
            if (samplesSinceEdge < sr / 25.0) // below ~25 Hz is not a note
                period = samplesSinceEdge;
            samplesSinceEdge = 0.0;
        }
        else if (high && y < hyst * peakN)
            high = false;
        return rose;
    }

    bool isHigh() const noexcept { return high; }
    double level() const noexcept { return std::max (peakP, -peakN); }
    double lastFiltered() const noexcept { return filtered; }
    /** The last measured period in samples (0 = none yet). */
    double periodSamples() const noexcept { return period; }

private:
    double sr = 48000.0, a = 1.0, hyst = 0.3, decay = 0.999, peakP = 0.0, peakN = 0.0, filtered = 0.0, samplesSinceEdge = 0.0, period = 0.0;
    double lp[4] {};
    int numPoles = 2;
    bool high = false;
};

/** A chain of toggle flip-flops (4013s) clocked by the analyser's edges: q[0] divides by 2, q[1] by 4. */
struct Dividers
{
    bool q[2] { false, false };
    void clock() noexcept
    {
        q[0] = ! q[0];
        if (! q[0])
            q[1] = ! q[1];
    }
    void reset() noexcept { q[0] = q[1] = false; }
};

/**
    A resonant low-pass voltage-controlled filter (the guitar synths' VCF): a 2-pole state-variable filter in TPT
    form, safe while its cutoff moves every sample. resonance 0..1 -> Q 0.5..~12.
*/
class Vcf
{
public:
    void reset() noexcept { ic1 = ic2 = 0.0; }
    double process (double x, double cutoffHz, double resonance, double sampleRate) noexcept
    {
        const double g = std::tan (3.14159265358979323846 * std::clamp (cutoffHz, 20.0, 0.45 * sampleRate) / sampleRate);
        const double k = 2.0 - 1.9 * std::clamp (resonance, 0.0, 1.0); // 1/Q
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v2;
    }

private:
    double ic1 = 0.0, ic2 = 0.0;
};

} // namespace openguitarmultifx::pitchparts
