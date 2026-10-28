#include "Effects/PlateReverbProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PlateReverbProcessorTests : public juce::UnitTest
{
public:
    PlateReverbProcessorTests() : juce::UnitTest ("PlateReverbProcessor", "Effects") {}

    static float tailEnergyAfterImpulse (PlateReverbProcessor& plate, int numChannels, int blocksToMeasure)
    {
        juce::AudioBuffer<float> dryBlock (numChannels, 512);
        dryBlock.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            dryBlock.setSample (ch, 0, 1.0f);
        plate.process (dryBlock);

        float energy = 0.0f;
        juce::AudioBuffer<float> tailBlock (numChannels, 512);
        for (int block = 0; block < blocksToMeasure; ++block)
        {
            tailBlock.clear();
            plate.process (tailBlock);
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < tailBlock.getNumSamples(); ++i)
                    energy += std::abs (tailBlock.getSample (ch, i));
        }
        return energy;
    }

    void runTest() override
    {
        beginTest ("an impulse leaves a ringing tail, not silence, after the dry sample (mono)");
        {
            PlateReverbProcessor plate;
            plate.prepare (48000.0, 512, 1);

            const float energy = tailEnergyAfterImpulse (plate, 1, 4);
            expect (energy > 0.001f);
        }

        beginTest ("a mono impulse cross-feeds into the other channel (the tank is coupled, not two independent reverbs)");
        {
            PlateReverbProcessor plate;
            plate.prepare (48000.0, 512, 2);
            auto state = plate.getState();
            state->setAttribute ("plate_decay", 0.8);
            state->setAttribute ("plate_wet", 1.0);
            state->setAttribute ("plate_kill_dry", 1.0);
            plate.setState (*state);

            juce::AudioBuffer<float> buffer (2, 512);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f); // left only
            plate.process (buffer);

            float rightEnergy = 0.0f;
            juce::AudioBuffer<float> tail (2, 512);
            for (int block = 0; block < 6; ++block)
            {
                tail.clear();
                plate.process (tail);
                for (int i = 0; i < tail.getNumSamples(); ++i)
                    rightEnergy += std::abs (tail.getSample (1, i));
            }

            expect (rightEnergy > 0.001f);
        }

        beginTest ("higher decay produces a longer/louder tail than lower decay");
        {
            PlateReverbProcessor lowDecayPlate;
            lowDecayPlate.prepare (48000.0, 512, 1);
            auto lowState = lowDecayPlate.getState();
            lowState->setAttribute ("plate_decay", 0.05);
            lowDecayPlate.setState (*lowState);

            PlateReverbProcessor highDecayPlate;
            highDecayPlate.prepare (48000.0, 512, 1);
            auto highState = highDecayPlate.getState();
            highState->setAttribute ("plate_decay", 0.95);
            highDecayPlate.setState (*highState);

            const float lowEnergy = tailEnergyAfterImpulse (lowDecayPlate, 1, 10);
            const float highEnergy = tailEnergyAfterImpulse (highDecayPlate, 1, 10);

            expect (highEnergy > lowEnergy);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            PlateReverbProcessor plate;
            plate.prepare (48000.0, 512, 2);

            juce::Random random (313233);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                plate.process (buffer);

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

static PlateReverbProcessorTests plateReverbProcessorTests;

} // namespace openguitarmultifx
