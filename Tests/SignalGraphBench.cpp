#include "Engine/SignalGraph.h"
#include "EffectRegistry.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only (SIGNAL_GRAPH_BENCH=1): builds a SignalGraph with one
    processor lane per SIGNAL_GRAPH_BENCH_LANES key (default two heavy
    amps) -- the classic "two parallel amp lanes" preset -- and times
    graph.process() wall-clock per block serially (OGMFX_DISABLE_ROW_PARALLEL)
    vs with the row-parallel dispatch enabled, plus a serial-chain control
    to show a non-parallel topology is unaffected. 128-sample blocks at
    48 kHz, stereo, plucked-note input (same generator as PresetChainBench). */
class SignalGraphBench : public juce::UnitTest
{
public:
    SignalGraphBench() : juce::UnitTest ("SignalGraphBench", "Probe") {}

    void runTest() override
    {
        if (std::getenv ("SIGNAL_GRAPH_BENCH") == nullptr)
            return;

        constexpr double sr = 48000.0;
        constexpr int blockSize = 128;
        const double budgetMs = 1000.0 * blockSize / sr;

        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        juce::StringArray keys;
        if (auto* env = std::getenv ("SIGNAL_GRAPH_BENCH_LANES"))
            keys.addTokens (env, ",", "");
        else
            keys.addTokens ("DualRectifierStyleAmplifier,ENGLPowerballStyleAmplifier", ",", "");

        std::vector<std::unique_ptr<EffectProcessor>> laneFx, serialFx, eqFxA, eqFxB;
        for (auto& k : keys)
        {
            laneFx.push_back (registry.create (k));
            serialFx.push_back (registry.create (k));
            eqFxA.push_back (registry.create (k));
            eqFxB.push_back (registry.create (k));
            if (laneFx.back() == nullptr)
                logMessage ("  !! could not create " + k);
        }

        auto fillBuffer = [&] (juce::AudioBuffer<float>& buf, double& phase, double& age, double& level, double& freq, juce::Random& rng)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                age += 1.0 / sr;
                if (age > 0.4)
                {
                    age = 0.0;
                    freq = 82.0 * std::pow (2.0, rng.nextInt (24) / 12.0);
                    level = 0.3 * (0.3 + 0.7 * rng.nextDouble());
                }
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                const double x = level * std::exp (-age * 2.5) * (std::sin (phase) + 0.45 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase));
                buf.setSample (0, i, (float) x);
                buf.setSample (1, i, (float) x);
            }
        };

        auto timeGraph = [&] (SignalGraph& graph, int warmup, int blocks)
        {
            juce::AudioBuffer<float> buf (2, blockSize);
            double phase = 0.0, age = 1.0e9, level = 0.0, freq = 110.0;
            juce::Random rng (3);
            for (int b = 0; b < warmup; ++b)
            {
                buf.clear();
                fillBuffer (buf, phase, age, level, freq, rng);
                graph.process (buf);
            }

            std::vector<double> ms;
            ms.reserve ((size_t) blocks);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                fillBuffer (buf, phase, age, level, freq, rng);
                const auto t0 = std::chrono::steady_clock::now();
                graph.process (buf);
                ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
            }
            std::sort (ms.begin(), ms.end());
            double sum = 0.0;
            for (double v : ms) sum += v;
            return std::array<double, 3> { sum / (double) ms.size(), ms[ms.size() * 99 / 100], ms.back() };
        };

        auto report = [&] (const char* label, const std::array<double, 3>& m)
        {
            logMessage ("  " + juce::String (label) + ": avg " + juce::String (m[0], 3) + " ms/block ("
                        + juce::String (100.0 * m[0] / budgetMs, 1) + "% cpu)  p99 " + juce::String (m[1], 3)
                        + "  max " + juce::String (m[2], 3));
        };

        // --- The 2-lane (or N-lane) parallel preset ---
        SignalGraph graph;
        for (int i = 0; i < keys.size(); ++i)
        {
            graph.setLaneReadsDeviceInput (i, true);
            graph.setLaneProcessors (i, { laneFx[(size_t) i].get() });
            graph.setLaneWritesDeviceOutput (i, true);
        }

        // Remember the caller's kill-switch state: the serial A-B needs
        // it set and the parallel run needs it clear, but a caller who
        // exported it expects it still set after the bench.
        const bool callerDisabled = std::getenv ("OGMFX_DISABLE_ROW_PARALLEL") != nullptr;

        setenv ("OGMFX_DISABLE_ROW_PARALLEL", "1", 1);
        graph.prepare (sr, blockSize, 2);
        const auto serial = timeGraph (graph, 500, 2000);

        unsetenv ("OGMFX_DISABLE_ROW_PARALLEL");
        graph.prepare (sr, blockSize, 2);
        const auto parallel = timeGraph (graph, 500, 2000);

        if (callerDisabled)
            setenv ("OGMFX_DISABLE_ROW_PARALLEL", "1", 1);

        // --- Serial-chain control: all the same processors on ONE lane ---
        SignalGraph chain;
        std::vector<EffectProcessor*> all;
        for (auto& fx : serialFx) all.push_back (fx.get());
        chain.setLaneReadsDeviceInput (0, true);
        chain.setLaneProcessors (0, std::move (all));
        chain.setLaneWritesDeviceOutput (0, true);
        chain.prepare (sr, blockSize, 2);
        const auto oneLane = timeGraph (chain, 500, 2000);

        beginTest ("row-parallel speedup");
        report ("serial   (env-disabled)", serial);
        report ("parallel (workers)     ", parallel);
        report ("one-lane serial chain  ", oneLane);
        logMessage ("  speedup " + juce::String (serial[0] / parallel[0], 2) + "x; lanes run by workers: "
                    + juce::String (graph.getJobsRunByWorkers()) + "; cpu " + juce::String (100.0 * serial[0] / budgetMs, 1)
                    + "% -> " + juce::String (100.0 * parallel[0] / budgetMs, 1) + "% of the " + juce::String (budgetMs, 2) + " ms budget");

        expect (graph.getJobsRunByWorkers() > 0, "parallel dispatch engaged");
        expect (parallel[0] < serial[0], "parallel faster than serial");

        // --- Output equivalence: the same input through fresh instances of
        // the same real processors must give bit-identical output serial vs
        // parallel (each lane's chain is deterministic and order-preserved) ---
        SignalGraph eqSerial, eqParallel;
        for (int i = 0; i < keys.size(); ++i)
        {
            eqSerial.setLaneReadsDeviceInput (i, true);
            eqSerial.setLaneProcessors (i, { eqFxA[(size_t) i].get() });
            eqSerial.setLaneWritesDeviceOutput (i, true);
            eqParallel.setLaneReadsDeviceInput (i, true);
            eqParallel.setLaneProcessors (i, { eqFxB[(size_t) i].get() });
            eqParallel.setLaneWritesDeviceOutput (i, true);
        }
        setenv ("OGMFX_DISABLE_ROW_PARALLEL", "1", 1);
        eqSerial.prepare (sr, blockSize, 2);
        unsetenv ("OGMFX_DISABLE_ROW_PARALLEL");
        eqParallel.prepare (sr, blockSize, 2);

        juce::AudioBuffer<float> inA (2, blockSize), inB (2, blockSize);
        double phase = 0.0, age = 1.0e9, level = 0.0, freq = 110.0;
        juce::Random rng (7);
        float maxDiff = 0.0f;
        for (int b = 0; b < 300; ++b)
        {
            inA.clear();
            fillBuffer (inA, phase, age, level, freq, rng);
            inB.makeCopyOf (inA);
            eqSerial.process (inA);
            eqParallel.process (inB);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    maxDiff = juce::jmax (maxDiff, std::abs (inA.getSample (ch, i) - inB.getSample (ch, i)));
        }
        logMessage ("  equivalence: max |serial - parallel| sample diff over 300 blocks = " + juce::String (maxDiff, 8));
        expect (eqParallel.getJobsRunByWorkers() > 0, "equivalence run actually dispatched to workers");
        expectWithinAbsoluteError (maxDiff, 0.0f, 1.0e-6f, "parallel output bit-identical to serial");
    }
};

static SignalGraphBench signalGraphBench;

} // namespace openguitarmultifx
