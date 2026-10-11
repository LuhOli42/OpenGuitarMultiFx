#include "Effects/TremoloProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class TremoloProcessorTests : public juce::UnitTest
{
public:
    TremoloProcessorTests() : juce::UnitTest ("TremoloProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("zero depth leaves a constant signal completely untouched");
        {
            TremoloProcessor tremolo;
            // Params pinned before prepare() snaps them: a post-prepare
            // setState rides the ~10 ms paramRampSeconds ramp, so the first
            // samples measure the depth ramping down, not the resting unit.
            auto state = tremolo.getState();
            state->setAttribute ("tremolo_depth", 0.0);
            tremolo.setState (*state);
            tremolo.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 4800);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.8f);

            tremolo.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), 0.8f, 1.0e-5f);
        }

        beginTest ("full depth oscillates a constant signal between roughly 0 and its original level");
        {
            TremoloProcessor tremolo;
            tremolo.prepare (48000.0, 512, 1);
            auto state = tremolo.getState();
            state->setAttribute ("tremolo_depth", 1.0);
            state->setAttribute ("tremolo_rate", 4.0); // one full cycle every 12000 samples @ 48kHz
            tremolo.setState (*state);

            juce::AudioBuffer<float> buffer (1, 12000); // a full LFO period
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 1.0f);

            tremolo.process (buffer);

            float minVal = 10.0f, maxVal = -10.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                minVal = juce::jmin (minVal, buffer.getSample (0, i));
                maxVal = juce::jmax (maxVal, buffer.getSample (0, i));
            }

            expectWithinAbsoluteError (maxVal, 1.0f, 0.01f);
            expectWithinAbsoluteError (minVal, 0.0f, 0.01f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            TremoloProcessor tremolo;
            tremolo.prepare (48000.0, 512, 2);

            juce::Random random (434445);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                tremolo.process (buffer);

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

static TremoloProcessorTests tremoloProcessorTests;

} // namespace openguitarmultifx
