#include "EffectRegistry.h"
#include "Effects/NAMProcessor.h"

#include <filesystem>

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <vector>

#ifndef OGMFX_TEST_FIXTURES_DIR
#error "fixtures dir"
#endif

namespace openguitarmultifx
{

/** Development benchmark, run by name only ("EffectCostBench"): % of one core per registered effect at defaults. */
class EffectCostBench : public juce::UnitTest
{
public:
    EffectCostBench() : juce::UnitTest ("EffectCostBench", "Bench") {}

    void runTest() override
    {
        beginTest ("cab IR: 150 ms cap keeps level and tone");
        {
            const auto irPath = juce::SystemStats::getEnvironmentVariable ("BENCH_IR", {});
            if (irPath.isNotEmpty())
            {
                const double sr0 = 48000.0;
                const auto render = [&] (size_t maxSamples)
                {
                    juce::dsp::Convolution conv;
                    conv.prepare ({ sr0, 512, 1 });
                    conv.loadImpulseResponse (juce::File (irPath), juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, maxSamples, juce::dsp::Convolution::Normalise::yes);
                    juce::Random rnd (5);
                    std::vector<float> out;
                    juce::AudioBuffer<float> buf (1, 512);
                    for (int b = 0; b < 400; ++b)
                    {
                        for (int i = 0; i < 512; ++i)
                            buf.setSample (0, i, rnd.nextFloat() * 0.2f - 0.1f);
                        juce::dsp::AudioBlock<float> block (buf);
                        juce::dsp::ProcessContextReplacing<float> ctx (block);
                        conv.process (ctx);
                        if (b >= 100)
                            for (int i = 0; i < 512; ++i)
                                out.push_back (buf.getSample (0, i));
                    }
                    return out;
                };
                const auto full = render (0), cut = render (7200);
                juce::dsp::FFT fft (14);
                const auto band = [&] (const std::vector<float>& x, double lo, double hi)
                {
                    std::vector<float> data (32768, 0.0f);
                    for (int i = 0; i < 16384; ++i) data[(size_t) i] = x[(size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / 16384.0f));
                    fft.performFrequencyOnlyForwardTransform (data.data());
                    double e = 0.0;
                    for (int k = (int) (lo / sr0 * 16384); k < (int) (hi / sr0 * 16384); ++k) e += (double) data[(size_t) k] * data[(size_t) k];
                    return 10.0 * std::log10 (e + 1e-30);
                };
                for (auto [lo, hi] : { std::pair<double, double> { 80, 200 }, { 200, 800 }, { 800, 3000 }, { 3000, 8000 }, { 8000, 16000 } })
                    logMessage ("band " + juce::String (lo, 0) + "-" + juce::String (hi, 0) + " Hz: cut vs full " + juce::String (band (cut, lo, hi) - band (full, lo, hi), 2) + " dB");
            }
        }

        beginTest ("cost of every registered effect");
        if (juce::SystemStats::getEnvironmentVariable ("EFFECT_BENCH", {}).isEmpty())
            return; // only on request: EFFECT_BENCH=1
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        constexpr double sr = 48000.0;
        // BENCH_BLOCK=n: the host buffer size (the app's own setting can be as small as 64).
        const int block = juce::jmax (16, juce::SystemStats::getEnvironmentVariable ("BENCH_BLOCK", "128").getIntValue());
        const bool flush = juce::SystemStats::getEnvironmentVariable ("BENCH_FTZ", {}).isNotEmpty();
        std::vector<std::pair<double, juce::String>> results;
        struct Row { double playing, tail; juce::String key; double worstBlock = 0.0; };
        std::vector<Row> rows;
        const auto only = juce::SystemStats::getEnvironmentVariable ("BENCH_FILTER", {});
        for (auto key : registry.getRegisteredNames())
        {
            if (only.isNotEmpty() && ! juce::StringArray::fromTokens (only, ",", "").contains (key) && ! key.startsWith (only))
                continue;
            auto fx = registry.create (key);
            fx->prepare (sr, block, 2);
            if (auto* nam = dynamic_cast<NAMProcessor*> (fx.get()))
            {
                const auto real = juce::SystemStats::getEnvironmentVariable ("BENCH_NAM", {});
                if (key == "NeuralAmp")
                    nam->loadModel (std::filesystem::path (OGMFX_TEST_FIXTURES_DIR) / "fixtures" / "lstm.nam");
                else if (key == "NeuralAmpCab" && real.isNotEmpty())
                    nam->loadModel (std::filesystem::path (real.toStdString()));
            }
            if (key == "Cab" || key == "Reverb")
            {
                const auto ir = juce::SystemStats::getEnvironmentVariable ("BENCH_IR", {});
                if (ir.isNotEmpty())
                    fx->loadModelFile (juce::File (ir));
            }
            juce::AudioBuffer<float> buf (2, block);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            long long n = 0;
            // BENCH_SWEEP=1: every float knob keeps moving (0.5 +- 0.4, 0.5 Hz, staggered), the way a turned knob, an
            // expression pedal or a snapshot ramp drives them -- what a compiled circuit pays to re-prepare per pot move.
            const bool sweep = juce::SystemStats::getEnvironmentVariable ("BENCH_SWEEP", {}).isNotEmpty();
            std::vector<juce::AudioParameterFloat*> knobs;
            if (sweep && fx->getParameters() != nullptr)
                for (auto* prm : fx->getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                        if (f->getNormalisableRange().interval < 1.0f)
                            knobs.push_back (f);
            double worstBlock = 0.0;
            const bool paced = juce::SystemStats::getEnvironmentVariable ("BENCH_REALTIME", {}).isNotEmpty();
            const auto run = [&] (double seconds, bool signal)
            {
                double sec = 0.0;
                const int blocks = (int) (seconds * sr / block);
                for (int b = 0; b < blocks; ++b)
                {
                    for (size_t k = 0; k < knobs.size(); ++k)
                        knobs[k]->setValueNotifyingHost ((float) (0.5 + 0.4 * std::sin (twoPi * 0.5 * (double) n / sr + (double) k)));
                    for (int i = 0; i < block; ++i, ++n)
                    {
                        double v = 0.0;
                        if (signal)
                            for (int k = 1; k <= 6; ++k)
                                v += 0.06 * std::sin (twoPi * 164.81 * k * (double) n / sr) / k * (0.6 + 0.4 * std::sin (twoPi * 2.0 * (double) n / sr));
                        buf.setSample (0, i, (float) v);
                        buf.setSample (1, i, (float) v);
                    }
                    std::unique_ptr<juce::ScopedNoDenormals> ftz;
                    if (flush)
                        ftz = std::make_unique<juce::ScopedNoDenormals>();
                    const auto t0 = std::chrono::steady_clock::now();
                    fx->process (buf);
                    const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                    // BENCH_REALTIME=1: pace the blocks the way a sound card does (one per block period, idle in between),
                    // so the CPU's frequency governor sees the app's duty cycle instead of a flat-out loop.
                    if (paced)
                        std::this_thread::sleep_until (t0 + std::chrono::duration<double> ((double) block / sr));
                    sec += dt;
                    if (signal) worstBlock = std::max (worstBlock, 100.0 * dt * sr / block);
                }
                return 100.0 * sec / seconds;
            };
            run (0.5, true);
            const double playing = run (2.0, true);
            run (0.3, false); // let the burst end
            const double tail = run (6.0, false); // digital silence: what decaying tails cost
            rows.push_back ({ playing, tail, key, worstBlock });
        }
        std::sort (rows.begin(), rows.end(), [] (auto& a, auto& b) { return a.playing + a.tail > b.playing + b.tail; });
        logMessage (juce::String (flush ? "FTZ on" : "FTZ off") + ": playing % / silent tail %");
        for (auto& r : rows)
            if (r.playing + r.tail > 0.05)
                logMessage (juce::String (r.playing, 2).paddedLeft (' ', 7) + " /" + juce::String (r.tail, 2).paddedLeft (' ', 7)
                            + "  worst block " + juce::String (r.worstBlock, 1).paddedLeft (' ', 6) + " %  " + r.key);
        return;
        std::sort (results.begin(), results.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        for (auto& r : results)
            logMessage (juce::String (r.first, 2).paddedLeft (' ', 7) + " %  " + r.second);
    }
};

static EffectCostBench effectCostBench;

} // namespace openguitarmultifx
