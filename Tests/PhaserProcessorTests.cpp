#include "Effects/Modulation/Phaser/PhaserProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PhaserProcessorTests : public juce::UnitTest
{
public:
    PhaserProcessorTests() : juce::UnitTest ("PhaserProcessor", "Effects") {}

    static std::vector<float> windowEnergies (PhaserProcessor& phaser, double sampleRate, double toneHz,
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
            phaser.process (buffer);

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
        beginTest ("zero mix leaves the signal completely unchanged");
        {
            PhaserProcessor phaser;
            // Mix pinned before prepare() snaps it: a post-prepare write
            // rides the ~10 ms param ramp, so the first samples blend the
            // wet path in while mix is still falling to zero.
            auto state = phaser.getState();
            state->setAttribute ("phaser_mix", 0.0);
            phaser.setState (*state);
            phaser.prepare (48000.0, 512, 1);

            juce::Random random (565758);
            juce::AudioBuffer<float> buffer (1, 2048);
            juce::AudioBuffer<float> original (1, 2048);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                buffer.setSample (0, i, v);
                original.setSample (0, i, v);
            }

            phaser.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), original.getSample (0, i), 1.0e-5f);
        }

        beginTest ("with the notch sweeping (depth > 0), a fixed-frequency tone's level visibly rises and falls over time; a static notch (depth = 0) barely changes it");
        {
            constexpr double sampleRate = 48000.0;
            constexpr double toneHz = 600.0; // inside the 200-2000Hz sweep range
            constexpr int numWindows = 8;
            constexpr int windowSamples = 1000;

            PhaserProcessor staticPhaser;
            staticPhaser.prepare (sampleRate, 512, 1);
            auto staticState = staticPhaser.getState();
            staticState->setAttribute ("phaser_depth", 0.0);
            staticState->setAttribute ("phaser_mix", 0.5);
            staticPhaser.setState (*staticState);

            PhaserProcessor sweepingPhaser;
            sweepingPhaser.prepare (sampleRate, 512, 1);
            auto sweepState = sweepingPhaser.getState();
            sweepState->setAttribute ("phaser_depth", 1.0);
            sweepState->setAttribute ("phaser_rate", 5.0); // fast enough to see a full sweep in this short test
            sweepState->setAttribute ("phaser_mix", 0.5);
            sweepingPhaser.setState (*sweepState);

            const auto staticEnergies = windowEnergies (staticPhaser, sampleRate, toneHz, numWindows, windowSamples);
            const auto sweepingEnergies = windowEnergies (sweepingPhaser, sampleRate, toneHz, numWindows, windowSamples);

            expect (variance (sweepingEnergies) > variance (staticEnergies) * 4.0f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            PhaserProcessor phaser;
            phaser.prepare (48000.0, 512, 2);
            auto state = phaser.getState();
            state->setAttribute ("phaser_feedback", 0.9); // worst case
            phaser.setState (*state);

            juce::Random random (596061);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                phaser.process (buffer);

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

static PhaserProcessorTests phaserProcessorTests;

} // namespace openguitarmultifx
