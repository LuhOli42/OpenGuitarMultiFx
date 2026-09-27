#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** Moving, adding or removing a block rebuilds the whole SignalGraph, and AudioEngine::setSignalGraph() calls prepare() on
    EVERY processor in it -- including the ones that are playing right now (the graph does not own them). A prepare() with the
    same sample rate and block size must therefore leave a running processor's sound alone: one that clears its state, its
    delay line, or re-settles its circuit is a pop every time any block is touched. */
class RePrepareTransparencyTests : public juce::UnitTest
{
public:
    RePrepareTransparencyTests() : juce::UnitTest ("RePrepareTransparency", "Engine") {}

    static constexpr double sr = 48000.0;
    static constexpr int block = 128;

    /** A plucked-note style signal, deterministic. */
    static void fill (juce::AudioBuffer<float>& buf, long long& n)
    {
        for (int i = 0; i < buf.getNumSamples(); ++i, ++n)
        {
            const double t = (double) n / sr;
            const double env = std::exp (-std::fmod (t, 0.5) * 3.0);
            const float v = (float) (0.25 * env * (std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * t)
                                                   + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 392.0 * t)));
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample (ch, i, v);
        }
    }

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        for (const auto& key : registry.getRegisteredNames())
        {
            beginTest ("a second prepare() with the same arguments does not disturb " + key);

            auto run = [&] (bool reprepare)
            {
                auto fx = registry.create (key);
                fx->prepareIfNeeded (sr, block, 2);
                std::vector<float> out;
                juce::AudioBuffer<float> buf (2, block);
                long long n = 0;
                const int blocks = (int) (1.5 * sr / block);
                for (int b = 0; b < blocks; ++b)
                {
                    if (reprepare && b == blocks / 2)
                        fx->prepareIfNeeded (sr, block, 2); // what SignalGraph::prepare() does on every chain change
                    fill (buf, n);
                    fx->process (buf);
                    for (int i = 0; i < block; ++i)
                        out.push_back (buf.getSample (0, i));
                }
                return out;
            };

            const auto plain = run (false);
            const auto again = run (true);
            const size_t from = plain.size() / 2;
            double worst = 0.0, level = 0.0;
            for (size_t i = from; i < plain.size(); ++i)
            {
                worst = std::max (worst, (double) std::abs (plain[i] - again[i]));
                level = std::max (level, (double) std::abs (plain[i]));
            }
            logMessage (key + ": peak output " + juce::String (level, 3) + ", largest difference after the re-prepare " + juce::String (worst, 6));
            expect (std::isfinite (worst));
            expectLessThan (worst, 1.0e-4 + 1.0e-3 * level);
        }
    }
};

static RePrepareTransparencyTests rePrepareTransparencyTests;

} // namespace openguitarmultifx
