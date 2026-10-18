#pragma once

#include "../MultiType/MultiTypeCommon.h"

#include <array>
#include <cmath>

namespace openguitarmultifx::gs
{
using namespace mt;

/** Peak envelope follower with separate attack and release (ms). */
struct EnvelopeFollower
{
    double a = 0.0, r = 0.0, y = 0.0;
    void set (double attackMs, double releaseMs, double fs)
    {
        a = std::exp (-1.0 / (juce::jmax (0.05, attackMs) * 1e-3 * fs));
        r = std::exp (-1.0 / (juce::jmax (0.05, releaseMs) * 1e-3 * fs));
    }
    void reset() { y = 0.0; }
    double process (double x)
    {
        const double v = std::abs (x);
        const double c = v > y ? a : r;
        y = v + c * (y - v);
        return y;
    }
};

/** Time-domain monophonic pitch tracker for synth/shifter control: low-pass (follows the tracked pitch) -> hysteresis
    zero-crossing with sub-sample interpolation -> two consistent periods. One estimate per period, so the latency is
    about one to two periods of the note. Fixed cost per sample, no allocation. */
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        minPeriod = fs / 1500.0;
        maxPeriod = fs / 38.0;
        peakDecay = std::exp (-1.0 / (0.06 * fs));
        reset();
    }

    void reset()
    {
        setCutoff (900.0);
        lp[0].reset(); lp[1].reset();
        dcx = dcy = 0.0; peak = 0.0; prev = 0.0;
        armed = false; sample = 0.0; lastCross = -1.0; sinceCross = 0.0;
        count = 0; hz = 0.0; voiced = false;
        history.fill (0.0);
    }

    /** Feeds one sample. Returns true when a new period estimate was accepted on this sample. */
    bool push (float x)
    {
        dcy = (double) x - dcx + 0.995 * dcy;
        dcx = (double) x;
        const double y = lp[1].process (lp[0].process (dcy));
        const double a = std::abs (y);
        peak = a > peak ? a : peak * peakDecay;
        sample += 1.0;
        sinceCross += 1.0;

        bool accepted = false;
        const double h = 0.12 * peak;
        if (y < -h) armed = true;
        if (armed && prev < 0.0 && y >= 0.0 && peak > 4e-4)
        {
            const double t = sample - 1.0 + prev / (prev - y);
            if (lastCross >= 0.0)
            {
                const double p = t - lastCross;
                if (p >= minPeriod && p <= maxPeriod)
                {
                    history[2] = history[1]; history[1] = history[0]; history[0] = p;
                    // two consecutive periods agreeing within 6 % -> the note is periodic and tracked
                    if (history[1] > 0.0 && std::abs (history[0] - history[1]) < 0.06 * history[1])
                    {
                        hz = fs / (0.5 * (history[0] + history[1]));
                        voiced = true;
                        accepted = true;
                        const double fc = juce::jlimit (180.0, 3500.0, 2.5 * hz);
                        if (std::abs (fc - cutoff) > 0.12 * cutoff) setCutoff (fc);
                    }
                }
                else
                    history.fill (0.0);
            }
            lastCross = t;
            sinceCross = 0.0;
            armed = false;
        }
        prev = y;

        const double limit = hz > 0.0 ? 2.6 * fs / hz : maxPeriod;
        if (sinceCross > juce::jmin (limit, 1.6 * maxPeriod) || peak < 2e-4) voiced = false;
        return accepted;
    }

    double hz = 0.0;
    bool voiced = false;

private:
    static double median3 (double a, double b, double c) { return juce::jmax (juce::jmin (a, b), juce::jmin (juce::jmax (a, b), c)); }
    void setCutoff (double f)
    {
        cutoff = f;
        lp[0].lowpass (f, fs ? fs : 48000.0, 0.54);
        lp[1].lowpass (f, fs ? fs : 48000.0, 1.31); // 4th-order Butterworth as two sections
    }

    double fs = 48000.0, minPeriod = 32.0, maxPeriod = 1263.0, peakDecay = 0.99, cutoff = 900.0;
    mt::Biquad lp[2];
    double dcx = 0, dcy = 0, peak = 0, prev = 0, sample = 0, lastCross = -1, sinceCross = 0;
    bool armed = false;
    int count = 0;
    std::array<double, 3> history {};
};

/** Zero-delay-feedback state-variable low-pass (stable under per-block cutoff changes). */
struct Svf
{
    double s1 = 0, s2 = 0, a1 = 1, a2 = 0, a3 = 0;
    void reset() { s1 = s2 = 0; }
    void set (double fc, double resonance, double fs)
    {
        const double g = std::tan (kPi * juce::jlimit (20.0, 0.45 * fs, fc) / fs);
        const double k = 2.0 - 1.95 * juce::jlimit (0.0, 1.0, resonance);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    double lp (double x)
    {
        const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1;
        s2 = 2 * v2 - s2;
        return v2;
    }
};

enum class Wave { saw, square, triangle, sine };

/** Band-limited (PolyBLEP) oscillator; phase in cycles. */
struct Osc
{
    double phase = 0.0;
    static double blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    double next (Wave w, double inc, double pulseWidth = 0.5)
    {
        inc = juce::jlimit (1e-6, 0.45, inc);
        double v = 0.0;
        switch (w)
        {
            case Wave::saw: v = 2.0 * phase - 1.0 - blep (phase, inc); break;
            case Wave::square:
            {
                v = phase < pulseWidth ? 1.0 : -1.0;
                v += blep (phase, inc);
                double t = phase - pulseWidth;
                t -= std::floor (t);
                v -= blep (t, inc);
                break;
            }
            case Wave::triangle: v = 4.0 * std::abs (phase - 0.5) - 1.0; break;
            case Wave::sine: v = std::sin (2.0 * kPi * phase); break;
        }
        phase += inc;
        phase -= std::floor (phase);
        return v;
    }
};

/** Soft limiter shape: clamp + odd cubic (no exp/tanh on the audio path). */
inline double softClip (double x)
{
    const double c = juce::jlimit (-1.5, 1.5, x);
    return c - (4.0 / 27.0) * c * c * c; // slope 1 at 0, flat at +-1.5
}

} // namespace openguitarmultifx::gs
