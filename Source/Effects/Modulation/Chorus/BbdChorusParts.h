#pragma once

#include "Effects/AnalogFilterDesign.h"
#include "BucketBrigadeDelay.h"

#include <algorithm>
#include <cmath>

namespace openguitarmultifx::bbdchorus
{

/** analog::ladderTransfer() result in the form BucketBrigadeDelay takes. */
inline BucketBrigadeDelay::Filter bbdFilter (const std::vector<analog::LadderNode>& ladder)
{
    const auto pz = analog::poleZero (analog::ladderTransfer (ladder));
    BucketBrigadeDelay::Filter f;
    f.poles = pz.poles;
    f.zeros = pz.zeros;
    f.dcGain = pz.dcGain;
    return f;
}

/**
    The triangle LFO every one of these pedals builds from two op-amps: a Schmitt trigger whose square output drives an
    integrator, whose ramp switches the Schmitt at +-threshold. Values are volts relative to the circuit's own reference.
    The ramp speed while the square is high / low can differ (a single-supply op-amp swings unevenly about its bias), so
    the triangle can lean the way the real one does. Advanced in seconds; audio-thread safe.
*/
class TriangleLfo
{
public:
    void setThresholds (double lowVolts, double highVolts) noexcept
    {
        low = lowVolts;
        high = highVolts;
        v = std::clamp (v, low, high);
    }
    /** Integrator slopes in V/s: rising (square low, output ramps up) and falling. */
    void setSlopes (double risingVoltsPerSecond, double fallingVoltsPerSecond) noexcept
    {
        rise = risingVoltsPerSecond;
        fall = fallingVoltsPerSecond;
    }
    void reset (double startVolts = 0.0, bool startRising = true) noexcept
    {
        v = startVolts;
        rising = startRising;
    }

    double advance (double dt) noexcept
    {
        if (rising)
        {
            v += rise * dt;
            if (v >= high)
            {
                v = high - (v - high) * fall / std::max (rise, 1.0e-12);
                rising = false;
            }
        }
        else
        {
            v -= fall * dt;
            if (v <= low)
            {
                v = low + (low - v) * rise / std::max (fall, 1.0e-12);
                rising = true;
            }
        }
        return v;
    }

    double value() const noexcept { return v; }
    bool isRising() const noexcept { return rising; }
    double frequencyHz() const noexcept { return 1.0 / ((high - low) / std::max (rise, 1.0e-12) + (high - low) / std::max (fall, 1.0e-12)); }

private:
    double low = -1.0, high = 1.0, rise = 1.0, fall = 1.0, v = 0.0;
    bool rising = true;
};

/** One-pole RC low-pass (an LFO smoothing network: series R, shunt C). */
class RcLowpass
{
public:
    void prepare (double sampleRate, double seconds) noexcept { a = 1.0 - std::exp (-1.0 / (std::max (1.0e-9, seconds) * sampleRate)); }
    void setTimeConstant (double sampleRate, double seconds) noexcept { prepare (sampleRate, seconds); }
    void reset (double value) noexcept { y = value; }
    double process (double x) noexcept { return y += a * (x - y); }
    double value() const noexcept { return y; }

private:
    double a = 1.0, y = 0.0;
};

/**
    The relaxation oscillator that clocks a BBD driver (MN3101/MN3102 with an external discharge transistor, or a
    CD4047's RC pin): a timing capacitor charges through R toward `towardVolts` from `startVolts` until it reaches the
    chip's switching threshold, then is reset quickly (`resetSeconds`). The LFO sets one of those voltages, which is
    how these clocks are voltage-controlled. Returns the oscillation period; the BBD clock is that divided by the
    driver's flip-flop (2 for the MN310x and CD4047).
*/
inline double rcChargePeriod (double r, double c, double startVolts, double towardVolts, double thresholdVolts, double resetSeconds)
{
    const double span = towardVolts - startVolts;
    const double remaining = towardVolts - thresholdVolts;
    const double ratio = remaining / (std::abs (span) > 1.0e-12 ? span : 1.0e-12);
    if (ratio <= 0.0) // the threshold lies beyond where the capacitor is heading: it never switches -- a stalled clock
        return 1.0e-3;
    if (ratio >= 1.0) // already past the threshold when the charge starts
        return resetSeconds;
    return r * c * std::log (1.0 / ratio) + resetSeconds;
}

/** An op-amp output that cannot pass its rails: linear inside, a short smooth knee into the clamp (a real output
    stage rounds off over a few hundred mV before it saturates). */
inline double railClip (double v, double lowRail, double highRail, double knee = 0.3) noexcept
{
    const double hi = highRail - knee, lo = lowRail + knee;
    if (v > hi)
    {
        const double e = (v - hi) / knee;
        return hi + knee * e / (1.0 + e);
    }
    if (v < lo)
    {
        const double e = (lo - v) / knee;
        return lo - knee * e / (1.0 + e);
    }
    return v;
}

/** A linear potentiometer used as a divider between two voltages: what the wiper delivers, and its source resistance. */
struct Divider
{
    double fraction = 0.5, sourceOhms = 0.0;
    static Divider of (double knob, double trackOhms, double topExtraOhms = 0.0, double bottomExtraOhms = 0.0) noexcept
    {
        // wiper toward "top" as the knob rises; extra fixed resistors at either end of the track
        const double k = std::clamp (knob, 0.0, 1.0);
        const double lower = bottomExtraOhms + k * trackOhms, upper = topExtraOhms + (1.0 - k) * trackOhms;
        Divider d;
        d.fraction = lower / std::max (1.0e-9, lower + upper);
        d.sourceOhms = lower * upper / std::max (1.0e-9, lower + upper);
        return d;
    }
};


/**
    One channel of an NE570/NE571 compander as used around a BBD (Boss DC-2, many delays): a full-wave rectifier
    averaging into C_RECT through the chip's internal 10K, and the delta-G cell either in the op-amp's feedback
    (compressor: the gain falls as the OUTPUT's average rises, so output level ~ sqrt(input level), 2:1) or in its input
    (expander: gain rises with the INPUT's average, level^2 -- the compressor's exact inverse at steady state). Unity gain
    sits at `unityVolts` of rectified average. What survives the round trip is the compander's breathing on transients,
    which is part of these pedals' sound.
*/
class Ne570
{
public:
    void prepare (double sampleRate, double rectifierCapFarads, double unityRectifiedVolts) noexcept
    {
        a = 1.0 - std::exp (-1.0 / (10.0e3 * rectifierCapFarads * sampleRate));
        unity = unityRectifiedVolts;
        floor = 0.01 * unityRectifiedVolts; // ~40 dB of range below unity before the gain stops rising
        reset();
    }
    void reset() noexcept { env = unity; }

    double compress (double x) noexcept
    {
        const double y = x * unity / std::max (env, floor);
        env += a * (std::abs (y) - env);
        return y;
    }

    /** A plain downward compressor on the same rectifier: unity below `unityVolts`, 2:1 above it (no upward gain). */
    double compressDown (double x) noexcept
    {
        const double y = x * std::min (1.0, std::sqrt (unity / std::max (env, floor)));
        env += a * (std::abs (x) - env);
        return y;
    }

    double expand (double x) noexcept
    {
        env += a * (std::abs (x) - env);
        return x * std::max (env, floor) / unity;
    }

private:
    double a = 1.0, unity = 0.3, floor = 0.003, env = 0.3;
};

} // namespace openguitarmultifx::bbdchorus
