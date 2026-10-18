#pragma once

#include "Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>

namespace openguitarmultifx::pipelinetest
{

/** The acceptance test of docs/PerformancePipeline.md for a pedal with both engines: `make (pipeline)` builds a
    prepared-ready instance on either engine, `settings` the knob sets to compare. Level / octave-band timbre on a
    decaying chord, transients (peaks, 5 ms envelopes through hard-gated bursts, onset frame), random-knob hot-burst
    robustness, silence settling, and CPU. Results go to `log`; returns the worst numbers. */
struct Verdict { double level = 0, band = 0, peak = 0, envelope = 0; int onset = 0; bool finite = true; double tail = 0, speedup = 0; };

using Knobs = std::vector<std::pair<juce::String, float>>;

inline void setParams (EffectProcessor& p, const Knobs& values)
{
    for (auto* prm : p.getParameters()->getParameters (true))
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
            for (auto& v : values)
                if (f->paramID == v.first)
                    *f = v.second;
}

inline std::vector<float> run (EffectProcessor& p, const std::vector<float>& in, double* cpu = nullptr)
{
    std::vector<float> out;
    out.reserve (in.size());
    juce::AudioBuffer<float> buf (1, 256);
    double t = 0.0;
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, in.size() - pos);
        buf.setSize (1, n, false, false, true);
        for (int i = 0; i < n; ++i) buf.setSample (0, i, in[pos + (size_t) i]);
        const auto t0 = std::chrono::steady_clock::now();
        p.process (buf);
        t += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        for (int i = 0; i < n; ++i) out.push_back (buf.getSample (0, i));
    }
    if (cpu != nullptr) *cpu += t;
    return out;
}

inline std::vector<float> chord (double rms, double seconds, double sr)
{
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    double norm = 0.0;
    for (int k = 1; k <= 8; ++k) norm += 0.5 / (double) (k * k);
    norm *= 3.0;
    const double scale = rms / std::sqrt (norm);
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        double v = 0.0;
        for (double f0 : { 82.41, 123.47, 164.81 })
            for (int k = 1; k <= 8; ++k)
                v += std::sin (twoPi * f0 * k * t + 0.3 * k) / (double) k * std::exp (-t * (0.6 + 0.15 * k));
        x[i] = (float) (scale * v * 1.6);
    }
    return x;
}

inline std::vector<float> bursts (double amp, double sr)
{
    std::vector<float> in ((size_t) (1.8 * sr));
    for (size_t i = 0; i < in.size(); ++i)
    {
        const double t = (double) i / sr;
        double v = 0.0;
        for (double f0 : { 110.0, 164.81, 220.0 })
            for (int k = 1; k <= 6; ++k) v += std::sin (6.283185307 * f0 * k * t + k) / k;
        in[i] = std::fmod (t, 0.6) < 0.3 ? (float) (amp * v / 3.0) : 0.0f;
    }
    return in;
}

struct Bands { double db[8]; double totalDb; };
inline Bands analyse (const std::vector<float>& x, double sr)
{
    constexpr int order = 15, n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data ((size_t) n * 2, 0.0f);
    const size_t start = 12000;
    for (int i = 0; i < n && start + (size_t) i < x.size(); ++i)
        data[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) n));
    fft.performFrequencyOnlyForwardTransform (data.data());
    Bands b {};
    double total = 0.0;
    const double edges[9] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000 };
    for (int band = 0; band < 8; ++band)
    {
        double e = 0.0;
        for (int k = (int) (edges[band] / sr * n); k < (int) (edges[band + 1] / sr * n); ++k)
            e += (double) data[(size_t) k] * data[(size_t) k];
        b.db[band] = 10.0 * std::log10 (e + 1.0e-30);
        total += e;
    }
    b.totalDb = 10.0 * std::log10 (total + 1.0e-30);
    return b;
}

inline Verdict compare (const std::function<std::unique_ptr<EffectProcessor> (bool)>& make, const std::vector<Knobs>& settings,
                        const std::function<void (const juce::String&)>& log, double sr = 48000.0)
{
    Verdict v;
    double cpuFull = 0.0, cpuPipe = 0.0;
    for (const auto& knobs : settings)
    {
        juce::String tag;
        for (auto& k : knobs) tag << k.first << "=" << juce::String (k.second, 2) << " ";
        for (double rms : { 0.05, 0.3 })
        {
            std::vector<float> out[2];
            for (int e = 0; e < 2; ++e)
            {
                auto p = make (e == 1);
                setParams (*p, knobs);
                p->prepare (sr, 256, 1);
                out[e] = run (*p, chord (rms, 3.0, sr), e == 0 ? &cpuFull : &cpuPipe);
            }
            for (auto s : out[1]) v.finite = v.finite && std::isfinite (s);
            const auto a = analyse (out[0], sr), b = analyse (out[1], sr);
            double band = 0.0;
            for (int i = 0; i < 8; ++i)
                if (a.db[i] > a.totalDb - 30.0) band = std::max (band, std::abs ((b.db[i] - b.totalDb) - (a.db[i] - a.totalDb)));
            log ("  " + tag + "in " + juce::String (rms) + ": level " + juce::String (b.totalDb - a.totalDb, 2) + " dB, band " + juce::String (band, 2) + " dB");
            v.level = std::max (v.level, std::abs (b.totalDb - a.totalDb));
            v.band = std::max (v.band, band);
        }
        for (double amp : { 0.08, 0.5 })
        {
            std::vector<float> out[2];
            for (int e = 0; e < 2; ++e)
            {
                auto p = make (e == 1);
                setParams (*p, knobs);
                p->prepare (sr, 256, 1);
                out[e] = run (*p, bursts (amp, sr));
            }
            auto peakDb = [] (const std::vector<float>& x) { float pk = 1e-9f; for (auto s : x) pk = std::max (pk, std::abs (s)); return 20.0 * std::log10 (pk); };
            const int frame = (int) (0.005 * sr);
            auto env = [frame] (const std::vector<float>& x) {
                std::vector<double> e;
                for (size_t s0 = 0; s0 + (size_t) frame <= x.size(); s0 += (size_t) frame)
                {
                    double acc = 0.0;
                    for (int i = 0; i < frame; ++i) acc += (double) x[s0 + (size_t) i] * x[s0 + (size_t) i];
                    e.push_back (10.0 * std::log10 (acc / frame + 1e-20));
                }
                return e;
            };
            const auto ea = env (out[0]), eb = env (out[1]);
            const double top = *std::max_element (ea.begin(), ea.end());
            double ed = 0.0;
            size_t wk = 0;
            for (size_t k = 0; k < ea.size(); ++k) if (ea[k] > top - 40.0 && std::abs (eb[k] - ea[k]) > ed) { ed = std::abs (eb[k] - ea[k]); wk = k; }
            log ("    worst envelope frame " + juce::String ((double) wk * 0.005, 3) + " s: netlist " + juce::String (ea[wk] - top, 1) + " dB, pipeline "
                 + juce::String (eb[wk] - top, 1) + " dB (re top)");
            auto onset = [top] (const std::vector<double>& e) { for (size_t k = 0; k < e.size(); ++k) if (e[k] > top - 10.0) return (int) k; return -1; };
            const double pd = std::abs (peakDb (out[1]) - peakDb (out[0]));
            log ("  " + tag + "bursts " + juce::String (amp) + ": peak " + juce::String (pd, 2) + " dB, envelope " + juce::String (ed, 2) + " dB");
            v.peak = std::max (v.peak, pd);
            v.envelope = std::max (v.envelope, ed);
            v.onset = std::max (v.onset, std::abs (onset (eb) - onset (ea)));
        }
    }
    // robustness: random knobs, 3 V bursts, stereo with a channel split half way, then silence
    {
        auto p = make (true);
        p->prepare (sr, 256, 2);
        juce::Random rng (11);
        juce::AudioBuffer<float> buf (2, 256);
        for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
        {
            if (blockIndex % 10 == 0)
            {
                Knobs k;
                juce::String kt;
                for (auto& kv : settings.front()) { k.push_back ({ kv.first, rng.nextFloat() }); kt << kv.first << "=" << juce::String (k.back().second, 2) << " "; }
                setParams (*p, k);
                if (blockIndex >= 290 && blockIndex <= 310) log ("    knobs @" + juce::String (blockIndex) + ": " + kt);
            }
            const float amp = (blockIndex % 37) < 3 ? 3.0f : 0.2f;
            for (int i = 0; i < 256; ++i)
            {
                const float x = amp * std::sin (0.07f * (float) (blockIndex * 256 + i)) + 0.01f * (rng.nextFloat() - 0.5f);
                buf.setSample (0, i, x);
                buf.setSample (1, i, blockIndex > 200 ? -x : x);
            }
            p->process (buf);
            float pk = 0.0f;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { v.finite = v.finite && std::isfinite (buf.getSample (c, i)) && std::abs (buf.getSample (c, i)) < 50.0f; pk = std::max (pk, std::abs (buf.getSample (c, i))); }
            if (! v.finite) { log ("  robustness: diverged at block " + juce::String (blockIndex) + " (input amp " + juce::String (amp) + ", peak " + juce::String (pk) + ")"); break; }
        }
        for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
        {
            buf.clear();
            p->process (buf);
            if (blockIndex > 300) for (int i = 0; i < 256; ++i) v.tail = std::max (v.tail, (double) std::abs (buf.getSample (0, i)));
        }
    }
    v.speedup = cpuFull / std::max (1e-9, cpuPipe);
    log ("  worst: level " + juce::String (v.level, 2) + " dB, band " + juce::String (v.band, 2) + " dB, peak " + juce::String (v.peak, 2)
         + " dB, envelope " + juce::String (v.envelope, 2) + " dB, onset " + juce::String (v.onset) + " frames, tail " + juce::String (v.tail, 8)
         + ", finite " + juce::String (v.finite ? "yes" : "NO") + ", speed-up x" + juce::String (v.speedup, 2));
    return v;
}

} // namespace openguitarmultifx::pipelinetest
