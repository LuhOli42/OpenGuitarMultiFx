#include "Effects/HallReverbProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class HallReverbProcessorTests : public juce::UnitTest
{
public:
    HallReverbProcessorTests() : juce::UnitTest ("HallReverbProcessor", "Effects") {}

    static float tailEnergyAfterImpulse (HallReverbProcessor& hall, int blocksToMeasure)
    {
        juce::AudioBuffer<float> dryBlock (1, 512);
        dryBlock.clear();
        dryBlock.setSample (0, 0, 1.0f);
        hall.process (dryBlock);

        float energy = 0.0f;
        juce::AudioBuffer<float> tailBlock (1, 512);
        for (int block = 0; block < blocksToMeasure; ++block)
        {
            tailBlock.clear();
            hall.process (tailBlock);
            for (int i = 0; i < tailBlock.getNumSamples(); ++i)
                energy += std::abs (tailBlock.getSample (0, i));
        }
        return energy;
    }

    void runTest() override
    {
        beginTest ("an impulse leaves a ringing tail, not silence, after the dry sample");
        {
            HallReverbProcessor hall;
            hall.prepare (48000.0, 512, 1);

            const float energy = tailEnergyAfterImpulse (hall, 4);
            expect (energy > 0.001f);
        }

        beginTest ("higher decay produces a longer/louder tail than lower decay");
        {
            HallReverbProcessor lowDecayHall;
            lowDecayHall.prepare (48000.0, 512, 1);
            auto lowState = lowDecayHall.getState();
            lowState->setAttribute ("hall_decay", 0.1);
            lowDecayHall.setState (*lowState);

            HallReverbProcessor highDecayHall;
            highDecayHall.prepare (48000.0, 512, 1);
            auto highState = highDecayHall.getState();
            highState->setAttribute ("hall_decay", 0.95);
            highDecayHall.setState (*highState);

            const float lowEnergy = tailEnergyAfterImpulse (lowDecayHall, 12);
            const float highEnergy = tailEnergyAfterImpulse (highDecayHall, 12);

            expect (highEnergy > lowEnergy);
        }

        beginTest ("left and right channels decorrelate (a hall isn't a mono echo panned centre)");
        {
            HallReverbProcessor hall;
            hall.prepare (48000.0, 512, 2);
            auto state = hall.getState();
            state->setAttribute ("hall_decay", 0.8);
            state->setAttribute ("hall_wet", 1.0);
            state->setAttribute ("hall_kill_dry", 1.0);
            hall.setState (*state);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);
            hall.process (buffer);

            bool anyDifference = false;
            for (int block = 0; block < 6 && ! anyDifference; ++block)
            {
                juce::AudioBuffer<float> tail (2, 512);
                tail.clear();
                hall.process (tail);
                for (int i = 0; i < tail.getNumSamples(); ++i)
                    if (std::abs (tail.getSample (0, i) - tail.getSample (1, i)) > 1.0e-6f)
                    {
                        anyDifference = true;
                        break;
                    }
            }
            expect (anyDifference);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            HallReverbProcessor hall;
            hall.prepare (48000.0, 512, 2);

            juce::Random random (151617);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                hall.process (buffer);

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

static HallReverbProcessorTests hallReverbProcessorTests;

} // namespace openguitarmultifx
