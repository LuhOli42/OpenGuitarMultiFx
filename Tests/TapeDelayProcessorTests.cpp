#include "Effects/TapeDelayProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class TapeDelayProcessorTests : public juce::UnitTest
{
public:
    TapeDelayProcessorTests() : juce::UnitTest ("TapeDelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("an impulse reappears in the output near the configured delay time");
        {
            TapeDelayProcessor delay;
            delay.prepare (48000.0, 512, 1);

            // tapedelay_time defaults to 350ms; wow/flutter only wobbles it
            // by +/-1.2ms, so the repeat still lands close to the nominal time.
            const int delaySamples = (int) (0.35 * 48000.0);
            juce::AudioBuffer<float> buffer (1, delaySamples + 300);
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

            expectWithinAbsoluteError (peakIndex, delaySamples, 80); // wow/flutter tolerance
            // Unlike DelayProcessor's clean line, the read position never
            // sits still -- the continuously wobbling read pointer smears a
            // single-sample impulse across several output samples via the
            // linear interpolation, so the peak reads measurably lower than
            // a plain wet-mix value (confirmed empirically, not a bug: real
            // tape wow smears transients the same way).
            expectWithinAbsoluteError (peak, 0.54f, 0.09f); // the default Wet
        }

        beginTest ("feedback saturation keeps repeats bounded even with a loud sustained input");
        {
            TapeDelayProcessor delay;
            delay.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 48000); // 1 second -- several repeats at the default 350ms time
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.9f);

            delay.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float sample = buffer.getSample (0, i);
                expect (std::isfinite (sample));
                expect (std::abs (sample) < 3.0f); // tanh() feedback can't run away, unlike a linear delay's could
            }
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            TapeDelayProcessor delay;
            delay.prepare (48000.0, 512, 2);

            juce::Random random (91011);
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

static TapeDelayProcessorTests tapeDelayProcessorTests;

} // namespace openguitarmultifx
