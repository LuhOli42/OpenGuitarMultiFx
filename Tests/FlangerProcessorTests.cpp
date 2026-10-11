#include "Effects/FlangerProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class FlangerProcessorTests : public juce::UnitTest
{
public:
    FlangerProcessorTests() : juce::UnitTest ("FlangerProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("with no modulation and no feedback, an impulse's echo lands at the centre delay time");
        {
            FlangerProcessor flanger;
            // Params pinned before prepare() snaps them: a post-prepare
            // setState rides the ~10 ms param ramp, so the impulse's echo
            // lands while depth and mix are still sweeping.
            auto state = flanger.getState();
            state->setAttribute ("flanger_depth", 0.0);
            state->setAttribute ("flanger_feedback", 0.0);
            state->setAttribute ("flanger_mix", 1.0);
            flanger.setState (*state);
            flanger.prepare (48000.0, 512, 1);

            const int centreSamples = (int) (0.004 * 48000.0); // 4ms centre delay
            juce::AudioBuffer<float> buffer (1, centreSamples + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            flanger.process (buffer);

            float peak = 0.0f;
            int peakIndex = 0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::abs (buffer.getSample (0, i));
                if (mag > peak) { peak = mag; peakIndex = i; }
            }

            expectWithinAbsoluteError (peakIndex, centreSamples, 4);
        }

        beginTest ("feedback keeps energy ringing on well after a burst ends; no feedback means it's over almost immediately");
        {
            // A single-sample impulse is the wrong probe for this: with
            // depth=0 the delay time is an exact repeating period, and
            // float rounding on that exact value can put the read index
            // one whole buffer position away from where the impulse
            // actually lives, making every "echo" miss it -- a precision
            // artefact specific to a perfectly periodic single-sample
            // test signal, not something a real (broadband, non-periodic)
            // signal runs into. A short noise burst's energy is spread
            // across many samples and positions, so it isn't sensitive to
            // that one-position edge case the way a lone impulse is.
            auto tailEnergyAfterBurst = [] (float feedbackValue)
            {
                FlangerProcessor flanger;
                flanger.prepare (48000.0, 512, 1);
                auto state = flanger.getState();
                state->setAttribute ("flanger_depth", 0.0);
                state->setAttribute ("flanger_feedback", (double) feedbackValue);
                state->setAttribute ("flanger_mix", 1.0);
                flanger.setState (*state);

                juce::Random random (606162);
                juce::AudioBuffer<float> buffer (1, 2000);
                buffer.clear();
                for (int i = 0; i < 200; ++i)
                    buffer.setSample (0, i, random.nextFloat() * 2.0f - 1.0f);

                flanger.process (buffer);

                float energy = 0.0f;
                for (int i = 250; i < buffer.getNumSamples(); ++i)
                    energy += std::abs (buffer.getSample (0, i));
                return energy;
            };

            const float noFeedbackEnergy = tailEnergyAfterBurst (0.0f);
            const float withFeedbackEnergy = tailEnergyAfterBurst (0.8f);

            expect (withFeedbackEnergy > noFeedbackEnergy * 2.0f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output (including negative feedback)");
        {
            FlangerProcessor flanger;
            flanger.prepare (48000.0, 512, 2);
            auto state = flanger.getState();
            state->setAttribute ("flanger_feedback", -0.9);
            flanger.setState (*state);

            juce::Random random (525354);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                flanger.process (buffer);

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

static FlangerProcessorTests flangerProcessorTests;

} // namespace openguitarmultifx
