#include "Engine/PitchDetector.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/** The tuner has to find every string of every instrument this app tunes, including the low ones. Before 2026-09-21 its
    search floor was 70 Hz, so B0/E1/F#1/A1/B1 -- the low strings of 5-string basses and 7/8-string guitars -- could not be
    found at all: the lag the search reaches never gets that long. The user reported exactly that ("ele n pega notas graves
    direto"). */
class PitchDetectorRangeTests : public juce::UnitTest
{
public:
    PitchDetectorRangeTests() : juce::UnitTest ("PitchDetectorRange", "Engine") {}

    static constexpr double sr = 48000.0;

    /** Feeds a harmonically rich tone (a plucked string is not a sine) and returns what the detector makes of it. */
    static float detect (double freq, float minHz = 28.0f)
    {
        PitchDetector detector;
        detector.prepare (sr, minHz);

        std::vector<float> block (256);
        double phase = 0.0;
        for (int b = 0; b < 400; ++b)
        {
            for (auto& s : block)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                s = (float) (0.25 * (std::sin (phase) + 0.5 * std::sin (2.0 * phase)
                                     + 0.3 * std::sin (3.0 * phase) + 0.15 * std::sin (4.0 * phase)));
            }
            detector.pushSamples (block.data(), (int) block.size());
        }
        detector.analyse();
        return detector.getDetectedFrequencyHz();
    }

    void runTest() override
    {
        beginTest ("every string of every instrument the app tunes is found, within 2 cents");
        struct Note { const char* name; double hz; };
        for (auto note : { Note { "B0 (5-string bass)", 30.87 }, Note { "E1 (bass)", 41.20 },
                           Note { "F#1 (8-string)", 46.25 }, Note { "B1 (7-string)", 61.74 },
                           Note { "E2 (guitar low E)", 82.41 }, Note { "A2", 110.00 },
                           Note { "D3", 146.83 }, Note { "G3", 196.00 },
                           Note { "B3", 246.94 }, Note { "E4 (guitar high E)", 329.63 },
                           Note { "A4", 440.00 }, Note { "E6 (24th fret, high E)", 1318.51 } })
        {
            const float found = detect (note.hz);
            const double cents = found > 0.0f ? 1200.0 * std::log2 ((double) found / note.hz) : -9999.0;
            logMessage (juce::String (note.name).paddedRight (' ', 24) + juce::String (note.hz, 2) + " Hz -> "
                        + (found > 0.0f ? juce::String (found, 2) + " Hz (" + juce::String (cents, 2) + " cents)"
                                        : juce::String ("NOT DETECTED")));
            expectGreaterThan (found, 0.0f);
            expectLessThan (std::abs (cents), 2.0);
        }

        beginTest ("floor: steady note at decreasing levels (diagnostic)");
        for (double amp : { 0.3, 0.1, 0.03, 0.01, 0.003, 0.001, 0.0003 })
        {
            PitchDetector detector;
            detector.prepare (sr);
            std::vector<float> block (256);
            double phase = 0.0;
            for (int b = 0; b < 400; ++b)
            {
                for (auto& s : block)
                {
                    phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                    s = (float) (amp * (std::sin (phase) + 0.4 * std::sin (2.0 * phase)));
                }
                detector.pushSamples (block.data(), (int) block.size());
            }
            detector.analyse();
            logMessage ("  amp " + juce::String (amp, 4) + " (" + juce::String (20.0 * std::log10 (amp), 1) + " dBFS) -> "
                        + juce::String (detector.getDetectedFrequencyHz(), 2) + " Hz");
        }

        beginTest ("a quiet, decaying note keeps being tracked instead of vanishing while it is still audible");
        {
            PitchDetector detector;
            detector.prepare (sr);

            // A plucked A2 decaying over several seconds, tracked the whole way down.
            std::vector<float> block (256);
            double phase = 0.0;
            float lastTracked = 0.0f;
            double quietestTrackedAmplitude = 1.0;
            for (int b = 0; b < 2600; ++b) // long enough for the note to fall below any plausible floor (~14 s)
            {
                const double amplitude = 0.35 * std::exp (-(double) b * 256.0 / sr / 1.6); // ~1.6 s decay
                for (auto& s : block)
                {
                    phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                    s = (float) (amplitude * (std::sin (phase) + 0.4 * std::sin (2.0 * phase)));
                }
                detector.pushSamples (block.data(), (int) block.size());
                if ((b % 8) == 0)
                {
                    detector.analyse();
                    const float hz = detector.getDetectedFrequencyHz();
                    if (hz > 0.0f)
                    {
                        lastTracked = hz;
                        quietestTrackedAmplitude = std::min (quietestTrackedAmplitude, amplitude);
                    }
                }
            }
            logMessage ("still tracking down to an amplitude of " + juce::String (quietestTrackedAmplitude, 5)
                        + " (" + juce::String (20.0 * std::log10 (quietestTrackedAmplitude), 1) + " dBFS), last reading "
                        + juce::String (lastTracked, 2) + " Hz");
            expectLessThan (quietestTrackedAmplitude, 0.005); // still tracking below -46 dBFS, where a note is plainly audible
            expectWithinAbsoluteError (lastTracked, 110.0f, 2.0f);
        }

        beginTest ("baseline: what the OLD 70 Hz floor gave at the same frequencies (diagnostic)");
        for (double hz : { 82.41, 146.83, 329.63, 1318.51 })
        {
            const float found = detect (hz, 70.0f);
            const double cents = found > 0.0f ? 1200.0 * std::log2 ((double) found / hz) : -9999.0;
            logMessage ("  OLD floor " + juce::String (hz, 2) + " Hz -> " + juce::String (found, 2) + " Hz ("
                        + juce::String (cents, 2) + " cents)");
        }

        beginTest ("silence reports no pitch rather than a wrong note");
        {
            PitchDetector detector;
            detector.prepare (sr);
            std::vector<float> block (256, 0.0f);
            for (int b = 0; b < 200; ++b)
                detector.pushSamples (block.data(), (int) block.size());
            detector.analyse();
            expectEquals (detector.getDetectedFrequencyHz(), 0.0f);
        }
    }
};

static PitchDetectorRangeTests pitchDetectorRangeTests;

} // namespace openguitarmultifx
