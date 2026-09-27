#include "EffectRegistry.h"
#include "Effects/NodalCircuit.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only (PRESET_BENCH=<preset xml>): builds a saved preset's chain from the registry, plays plucked notes at a few levels and times EACH
    block of the chain separately, 128-sample blocks at 48 kHz: average and worst as a share of the 2.67 ms budget. */
class PresetChainBench : public juce::UnitTest
{
public:
    PresetChainBench() : juce::UnitTest ("PresetChainBench", "Probe") {}

    void runTest() override
    {
        if (std::getenv ("PRESET_BENCH") == nullptr)
            return;

        constexpr double sr = 48000.0;
        constexpr int blockSize = 128;
        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        beginTest ("preset chain cost");
        const auto xml = juce::XmlDocument::parse (juce::File (std::getenv ("PRESET_BENCH")));
        expect (xml != nullptr, "preset parsed");
        if (xml == nullptr)
            return;

        std::vector<std::unique_ptr<EffectProcessor>> chain;
        std::vector<juce::String> names;
        for (auto* block : xml->getChildWithTagNameIterator ("Block"))
        {
            auto fx = registry.create (block->getStringAttribute ("key"));
            if (fx == nullptr || block->getIntAttribute ("bypassed") != 0)
                continue;
            if (auto* state = block->getChildByName ("EffectState"))
                fx->setState (*state);
            names.push_back (block->getStringAttribute ("key"));
            chain.push_back (std::move (fx));
        }

        for (auto& fx : chain)
            fx->prepare (sr, blockSize, 2);

        for (double maxLevel : { 0.05, 0.2, 0.6 })
        {
            juce::AudioBuffer<float> buf (2, blockSize);
            for (int b = 0; b < 200; ++b)
            {
                buf.clear();
                for (auto& fx : chain)
                    fx->process (buf);
            }

            std::vector<std::vector<double>> ms (chain.size());
            double phase = 0.0, age = 1.0e9, level = 0.0, freq = 110.0;
            juce::Random rng (3);
            const int totalBlocks = (int) (6.0 * sr / blockSize);
            for (int b = 0; b < totalBlocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    age += 1.0 / sr;
                    if (age > 0.4)
                    {
                        age = 0.0;
                        freq = 82.0 * std::pow (2.0, rng.nextInt (24) / 12.0);
                        level = maxLevel * (0.3 + 0.7 * rng.nextDouble());
                    }
                    phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                    const double x = level * std::exp (-age * 2.5) * (std::sin (phase) + 0.45 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase));
                    buf.setSample (0, i, (float) x);
                    buf.setSample (1, i, (float) x);
                }
                for (size_t k = 0; k < chain.size(); ++k)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    chain[k]->process (buf);
                    ms[k].push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
                }
            }

            const double budget = 1000.0 * blockSize / sr;
            juce::String line = "level " + juce::String (maxLevel, 2) + ":";
            double total = 0.0;
            for (size_t k = 0; k < chain.size(); ++k)
            {
                double sum = 0.0;
                for (double v : ms[k]) sum += v;
                std::sort (ms[k].begin(), ms[k].end());
                const double avg = 100.0 * sum / (double) ms[k].size() / budget, worst = 100.0 * ms[k].back() / budget, p99 = 100.0 * ms[k][ms[k].size() * 99 / 100] / budget;
                total += avg;
                line << "  " << names[k] << " avg " << juce::String (avg, 1) << "% p99 " << juce::String (p99, 0) << "% max " << juce::String (worst, 0) << "%";
            }
            logMessage (line + "  | chain avg " + juce::String (total, 1) + "%");
        }
    }
};

static PresetChainBench presetChainBench;

} // namespace openguitarmultifx
