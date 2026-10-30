#include "Effects/GatedReverbProcessor.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class GatedReverbProcessorTests : public juce::UnitTest
{
public:
    GatedReverbProcessorTests() : juce::UnitTest ("GatedReverbProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("the tail is audible right after the transient, then cuts off hard well before a natural decay would finish");
        {
            GatedReverbProcessor gated;
            gated.prepare (48000.0, 512, 1);
            auto state = gated.getState();
            state->setAttribute ("gatedreverb_decay", 1.0); // longest possible natural tail
            state->setAttribute ("gatedreverb_hold", 10.0); // shortest hold, so the cut happens soon
            state->setAttribute ("gatedreverb_wet", 1.0);
            state->setAttribute ("gatedreverb_kill_dry", 1.0);
            gated.setState (*state);

            juce::AudioBuffer<float> impulse (1, 512);
            impulse.clear();
            impulse.setSample (0, 0, 1.0f);
            gated.process (impulse);

            float earlyEnergy = 0.0f;
            for (int i = 0; i < impulse.getNumSamples(); ++i)
                earlyEnergy += std::abs (impulse.getSample (0, i));

            expect (earlyEnergy > 0.001f);

            // Run well past the detector's own release + hold + the gate
            // smoother's release (roughly 20ms + 10ms + ~110ms to reach a
            // 1e-4 gain floor at a 12ms release time constant) with
            // silence, so the gate should have slammed shut long before
            // this point -- a natural (ungated) reverb at decay=1.0 would
            // still be audibly ringing here.
            float lateEnergy = 0.0f;
            juce::AudioBuffer<float> tail (1, 512);
            for (int block = 0; block < 30; ++block)
            {
                tail.clear();
                gated.process (tail);
                if (block >= 25)
                    for (int i = 0; i < tail.getNumSamples(); ++i)
                        lateEnergy += std::abs (tail.getSample (0, i));
            }

            expect (lateEnergy < 1.0e-4f);
        }

        beginTest ("a longer hold keeps the gate open, and thus the tail audible, longer than a short hold");
        {
            GatedReverbProcessor shortHold;
            shortHold.prepare (48000.0, 512, 1);
            auto shortState = shortHold.getState();
            shortState->setAttribute ("gatedreverb_hold", 10.0);
            shortState->setAttribute ("gatedreverb_wet", 1.0);
            shortState->setAttribute ("gatedreverb_kill_dry", 1.0);
            shortHold.setState (*shortState);

            GatedReverbProcessor longHold;
            longHold.prepare (48000.0, 512, 1);
            auto longState = longHold.getState();
            longState->setAttribute ("gatedreverb_hold", 400.0);
            longState->setAttribute ("gatedreverb_wet", 1.0);
            longState->setAttribute ("gatedreverb_kill_dry", 1.0);
            longHold.setState (*longState);

            juce::AudioBuffer<float> shortImpulse (1, 512);
            shortImpulse.clear();
            shortImpulse.setSample (0, 0, 1.0f);
            shortHold.process (shortImpulse);

            juce::AudioBuffer<float> longImpulse (1, 512);
            longImpulse.clear();
            longImpulse.setSample (0, 0, 1.0f);
            longHold.process (longImpulse);

            // A window that's well past the short hold's gate closing but
            // still within the long hold's open window (10 blocks * ~10.7ms
            // ~= 107ms, between 10ms and 400ms).
            float shortEnergy = 0.0f;
            float longEnergy = 0.0f;
            juce::AudioBuffer<float> shortTail (1, 512);
            juce::AudioBuffer<float> longTail (1, 512);
            for (int block = 0; block < 10; ++block)
            {
                shortTail.clear();
                shortHold.process (shortTail);
                longTail.clear();
                longHold.process (longTail);

                if (block == 9)
                {
                    for (int i = 0; i < shortTail.getNumSamples(); ++i)
                        shortEnergy += std::abs (shortTail.getSample (0, i));
                    for (int i = 0; i < longTail.getNumSamples(); ++i)
                        longEnergy += std::abs (longTail.getSample (0, i));
                }
            }

            expect (longEnergy > shortEnergy);
        }

        beginTest ("repeated blocks of a bounded signal never produce NaN/Inf or runaway output");
        {
            GatedReverbProcessor gated;
            gated.prepare (48000.0, 512, 2);

            juce::Random random (373839);
            juce::AudioBuffer<float> buffer (2, 512);

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                gated.process (buffer);

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

static GatedReverbProcessorTests gatedReverbProcessorTests;

} // namespace openguitarmultifx
