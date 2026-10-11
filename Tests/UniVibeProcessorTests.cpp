#include "Effects/UniVibeProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class UniVibeProcessorTests : public juce::UnitTest
{
public:
    UniVibeProcessorTests() : juce::UnitTest ("UniVibeProcessor", "Effects") {}

    static std::vector<float> windowEnergies (UniVibeProcessor& vibe, double sampleRate, double toneHz,
                                               int numWindows, int windowSamples)
    {
        std::vector<float> energies;
        juce::AudioBuffer<float> buffer (1, windowSamples);
        double phase = 0.0;
        const double phaseInc = juce::MathConstants<double>::twoPi * toneHz / sampleRate;

        for (int w = 0; w < numWindows; ++w)
        {
            for (int i = 0; i < windowSamples; ++i)
            {
                buffer.setSample (0, i, (float) std::sin (phase));
                phase += phaseInc;
            }
            vibe.process (buffer);

            float energy = 0.0f;
            for (int i = 0; i < windowSamples; ++i)
                energy += std::abs (buffer.getSample (0, i));
            energies.push_back (energy);
        }
        return energies;
    }

    static float variance (const std::vector<float>& values)
    {
        double mean = 0.0;
        for (float v : values)
            mean += v;
        mean /= (double) values.size();

        double var = 0.0;
        for (float v : values)
            var += (v - mean) * (v - mean);
        return (float) (var / (double) values.size());
    }

    void runTest() override
    {
        beginTest ("zero depth and zero mix leaves a signal completely unchanged (static allpass, no throb, no wet blend)");
        {
            UniVibeProcessor vibe;
            // Params pinned before prepare() snaps them: a post-prepare
            // setState rides the ~10 ms param ramp, so the first samples
            // still carry throb and wet blend while the knobs settle.
            auto state = vibe.getState();
            state->setAttribute ("univibe_depth", 0.0);
            state->setAttribute ("univibe_mix", 0.0);
            vibe.setState (*state);
            vibe.prepare (48000.0, 512, 1);

            juce::Random random (666768);
            juce::AudioBuffer<float> buffer (1, 2048);
            juce::AudioBuffer<float> original (1, 2048);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                buffer.setSample (0, i, v);
                original.setSample (0, i, v);
            }

            vibe.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), original.getSample (0, i), 1.0e-5f);
        }

        beginTest ("depth > 0 visibly modulates a fixed-frequency tone's level over time; depth = 0 barely changes it");
        {
            constexpr double sampleRate = 48000.0;
            constexpr double toneHz = 400.0; // inside the 150-900Hz sweep range
            constexpr int numWindows = 8;
            constexpr int windowSamples = 1000;

            UniVibeProcessor staticVibe;
            staticVibe.prepare (sampleRate, 512, 1);
            auto staticState = staticVibe.getState();
            staticState->setAttribute ("univibe_depth", 0.0);
            staticVibe.setState (*staticState);

            UniVibeProcessor modulatingVibe;
            modulatingVibe.prepare (sampleRate, 512, 1);
            auto modState = modulatingVibe.getState();
            modState->setAttribute ("univibe_depth", 1.0);
            modState->setAttribute ("univibe_rate", 5.0);
            modulatingVibe.setState (*modState);

            const auto staticEnergies = windowEnergies (staticVibe, sampleRate, toneHz, numWindows, windowSamples);
            const auto modEnergies = windowEnergies (modulatingVibe, sampleRate, toneHz, numWindows, windowSamples);

            expect (variance (modEnergies) > variance (staticEnergies) * 4.0f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            UniVibeProcessor vibe;
            vibe.prepare (48000.0, 512, 2);

            juce::Random random (697071);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                vibe.process (buffer);

                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float sample = buffer.getSample (ch, i);
                        expect (std::isfinite (sample));
                        expect (std::abs (sample) < 10.0f);
                    }
            }
        }
    }
};

static UniVibeProcessorTests uniVibeProcessorTests;

} // namespace openguitarmultifx
