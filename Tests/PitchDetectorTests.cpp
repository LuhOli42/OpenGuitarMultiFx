#include "Engine/PitchDetector.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class PitchDetectorTests : public juce::UnitTest
{
public:
    PitchDetectorTests() : juce::UnitTest ("PitchDetector", "Engine") {}

    static void feedSine (PitchDetector& detector, double sampleRate, double toneHz, int totalSamples)
    {
        constexpr int blockSize = 128;
        double phase = 0.0;
        const double phaseInc = juce::MathConstants<double>::twoPi * toneHz / sampleRate;

        std::vector<float> block ((size_t) blockSize);
        int remaining = totalSamples;
        while (remaining > 0)
        {
            const int thisBlock = juce::jmin (blockSize, remaining);
            for (int i = 0; i < thisBlock; ++i)
            {
                block[(size_t) i] = (float) std::sin (phase);
                phase += phaseInc;
            }
            detector.pushSamples (block.data(), thisBlock);
            remaining -= thisBlock;
        }
        detector.analyse(); // the analysis runs on the control thread now -- see PitchDetector::analyse()
    }

    void runTest() override
    {
        beginTest ("silence reports no detected pitch (0 Hz)");
        {
            PitchDetector detector;
            detector.prepare (48000.0);

            std::vector<float> silence ((size_t) 128, 0.0f);
            for (int block = 0; block < 100; ++block)
                detector.pushSamples (silence.data(), (int) silence.size());
            detector.analyse();

            expectWithinAbsoluteError (detector.getDetectedFrequencyHz(), 0.0f, 1.0e-6f);
        }

        beginTest ("a 110Hz sine (guitar A2) is detected within 1% of the true frequency");
        {
            PitchDetector detector;
            detector.prepare (48000.0);

            // Enough samples for several analysis cycles (~15Hz update
            // rate) to run and settle.
            feedSine (detector, 48000.0, 110.0, (int) (48000.0 * 0.5));

            const float detected = detector.getDetectedFrequencyHz();
            expect (detected > 0.0f);
            expectWithinAbsoluteError (detected, 110.0f, 110.0f * 0.01f);
        }

        beginTest ("a 440Hz sine (A4) is detected within 1% of the true frequency, not an octave off");
        {
            PitchDetector detector;
            detector.prepare (48000.0);

            feedSine (detector, 48000.0, 440.0, (int) (48000.0 * 0.5));

            const float detected = detector.getDetectedFrequencyHz();
            expect (detected > 0.0f);
            expectWithinAbsoluteError (detected, 440.0f, 440.0f * 0.01f);
        }

        beginTest ("a 82.4Hz sine (standard-tuning low E) is detected within 1.5% of the true frequency");
        {
            PitchDetector detector;
            detector.prepare (48000.0);

            feedSine (detector, 48000.0, 82.4, (int) (48000.0 * 0.6));

            const float detected = detector.getDetectedFrequencyHz();
            expect (detected > 0.0f);
            expectWithinAbsoluteError (detected, 82.4f, 82.4f * 0.015f);
        }

        beginTest ("works at a non-48kHz sample rate too (44.1kHz)");
        {
            PitchDetector detector;
            detector.prepare (44100.0);

            feedSine (detector, 44100.0, 220.0, (int) (44100.0 * 0.5));

            const float detected = detector.getDetectedFrequencyHz();
            expect (detected > 0.0f);
            expectWithinAbsoluteError (detected, 220.0f, 220.0f * 0.01f);
        }
    }
};

static PitchDetectorTests pitchDetectorTests;

} // namespace openguitarmultifx
