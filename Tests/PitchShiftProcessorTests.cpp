#include "Effects/PitchShiftProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PitchShiftProcessorTests : public juce::UnitTest
{
public:
    PitchShiftProcessorTests() : juce::UnitTest ("PitchShiftProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("at zero semitones (ratio 1), the shifter degenerates to a fixed half-grain delay");
        {
            PitchShiftProcessor shift;
            shift.prepare (48000.0, 512, 1);
            auto state = shift.getState();
            state->setAttribute ("pitchshift_semitones", 0.0);
            state->setAttribute ("pitchshift_mix", 1.0);
            shift.setState (*state);

            const int expectedDelay = (int) (0.025 * 48000.0); // grain(50ms)/2
            juce::AudioBuffer<float> buffer (1, expectedDelay + 400);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            shift.process (buffer);

            float peak = 0.0f;
            int peakIndex = 0;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float mag = std::abs (buffer.getSample (0, i));
                if (mag > peak) { peak = mag; peakIndex = i; }
            }

            expectWithinAbsoluteError (peakIndex, expectedDelay, 4);
            expect (peak > 0.5f);
        }

        beginTest ("zero mix leaves the signal completely unchanged regardless of the shift amount");
        {
            PitchShiftProcessor shift;
            // Params pinned before prepare() snaps them: a post-prepare
            // setState rides the ~10 ms param ramp, so the first samples
            // blend the shifted signal in while mix is still falling.
            auto state = shift.getState();
            state->setAttribute ("pitchshift_semitones", 12.0);
            state->setAttribute ("pitchshift_mix", 0.0);
            shift.setState (*state);
            shift.prepare (48000.0, 512, 1);

            juce::Random random (757677);
            juce::AudioBuffer<float> buffer (1, 2048);
            juce::AudioBuffer<float> original (1, 2048);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                buffer.setSample (0, i, v);
                original.setSample (0, i, v);
            }

            shift.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), original.getSample (0, i), 1.0e-5f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output at extreme shifts");
        {
            PitchShiftProcessor shift;
            shift.prepare (48000.0, 512, 2);
            auto state = shift.getState();
            state->setAttribute ("pitchshift_semitones", -24.0);
            shift.setState (*state);

            juce::Random random (787980);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                shift.process (buffer);

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

static PitchShiftProcessorTests pitchShiftProcessorTests;

} // namespace openguitarmultifx
