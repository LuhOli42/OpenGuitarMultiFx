#include "Effects/AnalogDelayProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class AnalogDelayProcessorTests : public juce::UnitTest
{
public:
    AnalogDelayProcessorTests() : juce::UnitTest ("AnalogDelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("an impulse reappears in the output near the configured delay time");
        {
            AnalogDelayProcessor delay;
            delay.prepare (48000.0, 512, 1);

            // analogdelay_time defaults to 350ms.
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
            expect (peak > 0.01f);
        }

        beginTest ("lower tone darkens successive repeats more than higher tone (BBD-style filtering)");
        {
            // Feed a bright impulse, run long enough to collect several
            // feedback repeats, and compare each repeat's high-frequency
            // content (approximated by sample-to-sample difference energy,
            // a cheap proxy for brightness) between a dark and a bright
            // setting.
            AnalogDelayProcessor darkDelay;
            darkDelay.prepare (48000.0, 512, 1);
            auto darkState = darkDelay.getState();
            darkState->setAttribute ("analogdelay_tone", 0.05);
            darkState->setAttribute ("analogdelay_feedback", 0.8);
            darkState->setAttribute ("analogdelay_wet", 1.0);
            darkState->setAttribute ("analogdelay_kill_dry", 1.0);
            darkDelay.setState (*darkState);

            AnalogDelayProcessor brightDelay;
            brightDelay.prepare (48000.0, 512, 1);
            auto brightState = brightDelay.getState();
            brightState->setAttribute ("analogdelay_tone", 1.0);
            brightState->setAttribute ("analogdelay_feedback", 0.8);
            brightState->setAttribute ("analogdelay_wet", 1.0);
            brightState->setAttribute ("analogdelay_kill_dry", 1.0);
            brightDelay.setState (*brightState);

            const int totalSamples = (int) (48000.0 * 1.5);
            juce::AudioBuffer<float> darkBuffer (1, totalSamples);
            juce::AudioBuffer<float> brightBuffer (1, totalSamples);
            darkBuffer.clear();
            brightBuffer.clear();
            darkBuffer.setSample (0, 0, 1.0f);
            brightBuffer.setSample (0, 0, 1.0f);

            darkDelay.process (darkBuffer);
            brightDelay.process (brightBuffer);

            // Skip the first repeat (dominated by the un-filtered dry
            // impulse's own harmonics) and measure later in the tail,
            // where repeated filtering should have visibly separated them.
            double darkDiffEnergy = 0.0;
            double brightDiffEnergy = 0.0;
            for (int i = (int) (48000.0 * 1.0); i < totalSamples; ++i)
            {
                const float darkDiff = darkBuffer.getSample (0, i) - darkBuffer.getSample (0, i - 1);
                const float brightDiff = brightBuffer.getSample (0, i) - brightBuffer.getSample (0, i - 1);
                darkDiffEnergy += (double) (darkDiff * darkDiff);
                brightDiffEnergy += (double) (brightDiff * brightDiff);
            }

            expect (brightDiffEnergy > darkDiffEnergy);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            AnalogDelayProcessor delay;
            delay.prepare (48000.0, 512, 2);

            juce::Random random (54321);
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

static AnalogDelayProcessorTests analogDelayProcessorTests;

} // namespace openguitarmultifx
