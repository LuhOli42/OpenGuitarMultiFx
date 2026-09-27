#pragma once

#include "Effects/EffectProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** What a processor's output and a few internal nodes did under a pure sine: the amplitude of the fundamental (correlation
    over a whole number of cycles, after a warm-up) and the min/max over the same window. The circuit-pedal tests use it
    to compare a node's small-signal gain against a closed form. */
struct SineProbe
{
    double out = 0.0;              // fundamental amplitude at the output
    float outMax = 0.0f, outMin = 0.0f;
    std::vector<double> tap;       // fundamental amplitude at each tap
    std::vector<double> tapMax, tapMin;
};

template <typename Processor>
SineProbe probeSine (Processor& p, const std::vector<std::function<double()>>& taps, double freq, double amp,
                     double sampleRate = 48000.0, double warmup = 0.4, double measure = 0.2)
{
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    const long long total = (long long) ((warmup + measure) * sampleRate);
    const double cycles = std::floor (measure * freq);
    const long long len = (long long) std::llround (cycles * sampleRate / freq);
    const long long start = total - len;

    SineProbe r;
    r.tap.assign (taps.size(), 0.0);
    r.tapMax.assign (taps.size(), -1.0e9);
    r.tapMin.assign (taps.size(), 1.0e9);
    std::vector<double> ts (taps.size(), 0.0), tc (taps.size(), 0.0);
    double oS = 0.0, oC = 0.0;
    r.outMax = -1.0e9f;
    r.outMin = 1.0e9f;

    juce::AudioBuffer<float> buf (1, 1);
    for (long long n = 0; n < total; ++n)
    {
        const double ph = twoPi * freq * (double) n / sampleRate;
        buf.setSample (0, 0, (float) (amp * std::sin (ph)));
        p.process (buf);
        if (n < start)
            continue;

        const double y = buf.getSample (0, 0);
        oS += y * std::sin (ph);
        oC += y * std::cos (ph);
        r.outMax = std::max (r.outMax, (float) y);
        r.outMin = std::min (r.outMin, (float) y);
        for (size_t i = 0; i < taps.size(); ++i)
        {
            const double v = taps[i]();
            ts[i] += v * std::sin (ph);
            tc[i] += v * std::cos (ph);
            r.tapMax[i] = std::max (r.tapMax[i], v);
            r.tapMin[i] = std::min (r.tapMin[i], v);
        }
    }

    const double k = 2.0 / (double) len;
    r.out = k * std::sqrt (oS * oS + oC * oC);
    for (size_t i = 0; i < taps.size(); ++i)
        r.tap[i] = k * std::sqrt (ts[i] * ts[i] + tc[i] * tc[i]);
    return r;
}

/** Sets a processor's parameters (in getParameters (true) order) from raw values. */
inline void setParams (EffectProcessor& p, std::initializer_list<float> values)
{
    auto params = p.getParameters()->getParameters (true);
    int i = 0;
    for (float v : values)
        *dynamic_cast<juce::AudioParameterFloat*> (params[i++]) = v;
}

} // namespace openguitarmultifx
