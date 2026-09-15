#include "Effects/OD1StyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class OD1StyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    OD1StyleOverdriveProcessorTests() : juce::UnitTest ("OD1StyleOverdriveProcessor", "Effects") {}

    static void setParam (OD1StyleOverdriveProcessor& proc, const juce::String& paramId, float value)
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
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                od1.process (buffer);
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
                OD1StyleOverdriveProcessor od1;
                od1.prepare (48000.0, 512, 1);
                setParam (od1, "od1_drive", driveSetting);

                juce::AudioBuffer<float> buffer (1, 512);

                for (int block = 0; block < 40; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        buffer.setSample (0, i, (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }

                    od1.process (buffer);

                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        expect (std::isfinite (s));
                        expect (std::abs (s) < 20.0f);
                    }
                }
            }
        }

        beginTest ("Q6's DC operating point is forward-active, not cutoff");
        {
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 40; ++block)
            {
                buffer.clear();
                od1.process (buffer);
            }

            const auto bias = od1.getDebugBiasPoint();
            expect (std::isfinite (bias.vBase) && std::isfinite (bias.vEmitter) && std::isfinite (bias.vCollector));

            const float vbe = bias.vBase - bias.vEmitter;
            expectGreaterOrEqual (vbe, 0.2f);
            expectLessOrEqual (vbe, 0.8f);
        }

        beginTest ("clipping is genuinely asymmetric (1 diode vs 2 in series)");
        {
            // The whole reason this pedal gets its own AsymmetricDiodePair
            // rather than reusing chowdsp_wdf's symmetric DiodePairT: one
            // half-cycle should clip at roughly one silicon diode drop,
            // the other at roughly two -- so a loud sine's positive and
            // negative peaks should come out visibly different in
            // magnitude, unlike a symmetric clipper.
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 512, 1);
            setParam (od1, "od1_drive", 1.0f);

            juce::AudioBuffer<float> buffer (1, 512);
            float posPeak = 0.0f, negPeak = 0.0f;

            for (int block = 0; block < 60; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                    buffer.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)));
                }
                od1.process (buffer);

                if (block >= 50)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const float s = buffer.getSample (0, i);
                        posPeak = juce::jmax (posPeak, s);
                        negPeak = juce::jmin (negPeak, s);
                    }
            }

            logMessage ("posPeak=" + juce::String (posPeak, 4) + " negPeak=" + juce::String (negPeak, 4));
            expectGreaterThan (std::abs (posPeak - std::abs (negPeak)), 0.001f);
        }

        beginTest ("Drive knob is a real, monotonically increasing distortion control");
        {
            float previousPeakToPeak = -1.0f;

            for (float driveSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                OD1StyleOverdriveProcessor od1;
                od1.prepare (48000.0, 512, 1);
                setParam (od1, "od1_drive", driveSetting);

                juce::AudioBuffer<float> buffer (1, 512);
                float minVal = 1.0e9f, maxVal = -1.0e9f;

                // 400 blocks (~4.3s): prepare() already settles the DC
                // bias on SILENCE, but the sine itself still has to charge
                // through this circuit's own slow output coupling
                // (outputLoadResistance*C8 = 1Mohm*1uF = ~1s, same as the
                // DS-1's) before its steady-state AC amplitude is reached.
                // Peak-to-peak over the LAST block only (not an RMS
                // average over a wide window) -- an RMS average starting
                // as early as block 350 still mixed in a shrinking
                // transient large enough to invert the apparent trend
                // (verified via a targeted trace: the actual steady-state
                // amplitude at every stage tracked Drive/Level correctly
                // the whole time; only that particular measurement
                // methodology didn't).
                for (int block = 0; block < 400; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        buffer.setSample (0, i, (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }
                    od1.process (buffer);

                    if (block == 399)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                        {
                            const float s = buffer.getSample (0, i);
                            minVal = juce::jmin (minVal, s);
                            maxVal = juce::jmax (maxVal, s);
                        }
                }

                const float peakToPeak = maxVal - minVal;
                logMessage ("Drive=" + juce::String (driveSetting) + " peakToPeak=" + juce::String (peakToPeak, 5));

                expectGreaterThan (peakToPeak, previousPeakToPeak);
                previousPeakToPeak = peakToPeak;
            }
        }

        beginTest ("Level knob is a real, monotonically increasing volume control");
        {
            float previousPeakToPeak = -1.0f;

            for (float levelSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                OD1StyleOverdriveProcessor od1;
                od1.prepare (48000.0, 512, 1);
                setParam (od1, "od1_level", levelSetting);

                juce::AudioBuffer<float> buffer (1, 512);
                float minVal = 1.0e9f, maxVal = -1.0e9f;

                // Same ~1s output-coupling settle reasoning as the Drive
                // test above.
                for (int block = 0; block < 400; ++block)
                {
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        const double t = (double) (block * buffer.getNumSamples() + i) / 48000.0;
                        buffer.setSample (0, i, (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                    }
                    od1.process (buffer);

                    if (block == 399)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                        {
                            const float s = buffer.getSample (0, i);
                            minVal = juce::jmin (minVal, s);
                            maxVal = juce::jmax (maxVal, s);
                        }
                }

                const float peakToPeak = maxVal - minVal;
                logMessage ("Level=" + juce::String (levelSetting) + " peakToPeak=" + juce::String (peakToPeak, 5));

                expectGreaterThan (peakToPeak, previousPeakToPeak);
                previousPeakToPeak = peakToPeak;
            }
        }

        beginTest ("CPU cost stays comfortably real-time capable (regression guard)");
        {
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                buffer.setSample (0, i, 0.5f * std::sin ((float) i * 0.3f));
                buffer.setSample (1, i, 0.5f * std::sin ((float) i * 0.31f));
            }

            constexpr int numBlocks = 500;
            const auto start = juce::Time::getHighResolutionTicks();
            for (int block = 0; block < numBlocks; ++block)
                od1.process (buffer);
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
            // Same regression guard as DS1StyleDistortionProcessorTests --
            // see this processor's own prepare() doc comment for why the
            // UI's chain-grid reorder path triggers exactly this call
            // pattern on a real reported pop.
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 128, 1);
            juce::AudioBuffer<float> buffer (1, 128);

            for (int block = 0; block < 100; ++block)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const double t = (double) (block * 128 + i) / 48000.0;
                    buffer.setSample (0, i, (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
                }
                od1.process (buffer);
            }
            const float beforeSample = buffer.getSample (0, 127);

            od1.prepare (48000.0, 128, 1); // simulated reorder-triggered re-prepare

            for (int i = 0; i < 128; ++i)
            {
                const double t = (double) (100 * 128 + i) / 48000.0;
                buffer.setSample (0, i, (float) (0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t)));
            }
            od1.process (buffer);
            const float afterSample = buffer.getSample (0, 0);

            logMessage ("before=" + juce::String (beforeSample, 5) + " after=" + juce::String (afterSample, 5));
            expectLessThan (std::abs (beforeSample - afterSample), 1.0f);
        }

        beginTest ("stereo channels stay independent and both finite");
        {
            OD1StyleOverdriveProcessor od1;
            od1.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int block = 0; block < 10; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    buffer.setSample (0, i, 0.4f * std::sin ((float) i * 0.1f));
                    buffer.setSample (1, i, -0.4f * std::sin ((float) i * 0.1f));
                }
                od1.process (buffer);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expect (std::isfinite (buffer.getSample (ch, i)));
        }
    }
};

static OD1StyleOverdriveProcessorTests od1StyleOverdriveProcessorTests;

} // namespace openguitarmultifx
