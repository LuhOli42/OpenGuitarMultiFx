#include "Effects/DelayProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class DelayProcessorTests : public juce::UnitTest
{
public:
    DelayProcessorTests() : juce::UnitTest ("DelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("an impulse reappears in the output near the configured delay time");
        {
            DelayProcessor delay;
            delay.prepare (48000.0, 512, 1);

            // delay_time defaults to 350ms; delay_mix defaults to 0.35.
            const int delaySamples = (int) (0.35 * 48000.0);
            juce::AudioBuffer<float> buffer (1, delaySamples + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            delay.process (buffer);

            float peak = 0.0f;
            int peakIndex = 0;
            for (int i = 100; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::abs (buffer.getSample (0, i));
                if (mag > peak)
                {
                    peak = mag;
                    peakIndex = i;
                }
            }

            expectWithinAbsoluteError (peakIndex, delaySamples, 4);
            expectWithinAbsoluteError (peak, 0.54f, 0.05f); // the default Wet: the echo rides on top of a unity dry
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            DelayProcessor delay;
            delay.prepare (48000.0, 512, 2);

            juce::Random random (1234);
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

static DelayProcessorTests delayProcessorTests;

} // namespace openguitarmultifx
