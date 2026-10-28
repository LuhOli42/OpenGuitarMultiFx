#include "Effects/MultiTapDelayProcessor.h"

#include <juce_core/juce_core.h>

#include <array>

namespace openguitarmultifx
{

class MultiTapDelayProcessorTests : public juce::UnitTest
{
public:
    MultiTapDelayProcessorTests() : juce::UnitTest ("MultiTapDelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("an impulse produces four echoes at 0.25x/0.5x/0.75x/1x the configured time");
        {
            MultiTapDelayProcessor delay;
            delay.prepare (48000.0, 512, 1);
            auto state = delay.getState();
            state->setAttribute ("multitap_time", 400.0);
            state->setAttribute ("multitap_feedback", 0.0); // isolate the four taps, no repeats
            state->setAttribute ("multitap_wet", 1.0);
            state->setAttribute ("multitap_kill_dry", 1.0);
            delay.setState (*state);

            const int baseSamples = (int) (0.400 * 48000.0);
            const std::array<float, 4> ratios { 0.25f, 0.5f, 0.75f, 1.0f };

            juce::AudioBuffer<float> buffer (1, baseSamples + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            delay.process (buffer);

            for (float ratio : ratios)
            {
                const int target = (int) ((float) baseSamples * ratio);
                float peak = 0.0f;
                int peakIndex = 0;
                for (int i = juce::jmax (0, target - 50); i < juce::jmin (buffer.getNumSamples(), target + 50); ++i)
                {
                    const float mag = std::abs (buffer.getSample (0, i));
                    if (mag > peak) { peak = mag; peakIndex = i; }
                }

                expectWithinAbsoluteError (peakIndex, target, 4);
                expect (peak > 0.01f);
            }
        }

        beginTest ("feedback re-triggers a second pass of all four taps; no feedback means silence once the first pass ends");
        {
            // Comparing feedback=0 against feedback=0.6 rather than
            // predicting an exact sample position for the second pass:
            // the parameter smoothing (SmoothedValue, 30ms ramp) makes
            // exact-position math for a second-order effect fragile to
            // get precisely right, but "is there anything at all here"
            // well past where the first pass has already finished is a
            // robust, position-independent way to prove feedback works.
            auto tailEnergyPastFirstPass = [] (float feedbackValue)
            {
                MultiTapDelayProcessor delay;
                delay.prepare (48000.0, 512, 1);
                auto state = delay.getState();
                state->setAttribute ("multitap_time", 50.0);
                state->setAttribute ("multitap_feedback", (double) feedbackValue);
                state->setAttribute ("multitap_wet", 1.0);
                state->setAttribute ("multitap_kill_dry", 1.0);
                delay.setState (*state);

                const int baseSamples = (int) (0.050 * 48000.0);
                juce::AudioBuffer<float> buffer (1, baseSamples * 2 + 400);
                buffer.clear();
                buffer.setSample (0, 0, 1.0f);
                delay.process (buffer);

                float energy = 0.0f;
                for (int i = baseSamples + 50; i < buffer.getNumSamples(); ++i)
                    energy += std::abs (buffer.getSample (0, i));
                return energy;
            };

            const float noFeedbackEnergy = tailEnergyPastFirstPass (0.0f);
            const float withFeedbackEnergy = tailEnergyPastFirstPass (0.6f);

            expectWithinAbsoluteError (noFeedbackEnergy, 0.0f, 1.0e-6f);
            expect (withFeedbackEnergy > noFeedbackEnergy + 1.0e-4f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            MultiTapDelayProcessor delay;
            delay.prepare (48000.0, 512, 2);

            juce::Random random (404142);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                delay.process (buffer);

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

static MultiTapDelayProcessorTests multiTapDelayProcessorTests;

} // namespace openguitarmultifx
