#include "Effects/BigMuffStyleFuzzProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>

namespace openguitarmultifx
{

class BigMuffStyleFuzzProcessorTests : public juce::UnitTest
{
public:
    BigMuffStyleFuzzProcessorTests() : juce::UnitTest ("BigMuffStyleFuzzProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (BigMuffStyleFuzzProcessor& p, float sustain, float tone, float volume)
    {
        const float v[3] = { sustain, tone, volume };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 3)
                *f = v[i++];
    }

    /** Peak-to-peak of the LAST block after a long settle -- the measurement the project settled on for these
        circuits (an RMS window that starts earlier still contains the turn-on transient; see AGENTS.md). */
    static double steadyStatePeakToPeak (BigMuffStyleFuzzProcessor& p, double freq, double amplitude, int blocks = 400)
    {
        juce::AudioBuffer<float> buf (1, 128);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                buf.setSample (0, i, (float) (amplitude * std::sin (phase)));
            }
            p.process (buf);
            if (b == blocks - 1)
                for (int i = 0; i < 128; ++i)
                {
                    lo = juce::jmin (lo, (double) buf.getSample (0, i));
                    hi = juce::jmax (hi, (double) buf.getSample (0, i));
                }
        }
        return hi - lo;
    }

    void runTest() override
    {
        for (auto model : { BigMuffStyleFuzzProcessor::Model::usV3, BigMuffStyleFuzzProcessor::Model::russianGreen, BigMuffStyleFuzzProcessor::Model::sovtekFirstEdition })
        {
            const bool us = model == BigMuffStyleFuzzProcessor::Model::usV3;
            const juce::String tag (us ? "USA V3: " : model == BigMuffStyleFuzzProcessor::Model::russianGreen ? "Russian: " : "Sovtek 1st: ");

            beginTest (tag + "DC operating point -- all four stages forward-active, none pinned at a rail");
            {
                BigMuffStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 1);
                expect (p.dcConverged(), "DC solve converged");
                for (int stage = 0; stage < 4; ++stage)
                {
                    const double vc = p.debugCollector (stage);
                    logMessage (tag + "stage " + juce::String (stage) + " collector " + juce::String (vc, 3) + " V");
                    // A transistor pinned at cutoff sits at the rail; one in saturation sits near ground. Either is
                    // the bug this test exists to catch (see the booster's cutoff bug in Source/Effects/AGENTS.md).
                    expect (vc > 0.5 && vc < 8.5, "stage " + juce::String (stage) + " is not pinned at a rail: "
                                                  + juce::String (vc, 3) + " V");
                }
                // The two clipping stages are the same cell with the same values, so they must land on the same point.
                expectWithinAbsoluteError (p.debugCollector (1), p.debugCollector (2), 0.25);
            }

            beginTest (tag + "Sustain controls COMPRESSION, not level -- the Big Muff's actual signature");
            {
                // A Big Muff's Sustain knob barely moves the output level: both clipping stages run into their
                // diodes either way, so the ceiling is the same. What it moves is how much of the dynamic range
                // survives -- wide open, a soft pick and a hard one come out at nearly the same level.
                const auto dynamicRange = [&] (float sustain)
                {
                    BigMuffStyleFuzzProcessor soft (model), loud (model);
                    soft.prepare (sr, 128, 1);
                    loud.prepare (sr, 128, 1);
                    setKnobs (soft, sustain, 0.5f, 1.0f);
                    setKnobs (loud, sustain, 0.5f, 1.0f);
                    // 0.05 -> 0.2 V is soft picking to digging in on a real pickup. Deliberately NOT a hotter
                    // pair: at 0.4-0.8 V (booster territory, not a guitar) the lower-gain Russian stops clipping
                    // flat and opens up again, which is the circuit being honest, not compression failing.
                    return steadyStatePeakToPeak (loud, 220.0, 0.20) / juce::jmax (1.0e-9, steadyStatePeakToPeak (soft, 220.0, 0.05));
                };
                const double clean = dynamicRange (0.0f), squashed = dynamicRange (1.0f);
                logMessage (tag + "4x input change gives a " + juce::String (clean, 2) + "x output change at Sustain 0, "
                            + juce::String (squashed, 2) + "x at Sustain 1");
                expect (squashed < clean, "more Sustain compresses harder: " + juce::String (clean, 2) + "x -> "
                                          + juce::String (squashed, 2) + "x");
                expect (squashed < 1.5, "at full Sustain a 4x input change is squashed to under 1.5x");
            }

            if (std::getenv ("BMP_CURVE") != nullptr)
            {
                for (float sus : { 0.0f, 0.3f, 0.7f, 1.0f })
                {
                    juce::String line (tag + "Sustain " + juce::String (sus, 2) + ":");
                    for (double amp : { 0.01, 0.025, 0.05, 0.1, 0.2, 0.4, 0.8 })
                    {
                        BigMuffStyleFuzzProcessor p (model);
                        p.prepare (sr, 128, 1);
                        setKnobs (p, sus, 0.5f, 1.0f);
                        line += "  in " + juce::String (amp, 3) + " -> " + juce::String (steadyStatePeakToPeak (p, 220.0, amp), 4);
                    }
                    logMessage (line);
                }
            }

            beginTest (tag + "Volume is monotonic and near-silent at zero");
            {
                BigMuffStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 1);
                setKnobs (p, 0.7f, 0.5f, 0.0f);
                const double quiet = steadyStatePeakToPeak (p, 220.0, 0.1);
                BigMuffStyleFuzzProcessor q (model);
                q.prepare (sr, 128, 1);
                setKnobs (q, 0.7f, 0.5f, 1.0f);
                const double loud = steadyStatePeakToPeak (q, 220.0, 0.1);
                logMessage (tag + "Volume 0 p-p " + juce::String (quiet, 5) + ", Volume 1 p-p " + juce::String (loud, 4));
                expect (loud > quiet * 20.0, "Volume opens up the output");
            }

            beginTest (tag + "Tone moves the balance between the two branches");
            {
                // The tone stack is a fixed notch; the pot picks which side of it dominates. Compare a low and a
                // high note rather than absolute level, since the pot also changes the loading.
                const auto ratio = [&] (float tone)
                {
                    BigMuffStyleFuzzProcessor p (model);
                    p.prepare (sr, 128, 1);
                    setKnobs (p, 0.7f, tone, 1.0f);
                    const double bass = steadyStatePeakToPeak (p, 120.0, 0.1);
                    BigMuffStyleFuzzProcessor q (model);
                    q.prepare (sr, 128, 1);
                    setKnobs (q, 0.7f, tone, 1.0f);
                    const double treble = steadyStatePeakToPeak (q, 3000.0, 0.1);
                    return treble / juce::jmax (1.0e-9, bass);
                };
                const double dark = ratio (0.0f), bright = ratio (1.0f);
                logMessage (tag + "treble/bass ratio: Tone 0 = " + juce::String (dark, 3) + ", Tone 1 = " + juce::String (bright, 3));
                expect (dark != bright, "the Tone control does something");
            }

            beginTest (tag + "hot input and knob extremes stay finite and the solver converges");
            {
                BigMuffStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 2);
                juce::Random rng (2026);
                juce::AudioBuffer<float> buf (2, 128);
                double phase = 0.0;
                bool finite = true;
                for (int b = 0; b < 1200; ++b)
                {
                    if (b % 40 == 0)
                        setKnobs (p, rng.nextFloat(), rng.nextFloat(), rng.nextFloat());
                    const double amp = b % 300 < 40 ? 3.0 : 0.3; // hot bursts a pedal in front could really deliver
                    for (int i = 0; i < 128; ++i)
                    {
                        phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / sr;
                        const float x = (float) (amp * std::sin (phase));
                        buf.setSample (0, i, x);
                        buf.setSample (1, i, x);
                    }
                    p.process (buf);
                    for (int i = 0; i < 128; ++i)
                        finite = finite && std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 50.0f;
                }
                expect (finite, "output stays finite and bounded");
                logMessage (tag + "solver failure rate " + juce::String (p.getSolveFailureRate(), 6)
                            + ", " + juce::String (p.debugIterations(), 2) + " Newton iterations/sample");
                expectLessThan (p.getSolveFailureRate(), 1.0e-5);
            }
        }

        beginTest ("the two models are genuinely different circuits, not the same one twice");
        {
            BigMuffStyleFuzzProcessor a (BigMuffStyleFuzzProcessor::Model::usV3);
            BigMuffStyleFuzzProcessor b (BigMuffStyleFuzzProcessor::Model::russianGreen);
            a.prepare (sr, 128, 1);
            b.prepare (sr, 128, 1);
            setKnobs (a, 0.7f, 0.5f, 1.0f);
            setKnobs (b, 0.7f, 0.5f, 1.0f);
            const double pa = steadyStatePeakToPeak (a, 220.0, 0.1);
            const double pb = steadyStatePeakToPeak (b, 220.0, 0.1);
            logMessage ("USA p-p " + juce::String (pa, 4) + " vs Russian p-p " + juce::String (pb, 4));
            expect (std::abs (pa - pb) > 0.01 * juce::jmax (pa, pb), "the two models measure differently");
        }
    }
};

static BigMuffStyleFuzzProcessorTests bigMuffStyleFuzzProcessorTests;

} // namespace openguitarmultifx
