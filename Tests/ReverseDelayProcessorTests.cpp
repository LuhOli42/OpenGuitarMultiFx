#include "Effects/ReverseDelayProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class ReverseDelayProcessorTests : public juce::UnitTest
{
public:
    ReverseDelayProcessorTests() : juce::UnitTest ("ReverseDelayProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("the second chunk plays the first chunk's audio back in reverse");
        {
            ReverseDelayProcessor reverseDelay;

            // Pin down exact params BEFORE prepare() -- chunk length is
            // only re-sampled at a chunk boundary (see the class doc
            // comment), so setting Time after prepare() wouldn't shrink
            // the chunk prepare() already sized from the default 500ms
            // until a boundary the test's short buffers would never
            // reach. 100ms chunk, no feedback (chunk 1's recording is
            // exactly the raw input, not tainted by its own -- silent, at
            // this point -- feedback), fully wet.
            auto state = reverseDelay.getState();
            state->setAttribute ("reversedelay_time", 100.0);
            state->setAttribute ("reversedelay_feedback", 0.0);
            state->setAttribute ("reversedelay_wet", 1.0);
            state->setAttribute ("reversedelay_kill_dry", 1.0);
            reverseDelay.setState (*state);

            reverseDelay.prepare (48000.0, 512, 1);

            const int chunkLength = (int) (0.1 * 48000.0);

            // Chunk 1: a ramp, so every sample position is distinguishable.
            juce::AudioBuffer<float> chunk1 (1, chunkLength);
            for (int i = 0; i < chunkLength; ++i)
                chunk1.setSample (0, i, (float) i / (float) chunkLength);
            reverseDelay.process (chunk1);

            // Chunk 2: silence in, so the output is purely the reversed
            // chunk 1 (mix=1.0, so nothing of this silent input shows up).
            juce::AudioBuffer<float> chunk2 (1, chunkLength);
            chunk2.clear();
            reverseDelay.process (chunk2);

            // First sample of chunk 2 should be chunk 1's LAST sample;
            // last sample of chunk 2 should be chunk 1's FIRST sample.
            expectWithinAbsoluteError (chunk2.getSample (0, 0), (float) (chunkLength - 1) / (float) chunkLength, 0.001f);
            expectWithinAbsoluteError (chunk2.getSample (0, chunkLength - 1), 0.0f, 0.001f);
        }

        beginTest ("during the very first chunk there's nothing to play back yet");
        {
            ReverseDelayProcessor reverseDelay;

            // Same param-ramp caveat as the test above: mix must be pinned
            // BEFORE prepare() snaps it -- a post-prepare write ramps the
            // dry blend down over ~10 ms, so early samples leak the input.
            auto state = reverseDelay.getState();
            state->setAttribute ("reversedelay_wet", 1.0);      // fully wet -- isolates the (silent) reverse tap
            state->setAttribute ("reversedelay_kill_dry", 1.0);
            reverseDelay.setState (*state);

            reverseDelay.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 256);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.8f);

            reverseDelay.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), 0.0f, 0.0001f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            ReverseDelayProcessor reverseDelay;
            reverseDelay.prepare (48000.0, 512, 2);

            juce::Random random (192021);
            juce::AudioBuffer<float> buffer (2, 512);

            // 200 blocks * 512 samples spans several chunk boundaries at
            // the default 500ms chunk length -- exercises the swap logic
            // repeatedly, not just once.
            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                reverseDelay.process (buffer);

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

static ReverseDelayProcessorTests reverseDelayProcessorTests;

} // namespace openguitarmultifx
