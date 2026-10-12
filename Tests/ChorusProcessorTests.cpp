#include "Effects/Modulation/Chorus/ChorusProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class ChorusProcessorTests : public juce::UnitTest
{
public:
    ChorusProcessorTests() : juce::UnitTest ("ChorusProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("at LFO phase zero, an impulse's echo lands at the centre delay time");
        {
            ChorusProcessor chorus;
            chorus.prepare (48000.0, 512, 1);
            auto state = chorus.getState();
            state->setAttribute ("chorus_mix", 1.0);
            // Depth must be zero here, not just "phase starts at zero" --
            // the LFO keeps advancing while the impulse propagates through
            // the delay line, so a nonzero depth drifts the actual delay
            // time by the time the echo arrives even though it started at
            // sin(0)=0. This test isolates the base delay time; the next
            // test covers the sweep itself.
            state->setAttribute ("chorus_depth", 0.0);
            chorus.setState (*state);

            const int centreSamples = (int) (0.015 * 48000.0); // 15ms centre delay
            juce::AudioBuffer<float> buffer (1, centreSamples + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            chorus.process (buffer);

            float peak = 0.0f;
            int peakIndex = 0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::abs (buffer.getSample (0, i));
                if (mag > peak) { peak = mag; peakIndex = i; }
            }

            expectWithinAbsoluteError (peakIndex, centreSamples, 4);
        }

        beginTest ("after the LFO has swept a quarter cycle, the delay time has actually moved");
        {
            ChorusProcessor chorus;
            chorus.prepare (48000.0, 512, 1);
            auto state = chorus.getState();
            state->setAttribute ("chorus_mix", 1.0);
            state->setAttribute ("chorus_depth", 1.0); // max +/-8ms sweep
            state->setAttribute ("chorus_rate", 1.0);  // one full cycle every 48000 samples
            chorus.setState (*state);

            // Run silence for a quarter period (12000 samples @ 1Hz/48kHz)
            // so the LFO reaches its peak (sin(pi/2) = 1 -> delay = centre + depth).
            juce::AudioBuffer<float> silence (1, 12000);
            silence.clear();
            chorus.process (silence);

            juce::AudioBuffer<float> buffer (1, 2000);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            chorus.process (buffer);

            const int expectedSamples = (int) (0.023 * 48000.0); // 15ms + 8ms at the LFO's peak
            float peak = 0.0f;
            int peakIndex = 0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::abs (buffer.getSample (0, i));
                if (mag > peak) { peak = mag; peakIndex = i; }
            }

            expectWithinAbsoluteError (peakIndex, expectedSamples, 10);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            ChorusProcessor chorus;
            chorus.prepare (48000.0, 512, 2);

            juce::Random random (464748);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                chorus.process (buffer);

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

static ChorusProcessorTests chorusProcessorTests;

} // namespace openguitarmultifx
