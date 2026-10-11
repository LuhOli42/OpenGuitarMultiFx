#include "Effects/OctaverProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class OctaverProcessorTests : public juce::UnitTest
{
public:
    OctaverProcessorTests() : juce::UnitTest ("OctaverProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("zero level leaves the dry signal intact (just the fixed output headroom scale)");
        {
            OctaverProcessor octaver;
            // Level pinned before prepare() snaps it: a post-prepare write
            // rides the ~10 ms param ramp, so the first samples blend the
            // sub-octave layer in while level is still falling to zero.
            auto state = octaver.getState();
            state->setAttribute ("octaver_level", 0.0);
            octaver.setState (*state);
            octaver.prepare (48000.0, 512, 1);

            juce::Random random (818283);
            juce::AudioBuffer<float> buffer (1, 2048);
            juce::AudioBuffer<float> original (1, 2048);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                buffer.setSample (0, i, v);
                original.setSample (0, i, v);
            }

            octaver.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), original.getSample (0, i) * 0.7f, 1.0e-5f);
        }

        beginTest ("a nonzero level measurably changes the output from the pure-dry case (the sub-octave layer is actually mixed in)");
        {
            OctaverProcessor dryOnly;
            dryOnly.prepare (48000.0, 512, 1);
            auto dryState = dryOnly.getState();
            dryState->setAttribute ("octaver_level", 0.0);
            dryOnly.setState (*dryState);

            OctaverProcessor withOctave;
            withOctave.prepare (48000.0, 512, 1);
            auto octaveState = withOctave.getState();
            octaveState->setAttribute ("octaver_level", 1.0);
            withOctave.setState (*octaveState);

            juce::Random random (848586);
            juce::AudioBuffer<float> dryBuffer (1, 4096);
            juce::AudioBuffer<float> octaveBuffer (1, 4096);
            for (int i = 0; i < dryBuffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                dryBuffer.setSample (0, i, v);
                octaveBuffer.setSample (0, i, v);
            }

            dryOnly.process (dryBuffer);
            withOctave.process (octaveBuffer);

            float totalDiff = 0.0f;
            for (int i = 0; i < dryBuffer.getNumSamples(); ++i)
                totalDiff += std::abs (octaveBuffer.getSample (0, i) - dryBuffer.getSample (0, i));

            expect (totalDiff > 1.0f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            OctaverProcessor octaver;
            octaver.prepare (48000.0, 512, 2);
            auto state = octaver.getState();
            state->setAttribute ("octaver_level", 1.0);
            octaver.setState (*state);

            juce::Random random (879091);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                octaver.process (buffer);

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

static OctaverProcessorTests octaverProcessorTests;

} // namespace openguitarmultifx
