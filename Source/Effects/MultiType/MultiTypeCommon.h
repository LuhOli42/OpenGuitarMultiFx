#pragma once

#include "../EffectProcessor.h"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

/*  Shared pieces for the "Multi-Type" blocks: one generic engine per category whose "types" are only presets of
    the engine's parameters (docs/MultiTypeBlocks.md). */

namespace openguitarmultifx::mt
{

constexpr double kPi = 3.14159265358979323846;

/** Float parameter with a printable value (units / decimals) -- the panel shows getText(). */
inline std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name, float lo, float hi,
                                                              float def, float skew = 1.0f, const juce::String& unit = {}, int decimals = 1)
{
    juce::NormalisableRange<float> r (lo, hi, 0.0f, skew);
    auto attr = juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
        [unit, decimals] (float v, int) {
            const auto s = decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
            return unit.isEmpty() ? s : s + " " + unit;
        });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, attr);
}

/** A list of named choices stored as an integer-stepped float (the panel turns <= 9 steps into a switch and, in this
    branch, shows the name on a stepped knob above that). */
inline std::unique_ptr<juce::AudioParameterFloat> choiceParam (const juce::String& id, const juce::String& name,
                                                               const juce::StringArray& names, int def)
{
    juce::NormalisableRange<float> r (0.0f, float (juce::jmax (1, names.size() - 1)), 1.0f);
    auto attr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [names] (float v, int) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, float (def), attr);
}

inline int asIndex (const juce::AudioParameterFloat* p) { return juce::roundToInt (p->get()); }

/** Watches a block's Type parameter on the message thread and loads that type's defaults into the other
    parameters when the user changes it -- never on the audio thread, and not when a preset restores the type. */
class TypeWatcher : private juce::Timer
{
public:
    std::function<void (int)> onTypeChanged;

    void attach (juce::AudioParameterFloat* typeParam)
    {
        type = typeParam;
        last = asIndex (type);
        startTimerHz (15);
    }
    void syncWithoutApplying() { if (type != nullptr) last = asIndex (type); }

    /** Same check as the timer, for control-thread entry points (prepare) -- hosts without a message loop. */
    void poll()
    {
        if (type == nullptr) return;
        const int now = asIndex (type);
        if (now == last) return;
        last = now;
        if (onTypeChanged) onTypeChanged (now);
    }

private:
    void timerCallback() override { poll(); }
    juce::AudioParameterFloat* type = nullptr;
    int last = -1;
};

inline void setParam (juce::AudioParameterFloat* p, float v)
{
    if (p != nullptr) p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (p->range.start, p->range.end, v)));
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    static double w (double f, double fs) { return 2.0 * kPi * juce::jlimit (5.0, 0.45 * fs, f) / fs; }
    void lowpass (double f, double fs, double q = 0.7071)
    {
        const double c = std::cos (w (f, fs)), al = std::sin (w (f, fs)) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double f, double fs, double q = 0.7071)
    {
        const double c = std::cos (w (f, fs)), al = std::sin (w (f, fs)) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double f, double fs, double q)
    {
        const double c = std::cos (w (f, fs)), al = std::sin (w (f, fs)) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double f, double fs, double db, double q)
    {
        const double A = std::pow (10.0, db / 40.0), c = std::cos (w (f, fs)), al = std::sin (w (f, fs)) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void shelf (double f, double fs, double db, bool low)
    {
        const double A = std::pow (10.0, db / 40.0), c = std::cos (w (f, fs)), s = std::sin (w (f, fs));
        const double al = s / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        if (low) set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                      (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
        else set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                  (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void allpass (double f, double fs, double q)
    {
        const double c = std::cos (w (f, fs)), al = std::sin (w (f, fs)) / (2 * q);
        set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
};

/** One-pole low/high pass (TPT form, stable under modulation). */
struct OnePole
{
    double g = 0, z = 0;
    void set (double f, double fs) { const double t = std::tan (kPi * juce::jlimit (1.0, 0.45 * fs, f) / fs); g = t / (1 + t); }
    double lp (double x) { const double v = (x - z) * g; const double y = v + z; z = y + v; return y; }
    double hp (double x) { return x - lp (x); }
    void reset() { z = 0; }
};

/** Circular delay line with cubic (Hermite) fractional read. */
struct DelayLine
{
    std::vector<float> buf;
    int pos = 0;
    void prepare (int maxSamples) { buf.assign ((size_t) juce::jmax (8, maxSamples + 4), 0.0f); pos = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    void push (float x) { buf[(size_t) pos] = x; pos = (pos + 1) % (int) buf.size(); }
    float read (double delay) const
    {
        const int n = (int) buf.size();
        delay = juce::jlimit (1.0, (double) n - 3.0, delay);
        const double rp = (double) pos - delay;
        int i1 = (int) std::floor (rp);
        const double f = rp - i1;
        auto at = [&] (int i) { return buf[(size_t) (((i % n) + n) % n)]; };
        const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3, c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return (float) (((c3 * f + c2) * f + c1) * f + c0);
    }
};

/** Schroeder allpass on a plain delay (diffusers). */
struct Diffuser
{
    std::vector<float> buf;
    int len = 1, pos = 0;
    double g = 0.6;
    void prepare (int maxLen) { buf.assign ((size_t) juce::jmax (2, maxLen), 0.0f); pos = 0; }
    void setLength (int l) { len = juce::jlimit (1, (int) buf.size(), l); pos %= len; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    float process (float x)
    {
        const float d = buf[(size_t) pos];
        const float v = x + (float) g * d;
        buf[(size_t) pos] = v;
        pos = (pos + 1) % len;
        return d - (float) g * v;
    }
};

enum class LfoShape { sine, triangle, square, sawUp, sawDown, random };
inline juce::StringArray lfoShapeNames() { return { "SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN", "RANDOM" }; }

struct Lfo
{
    double phase = 0, inc = 0, held = 0, target = 0;
    juce::Random rng;
    void set (double hz, double fs) { inc = hz / fs; }
    double next (LfoShape shape, double offset = 0.0)
    {
        double p = phase + offset;
        p -= std::floor (p);
        double y = 0;
        switch (shape)
        {
            case LfoShape::sine: y = std::sin (2 * kPi * p); break;
            case LfoShape::triangle: y = 1.0 - 4.0 * std::abs (p - 0.5); break;
            case LfoShape::square: y = p < 0.5 ? 1.0 : -1.0; break;
            case LfoShape::sawUp: y = 2 * p - 1; break;
            case LfoShape::sawDown: y = 1 - 2 * p; break;
            case LfoShape::random: y = held + (target - held) * p; break;
        }
        return y;
    }
    void advance()
    {
        phase += inc;
        if (phase >= 1.0) { phase -= 1.0; held = target; target = rng.nextDouble() * 2 - 1; }
    }
};

/** Mix/Level stage shared by every block: dry/wet mix and output level in dB, smoothed. */
/** Wet over a dry path that stays at unity (Kill Dry ramps the dry out), then the block's Level. `mix` carries the Wet
    fraction. */
struct MixLevel
{
    juce::SmoothedValue<float> mix, level, dry;
    void prepare (double fs) { mix.reset (fs, 0.03); level.reset (fs, 0.03); dry.reset (fs, 0.02); }
    void setTargets (float wetPercent, float levelDb, bool killDry = false)
    {
        mix.setTargetValue (wetPercent / 100.0f);
        level.setTargetValue (juce::Decibels::decibelsToGain (levelDb));
        dry.setTargetValue (killDry ? 0.0f : 1.0f);
    }
};

} // namespace openguitarmultifx::mt
