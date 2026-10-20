#include "Effects/Pitch/HarmonizerProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class HarmonizerProcessorTests : public juce::UnitTest
{
public:
    HarmonizerProcessorTests() : juce::UnitTest ("HarmonizerProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("zero level leaves the dry signal intact (just the fixed output headroom scale)");
        {
            HarmonizerProcessor harmonizer;
            harmonizer.prepare (48000.0, 512, 1);
            auto state = harmonizer.getState();
            state->setAttribute ("harmonizer_level", 0.0);
            harmonizer.setState (*state);

            juce::Random random (929394);
            juce::AudioBuffer<float> buffer (1, 2048);
            juce::AudioBuffer<float> original (1, 2048);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                buffer.setSample (0, i, v);
                original.setSample (0, i, v);
            }

            harmonizer.process (buffer);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                expectWithinAbsoluteError (buffer.getSample (0, i), original.getSample (0, i) * 0.7f, 1.0e-5f);
        }

        beginTest ("a nonzero level measurably changes the output from the pure-dry case (the harmony layer is actually mixed in)");
        {
            HarmonizerProcessor dryOnly;
            dryOnly.prepare (48000.0, 512, 1);
            auto dryState = dryOnly.getState();
            dryState->setAttribute ("harmonizer_level", 0.0);
            dryOnly.setState (*dryState);

            HarmonizerProcessor withHarmony;
            withHarmony.prepare (48000.0, 512, 1);
            auto harmonyState = withHarmony.getState();
            harmonyState->setAttribute ("harmonizer_level", 1.0);
            withHarmony.setState (*harmonyState);

            juce::Random random (959697);
            juce::AudioBuffer<float> dryBuffer (1, 4096);
            juce::AudioBuffer<float> harmonyBuffer (1, 4096);
            for (int i = 0; i < dryBuffer.getNumSamples(); ++i)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                dryBuffer.setSample (0, i, v);
                harmonyBuffer.setSample (0, i, v);
            }

            dryOnly.process (dryBuffer);
            withHarmony.process (harmonyBuffer);

            float totalDiff = 0.0f;
            for (int i = 0; i < dryBuffer.getNumSamples(); ++i)
                totalDiff += std::abs (harmonyBuffer.getSample (0, i) - dryBuffer.getSample (0, i));

            expect (totalDiff > 1.0f);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output at extreme intervals");
        {
            HarmonizerProcessor harmonizer;
            harmonizer.prepare (48000.0, 512, 2);
            auto state = harmonizer.getState();
            state->setAttribute ("harmonizer_semitones", -24.0);
            state->setAttribute ("harmonizer_level", 1.0);
            harmonizer.setState (*state);

            juce::Random random (989900);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                harmonizer.process (buffer);

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

static HarmonizerProcessorTests harmonizerProcessorTests;

} // namespace openguitarmultifx
