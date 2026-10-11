#include "Effects/VibratoProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class VibratoProcessorTests : public juce::UnitTest
{
public:
    VibratoProcessorTests() : juce::UnitTest ("VibratoProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("output is 100% wet -- an impulse produces no immediate dry pass-through, only the delayed echo");
        {
            VibratoProcessor vibrato;
            vibrato.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 500);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            vibrato.process (buffer);

            // Sample 0 itself must be ~0 (no dry blend), unlike Chorus
            // which would show a dry component immediately.
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.0f, 1.0e-6f);
        }

        beginTest ("at LFO phase zero, an impulse's echo lands at the centre delay time");
        {
            VibratoProcessor vibrato;
            auto state = vibrato.getState();
            // Depth must be zero here -- the LFO keeps advancing while the
            // impulse propagates through the delay line, so even a phase
            // starting at sin(0)=0 drifts the actual delay time by the
            // time the echo arrives if depth is nonzero (default 0.5). And
            // it must be pinned before prepare() snaps it: a post-prepare
            // write rides the ~10 ms param ramp, so the echo lands while
            // depth is still sweeping and the peak index drifts.
            state->setAttribute ("vibrato_depth", 0.0);
            vibrato.setState (*state);
            vibrato.prepare (48000.0, 512, 1);

            const int centreSamples = (int) (0.006 * 48000.0); // 6ms centre delay
            juce::AudioBuffer<float> buffer (1, centreSamples + 200);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            vibrato.process (buffer);

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
            VibratoProcessor vibrato;
            vibrato.prepare (48000.0, 512, 1);
            auto state = vibrato.getState();
            state->setAttribute ("vibrato_depth", 1.0); // max +/-5ms sweep
            state->setAttribute ("vibrato_rate", 1.0);  // one full cycle every 48000 samples
            vibrato.setState (*state);

            juce::AudioBuffer<float> silence (1, 12000); // quarter period
            silence.clear();
            vibrato.process (silence);

            juce::AudioBuffer<float> buffer (1, 2000);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            vibrato.process (buffer);

            const int expectedSamples = (int) (0.011 * 48000.0); // 6ms + 5ms at the LFO's peak
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
            VibratoProcessor vibrato;
            vibrato.prepare (48000.0, 512, 2);

            juce::Random random (495051);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                vibrato.process (buffer);

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

static VibratoProcessorTests vibratoProcessorTests;

} // namespace openguitarmultifx
