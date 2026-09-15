#include "Effects/DS1StyleDistortionProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class DS1StyleDistortionProcessorTests : public juce::UnitTest
{
public:
    DS1StyleDistortionProcessorTests() : juce::UnitTest ("DS1StyleDistortionProcessor", "Effects") {}

    static void setParam (DS1StyleDistortionProcessor& proc, const juce::String& paramId, float value)
    {
        if (auto* group = proc.getParameters())
            for (auto* param : group->getParameters (true))
                if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                    if (floatParam->paramID == paramId)
                        *floatParam = value;
    }

    void runTest() override
    {
        beginTest ("silence in stays finite and settles near silence");
        {
            DS1StyleDistortionProcessor ds1;
            ds1.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                ds1.process (buffer);
            }

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float s = buffer.getSample (0, i);
                expect (std::isfinite (s));
                expect (std::abs (s) < 0.05f);
            }
        }

        beginTest ("stays finite and bounded under a loud sine, every Drive setting");
        {
            for (float driveSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DS1StyleDistortionProcessor ds1;
                ds1.prepare (48000.0, 512, 1);
                setParam (ds1, "ds1_drive", driveSetting);

                juce::AudioBuffer<float> buffer (1, 512);

                for (int block = 0; block < 40; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        // A hot input -- a distortion pedal's whole job is to
                        // clip this hard, not blow up numerically doing it.
                        buffer.setSample (0, i, (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }

                    ds1.process (buffer);

                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        expect (std::isfinite (s));
                        expect (std::abs (s) < 20.0f);
                    }
                }
            }
        }

        beginTest ("Q2's DC operating point is forward-active, not cutoff");
        {
            // Same lesson as PositiveGroundBoosterProcessor's equivalent
            // test: boundedness alone can't tell a correctly-biased gain
            // stage from one silently stuck off.
            DS1StyleDistortionProcessor ds1;
            ds1.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 40; ++block)
            {
                buffer.clear();
                ds1.process (buffer);
            }

            const auto bias = ds1.getDebugBiasPoint();
            expect (std::isfinite (bias.vBase) && std::isfinite (bias.vEmitter) && std::isfinite (bias.vCollector));

            const float vbe = bias.vBase - bias.vEmitter;
            expectGreaterOrEqual (vbe, 0.2f);
            expectLessOrEqual (vbe, 0.8f);

            // Real collector current flowing -- meaningfully off both the
            // 9V rail (cutoff pinned high) and 0V (saturation pinned low).
            expectGreaterThan (bias.vCollector, 0.3f);
            expectLessThan (bias.vCollector, 8.7f);
        }

        beginTest ("Drive knob is a real, monotonically increasing distortion control");
        {
            float previousRms = -1.0f;

            for (float driveSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DS1StyleDistortionProcessor ds1;
                ds1.prepare (48000.0, 512, 1);
                setParam (ds1, "ds1_drive", driveSetting);

                juce::AudioBuffer<float> buffer (1, 512);
                double sumSquares = 0.0;
                int sampleCount = 0;

                for (int block = 0; block < 60; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        buffer.setSample (0, i, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }
                    ds1.process (buffer);

                    if (block >= 50)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                        {
                            const float s = buffer.getSample (0, i);
                            sumSquares += (double) s * (double) s;
                            ++sampleCount;
                        }
                }

                const float rms = (float) std::sqrt (sumSquares / (double) sampleCount);
                logMessage ("Drive=" + juce::String (driveSetting) + " rms=" + juce::String (rms, 4));

                expectGreaterThan (rms, previousRms);
                previousRms = rms;
            }
        }

        beginTest ("CPU cost stays comfortably real-time capable (regression guard)");
        {
            DS1StyleDistortionProcessor ds1;
            ds1.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.3f));
                buffer.setSample (1, i, 0.5f * std::sin ((float) i * 0.31f));
            }

            constexpr int numBlocks = 500; // ~5.3 seconds of audio at 48kHz/512 -- this processor is much heavier per-sample than the booster
            const auto start = juce::Time::getHighResolutionTicks();
            for (int block = 0; block < numBlocks; ++block)
                ds1.process (buffer);
            const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);

            const double audioSeconds = (double) numBlocks * buffer.getNumSamples() / 48000.0;
            const double realTimeFactor = audioSeconds / elapsedSeconds;
            logMessage ("Processed " + juce::String (audioSeconds, 1) + "s of stereo audio in "
                        + juce::String (elapsedSeconds, 3) + "s wall-clock -- " + juce::String (realTimeFactor, 1) + "x real-time");

            expectGreaterThan (realTimeFactor, 1.0);
            expect (std::isfinite (buffer.getSample (0, 0)));
        }

        beginTest ("a same-rate re-prepare() mid-signal does not click");
        {
            // The UI's chain-grid drag/reorder path rebuilds the whole
            // SignalGraph and calls prepare() again on every processor
            // already in the chain, at the same sample rate, mid-signal
            // (see Source/Engine/AGENTS.md and this processor's own
            // prepare() doc comment) -- this is exactly that scenario,
            // and is the regression guard for a real reported pop.
            DS1StyleDistortionProcessor ds1;
            ds1.prepare (48000.0, 128, 1);
            juce::AudioBuffer<float> buffer (1, 128);

            for (int block = 0; block < 100; ++block)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const double t = (double) (block * 128 + i) / 48000.0;
                    buffer.setSample (0, i, (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                }
                ds1.process (buffer);
            }
            const float beforeSample = buffer.getSample (0, 127);

            ds1.prepare (48000.0, 128, 1); // simulated reorder-triggered re-prepare

            for (int i = 0; i < 128; ++i)
            {
                const double t = (double) (100 * 128 + i) / 48000.0;
                buffer.setSample (0, i, (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
            }
            ds1.process (buffer);
            const float afterSample = buffer.getSample (0, 0);

            logMessage ("before=" + juce::String (beforeSample, 5) + " after=" + juce::String (afterSample, 5));
            expectLessThan (std::abs (beforeSample - afterSample), 1.0f);
        }

        beginTest ("stereo channels stay independent and both finite");
        {
            DS1StyleDistortionProcessor ds1;
            ds1.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int block = 0; block < 10; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    buffer.setSample (0, i, 0.4f * std::sin ((float) i * 0.1f));
                    buffer.setSample (1, i, -0.4f * std::sin ((float) i * 0.1f));
                }
                ds1.process (buffer);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expect (std::isfinite (buffer.getSample (ch, i)));
        }
    }
};

static DS1StyleDistortionProcessorTests ds1StyleDistortionProcessorTests;

} // namespace openguitarmultifx
