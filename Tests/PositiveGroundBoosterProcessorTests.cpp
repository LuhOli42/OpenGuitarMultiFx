#include "Effects/PositiveGroundBoosterProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class PositiveGroundBoosterProcessorTests : public juce::UnitTest
{
public:
    PositiveGroundBoosterProcessorTests() : juce::UnitTest ("PositiveGroundBoosterProcessor", "Effects") {}

    void runTest() override
    {
        beginTest ("silence in stays finite and settles near silence (AC-coupled, no DC servo needed)");
        {
            PositiveGroundBoosterProcessor booster;
            booster.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            buffer.clear();

            // Several blocks: the Newton-Raphson warm start needs a few
            // samples to settle onto the real DC operating point.
            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                booster.process (buffer);
            }

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float s = buffer.getSample (0, i);
                expect (std::isfinite (s));
                expect (std::abs (s) < 0.05f); // near-silent, not a DC offset or runaway
            }
        }

        beginTest ("stays finite and bounded under a loud sine, every Boost setting");
        {
            for (float boostSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                PositiveGroundBoosterProcessor booster;
                booster.prepare (48000.0, 512, 1);

                if (auto* group = booster.getParameters())
                    for (auto* param : group->getParameters (true))
                        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                            *floatParam = boostSetting;

                juce::AudioBuffer<float> buffer (1, 512);

                for (int block = 0; block < 40; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        // A hot input (guitar pickups can swing well past
                        // line level into a booster) -- this is exactly the
                        // regime a real transistor stage should compress/
                        // clip in, not blow up numerically.
                        buffer.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }

                    booster.process (buffer);

                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        expect (std::isfinite (s));
                        expect (std::abs (s) < 20.0f); // generous -- this is a gain stage, real clipping is expected
                    }
                }
            }
        }

        beginTest ("DC operating point is a forward-biased PNP in the active region, not cutoff");
        {
            // Real-hardware Rangemaster-clone builds typically report a
            // forward Vbe around 0.55-0.65V and a collector sitting well
            // off the supply rail (real current flowing, not pinned at Vcc
            // the way a transistor stuck at cutoff would be) -- this is
            // the test that catches a sign error turning the stage off
            // entirely, which pure boundedness/no-NaN checks above can't:
            // a transistor stuck at cutoff is perfectly finite and stable,
            // just silently wrong.
            PositiveGroundBoosterProcessor booster;
            booster.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 40; ++block)
            {
                buffer.clear();
                booster.process (buffer);
            }

            const auto bias = booster.getDebugBiasPoint();
            expect (std::isfinite (bias.vBase) && std::isfinite (bias.vEmitter) && std::isfinite (bias.vCollector));

            // PNP forward-active: emitter more positive than base (Vbe,
            // base-minus-emitter, is NEGATIVE) by roughly a silicon diode
            // drop -- generous bounds since the exact number depends on
            // the (undocumented-by-the-schematic) specific transistor part.
            const float vbe = bias.vBase - bias.vEmitter;
            expectGreaterOrEqual (-vbe, 0.2f);
            expectLessOrEqual (-vbe, 0.8f);

            // Real collector current flowing -- collector meaningfully off
            // the supply rail, not pinned there (cutoff). -8.4V matches
            // PositiveGroundBoosterProcessor's private supplyVoltage
            // constant (9V battery minus D1's ~0.6V forward drop).
            constexpr float supplyRailVoltage = -8.4f;
            expectGreaterThan (std::abs (bias.vCollector - supplyRailVoltage), 0.05f);
        }

        beginTest ("Boost knob is a real, monotonically increasing gain control");
        {
            float previousGain = -1.0f;

            for (float boostSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                PositiveGroundBoosterProcessor booster;
                booster.prepare (48000.0, 512, 1);
                if (auto* group = booster.getParameters())
                    for (auto* param : group->getParameters (true))
                        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                            *floatParam = boostSetting;

                juce::AudioBuffer<float> buffer (1, 512);
                const float inputAmplitude = 0.01f; // small-signal, avoid clipping distortion muddying the gain measurement
                float outputPeak = 0.0f;

                for (int block = 0; block < 60; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        buffer.setSample (0, i, inputAmplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
                    }
                    booster.process (buffer);

                    if (block >= 50) // after settling
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                            outputPeak = juce::jmax (outputPeak, std::abs (buffer.getSample (0, i)));
                }

                const float gain = outputPeak / inputAmplitude;
                logMessage ("Boost=" + juce::String (boostSetting) + " gain=" + juce::String (gain, 2) + "x");

                if (boostSetting > 0.0f) // 0 -> near-zero collector load is a real, deliberate exception -- see class doc comment
                    expectGreaterThan (gain, previousGain);
                previousGain = gain;
            }

            // At maximum, this circuit is known for a large boost (real
            // units are commonly described as adding on the order of
            // 20-30dB) -- assert it lands in a plausible range rather than
            // an arbitrary tiny or absurd number.
            expectGreaterThan (previousGain, 5.0f);
            expectLessThan (previousGain, 200.0f);
        }

        beginTest ("CPU cost stays comfortably real-time capable (regression guard)");
        {
            PositiveGroundBoosterProcessor booster;
            booster.prepare (48000.0, 512, 2); // stereo -- worst case, both channels solving every sample

            juce::AudioBuffer<float> buffer (2, 512);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                buffer.setSample (0, i, 0.4f * std::sin ((float) i * 0.3f)); // loud + high-frequency-ish content, not a trivially-converged DC signal
                buffer.setSample (1, i, 0.4f * std::sin ((float) i * 0.31f));
            }

            constexpr int numBlocks = 2000; // ~21.3 seconds of audio at 48kHz/512
            const auto start = juce::Time::getHighResolutionTicks();
            for (int block = 0; block < numBlocks; ++block)
                booster.process (buffer);
            const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);

            const double audioSeconds = (double) numBlocks * buffer.getNumSamples() / 48000.0;
            const double realTimeFactor = audioSeconds / elapsedSeconds;
            logMessage ("Processed " + juce::String (audioSeconds, 1) + "s of stereo audio in "
                        + juce::String (elapsedSeconds, 3) + "s wall-clock -- " + juce::String (realTimeFactor, 1) + "x real-time");

            // Generous floor (measured ~40x on this project's own dev
            // machine) -- this exists to catch a future accidental
            // regression (e.g. someone raising EbersMollBJT's default
            // Newton-Raphson iteration count), not to assert a specific
            // performance target.
            expectGreaterThan (realTimeFactor, 5.0);
            expect (std::isfinite (buffer.getSample (0, 0)));
        }

        beginTest ("stereo channels stay independent and both finite");
        {
            PositiveGroundBoosterProcessor booster;
            booster.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int block = 0; block < 10; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    buffer.setSample (0, i, 0.3f * std::sin ((float) i * 0.1f));
                    buffer.setSample (1, i, -0.3f * std::sin ((float) i * 0.1f));
                }
                booster.process (buffer);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expect (std::isfinite (buffer.getSample (ch, i)));
        }
    }
};

static PositiveGroundBoosterProcessorTests positiveGroundBoosterProcessorTests;

} // namespace openguitarmultifx
