#include "EffectRegistry.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only (CHAIN_SPIKE=1): the user's chain -- a distortion at full gain into the Bassman into a cab -- played from soft
    to loud, timing EVERY 128-sample block. What matters is not the average (the cost map already has that) but the worst
    blocks: the audio thread has 2.67 ms per block at 48 kHz, and one block over that is a dropout the user hears as the
    sound "travando", with the CPU meter reading over 100%. */
class ChainSpikeBench : public juce::UnitTest
{
public:
    ChainSpikeBench() : juce::UnitTest ("ChainSpikeBench", "Probe") {}

    static constexpr double sr = 48000.0;
    static constexpr int blockSize = 128;

    void runTest() override
    {
        if (std::getenv ("CHAIN_SPIKE") == nullptr)
            return;

        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        const juce::String pedalKey = std::getenv ("CHAIN_PEDAL") != nullptr ? std::getenv ("CHAIN_PEDAL") : "HM2StyleDistortion";
        const bool withAmp = std::getenv ("CHAIN_NO_AMP") == nullptr;
        const bool withCab = std::getenv ("CHAIN_NO_CAB") == nullptr;

        beginTest ("spike profile: " + pedalKey + (withAmp ? " -> Bassman" : "") + (withCab ? " -> Cab" : ""));

        BassmanStyleAmplifierProcessor* ampProbe = nullptr;
        std::vector<std::unique_ptr<EffectProcessor>> chain;
        if (pedalKey != "none")
        {
            auto pedal = registry.create (pedalKey);
            for (auto* p : pedal->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    *f = f->getNormalisableRange().end; // everything at maximum, as the user described
            chain.push_back (std::move (pedal));
        }
        if (withAmp)
        {
            auto amp = std::make_unique<BassmanStyleAmplifierProcessor>();
            if (std::getenv ("CHAIN_AMP_MAX") != nullptr)
                for (auto* p : amp->getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == "bm_vol_normal" || f->paramID == "bm_vol_bright" || f->paramID == "bm_power")
                            *f = f->getNormalisableRange().end;
            if (std::getenv ("CHAIN_AMP_INPUT") != nullptr)
                for (auto* p : amp->getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == "bm_input")
                            *f = (float) std::atof (std::getenv ("CHAIN_AMP_INPUT"));
            ampProbe = amp.get();
            chain.push_back (std::move (amp));
        }
        if (withCab)
            chain.push_back (registry.create ("Cab"));

        for (auto& fx : chain)
            fx->prepare (sr, blockSize, 2);

        // Warm up (settle the circuits, fill the IR).
        juce::AudioBuffer<float> buf (2, blockSize);
        for (int b = 0; b < 200; ++b)
        {
            buf.clear();
            for (auto& fx : chain)
                fx->process (buf);
        }

        // Play: repeated picks whose level sweeps from very soft to very loud and back, so the profile covers the whole
        // dynamic range the user plays over (this is where they report the meter jumping around).
        std::vector<double> ms;
        const int totalBlocks = (int) ((std::getenv ("CHAIN_LEVEL") != nullptr ? 6.0 : 20.0) * sr / blockSize);
        ms.reserve ((size_t) totalBlocks);
        double phase = 0.0, noteAge = 1.0e9, noteLevel = 0.0, noteFreq = 110.0;
        juce::Random rng (11);
        long long n = 0;
        double loudestBlockLevel = 0.0, loudestBlockMs = 0.0;
        int logged = 0;

        for (int b = 0; b < totalBlocks; ++b)
        {
            double blockPeak = 0.0;
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                noteAge += 1.0 / sr;
                if (noteAge > 0.4)
                {
                    noteAge = 0.0;
                    noteFreq = 82.0 * std::pow (2.0, rng.nextInt (24) / 12.0);
                    // level sweeps 0.02 -> 0.9 -> 0.02 over the whole run: soft picking to digging in
                    const double fixedLevel = std::getenv ("CHAIN_LEVEL") != nullptr ? std::atof (std::getenv ("CHAIN_LEVEL")) : -1.0;
                    const double sweep = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) b / (double) totalBlocks);
                    noteLevel = fixedLevel > 0.0 ? fixedLevel : 0.02 + 0.88 * sweep;
                }
                phase += 2.0 * juce::MathConstants<double>::pi * noteFreq / sr;
                const double env = std::exp (-noteAge * 2.5);
                const double x = noteLevel * env * (std::sin (phase) + 0.45 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase));
                buf.setSample (0, i, (float) x);
                buf.setSample (1, i, (float) x);
                blockPeak = std::max (blockPeak, std::abs (x));
            }

            const int recoveriesBefore = ampProbe != nullptr ? ampProbe->debugRecoveries() : 0;
            double perStage[4] = {};
            const auto t0 = std::chrono::steady_clock::now();
            int stage = 0;
            for (auto& fx : chain)
            {
                const auto s0 = std::chrono::steady_clock::now();
                fx->process (buf);
                if (stage < 4)
                    perStage[stage] = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - s0).count();
                ++stage;
            }
            const double elapsed = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            if (elapsed > 2.0 && logged < 12)
            {
                ++logged;
                logMessage ("  SPIKE block " + juce::String (b) + ": " + juce::String (elapsed, 2) + " ms  [stages "
                            + juce::String (perStage[0], 2) + " / " + juce::String (perStage[1], 2) + " / " + juce::String (perStage[2], 2)
                            + "]  input peak " + juce::String (blockPeak, 3)
                            + (ampProbe != nullptr ? "  amp recoveries this block " + juce::String (ampProbe->debugRecoveries() - recoveriesBefore)
                                                     + " (total " + juce::String (ampProbe->debugRecoveries()) + ")" : ""));
            }
            ms.push_back (elapsed);
            if (elapsed > loudestBlockMs)
            {
                loudestBlockMs = elapsed;
                loudestBlockLevel = blockPeak;
            }
        }

        // What the on-screen meter actually shows, simulated from the same block times: the decaying peak hold added on
        // 2026-09-21 against the raw last-block ratio it replaced.
        {
            const double blockSeconds = (double) blockSize / sr;
            const double decay = std::exp (-blockSeconds / 1.0);
            double held = 0.0, heldSum = 0.0, heldMin = 1.0e9, heldMax = 0.0;
            double rawSum = 0.0, rawMin = 1.0e9, rawMax = 0.0;
            const double budgetMs = 1000.0 * blockSize / sr;
            for (double v : ms)
            {
                const double instant = v / budgetMs;
                held = juce::jmax (instant, held * decay);
                heldSum += held; heldMin = std::min (heldMin, held); heldMax = std::max (heldMax, held);
                rawSum += instant; rawMin = std::min (rawMin, instant); rawMax = std::max (rawMax, instant);
            }
            logMessage ("  METER peak-hold (what is on screen now): average reading " + juce::String (100.0 * heldSum / (double) ms.size(), 1)
                        + "%, range " + juce::String (100.0 * heldMin, 1) + "-" + juce::String (100.0 * heldMax, 1) + "%");
            logMessage ("  METER raw last-block (what it showed before): average " + juce::String (100.0 * rawSum / (double) ms.size(), 1)
                        + "%, range " + juce::String (100.0 * rawMin, 1) + "-" + juce::String (100.0 * rawMax, 1) + "%");
        }

        auto sorted = ms;
        std::sort (sorted.begin(), sorted.end());
        auto pct = [&] (double p) { return sorted[(size_t) juce::jlimit (0, (int) sorted.size() - 1, (int) (p * (double) sorted.size()))]; };
        const double budget = 1000.0 * blockSize / sr;
        double mean = 0.0;
        for (double v : ms) mean += v;
        mean /= (double) ms.size();
        int over = 0, over80 = 0;
        for (double v : ms) { if (v > budget) ++over; if (v > 0.8 * budget) ++over80; }

        logMessage ("budget " + juce::String (budget, 2) + " ms/block");
        logMessage ("  mean " + juce::String (mean, 3) + " ms (" + juce::String (100.0 * mean / budget, 1) + "%)"
                    + ", median " + juce::String (pct (0.5), 3)
                    + ", p90 " + juce::String (pct (0.9), 3)
                    + ", p99 " + juce::String (pct (0.99), 3)
                    + ", p99.9 " + juce::String (pct (0.999), 3)
                    + ", max " + juce::String (sorted.back(), 3) + " ms (" + juce::String (100.0 * sorted.back() / budget, 1) + "%)");
        if (ampProbe != nullptr)
            logMessage ("  amp recoveries total " + juce::String (ampProbe->debugRecoveries()) + ", failure rate " + juce::String (ampProbe->getSolveFailureRate(), 6));
        logMessage ("  blocks over budget: " + juce::String (over) + " / " + juce::String ((int) ms.size())
                    + "; over 80% of budget: " + juce::String (over80)
                    + "; worst block's input peak " + juce::String (loudestBlockLevel, 3));
    }
};

static ChainSpikeBench chainSpikeBench;

} // namespace openguitarmultifx
