#include "Effects/DualDelayProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class DualDelayProcessorTests : public juce::UnitTest
{
public:
    DualDelayProcessorTests() : juce::UnitTest ("DualDelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("an impulse produces two distinct echoes, one near each tap's time");
        {
            DualDelayProcessor delay;
            delay.prepare (48000.0, 512, 1);
            auto state = delay.getState();
            state->setAttribute ("dualdelay_timeA", 100.0);
            state->setAttribute ("dualdelay_timeB", 300.0);
            state->setAttribute ("dualdelay_feedback", 0.0); // isolate the two taps, no repeats
            state->setAttribute ("dualdelay_wet", 1.0);
            state->setAttribute ("dualdelay_kill_dry", 1.0);
            delay.setState (*state);

            const int samplesA = (int) (0.100 * 48000.0);
            const int samplesB = (int) (0.300 * 48000.0);

            juce::AudioBuffer<float> buffer (1, samplesB + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            delay.process (buffer);

            auto peakNear = [&] (int target)
            {
                float peak = 0.0f;
                int peakIndex = 0;
                for (int i = juce::jmax (0, target - 50); i < juce::jmin (buffer.getNumSamples(), target + 50); ++i)
                {
                    const float mag = std::abs (buffer.getSample (0, i));
                    if (mag > peak) { peak = mag; peakIndex = i; }
                }
                return std::make_pair (peak, peakIndex);
            };

            const auto [peakA, indexA] = peakNear (samplesA);
            const auto [peakB, indexB] = peakNear (samplesB);

            expectWithinAbsoluteError (indexA, samplesA, 4);
            expectWithinAbsoluteError (indexB, samplesB, 4);
            expect (peakA > 0.01f);
            expect (peakB > 0.01f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            DualDelayProcessor delay;
            delay.prepare (48000.0, 512, 2);

            juce::Random random (98765);
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

static DualDelayProcessorTests dualDelayProcessorTests;

} // namespace openguitarmultifx
