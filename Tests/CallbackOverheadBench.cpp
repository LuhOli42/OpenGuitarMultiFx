#include "Engine/PitchDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only (CALLBACK_BENCH=1): what the tuner costs, split between the audio thread (the ring-buffer write, which is all
    it does now) and the control thread (YIN's own pass). Before 2026-09-21 the analysis ran from the audio thread and
    landed entirely inside one block -- 0.71 ms of a 2.67 ms budget at the old 70 Hz floor, and the wider floor this now
    uses would have been ~4 ms, i.e. a guaranteed dropout. */
class CallbackOverheadBench : public juce::UnitTest
{
public:
    CallbackOverheadBench() : juce::UnitTest ("CallbackOverheadBench", "Probe") {}

    void runTest() override
    {
        if (std::getenv ("CALLBACK_BENCH") == nullptr)
            return;

        beginTest ("tuner cost, audio thread vs control thread");
        juce::ScopedNoDenormals noDenormals;

        const double sr = 48000.0;
        const int blockSize = 128;
        const double budgetMs = 1000.0 * blockSize / sr;

        PitchDetector detector;
        detector.prepare (sr);

        std::vector<float> in ((size_t) blockSize);
        std::vector<double> pushMs, analyseMs;
        double phase = 0.0;
        const int blocks = (int) (10.0 * sr / blockSize);

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                in[(size_t) i] = (float) (0.2 * (std::sin (phase) + 0.3 * std::sin (2.0 * phase)));
            }

            auto t0 = std::chrono::steady_clock::now();
            detector.pushSamples (in.data(), blockSize);
            auto t1 = std::chrono::steady_clock::now();
            pushMs.push_back (std::chrono::duration<double, std::milli> (t1 - t0).count());

            if ((b % 8) == 0) // the control thread's 20 Hz-ish rate
            {
                auto a0 = std::chrono::steady_clock::now();
                detector.analyse();
                analyseMs.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a0).count());
            }
        }

        auto report = [&] (const char* name, std::vector<double> v, const char* thread)
        {
            std::sort (v.begin(), v.end());
            double sum = 0.0;
            for (double x : v) sum += x;
            logMessage (juce::String (name) + " (" + thread + "): mean " + juce::String (sum / (double) v.size(), 4)
                        + " ms, max " + juce::String (v.back(), 4) + " ms"
                        + " (" + juce::String (100.0 * v.back() / budgetMs, 1) + "% of one audio block)");
        };
        report ("ring-buffer write", pushMs, "audio thread");
        report ("YIN analysis", analyseMs, "control thread");
        logMessage ("audio block budget " + juce::String (budgetMs, 2) + " ms");
    }
};

static CallbackOverheadBench callbackOverheadBench;

} // namespace openguitarmultifx
