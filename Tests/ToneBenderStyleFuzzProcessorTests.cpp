#include "Effects/ToneBenderStyleFuzzProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class ToneBenderStyleFuzzProcessorTests : public juce::UnitTest
{
public:
    ToneBenderStyleFuzzProcessorTests() : juce::UnitTest ("ToneBenderStyleFuzzProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using Model = ToneBenderStyleFuzzProcessor::Model;

    static void setKnobs (ToneBenderStyleFuzzProcessor& p, float attack, float volume)
    {
        const float v[2] = { attack, volume };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 2)
                *f = v[i++];
    }

    /** Peak-to-peak of the output over the last block of a long run (see BigMuffStyleFuzzProcessorTests). */
    static double steadyPeakToPeak (ToneBenderStyleFuzzProcessor& p, double freq, double amplitude, int blocks = 400)
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
        for (auto model : { Model::germanium, Model::silicon })
        {
            const bool ge = model == Model::germanium;
            const juce::String tag (ge ? "Germanium: " : "Silicon: ");

            beginTest (tag + "DC operating point -- every stage forward-active with room to swing");
            {
                ToneBenderStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 1);
                expect (p.dcConverged(), "DC solve converged");
                // Magnitudes (the PNP model runs from a negative rail).
                const double v1 = std::abs (p.debugQ1Collector()), v2 = std::abs (p.debugQ2Collector());
                const double ve3 = std::abs (p.debugQ3Emitter()), v3 = std::abs (p.debugQ3Collector());
                logMessage (tag + "Q1 collector " + juce::String (v1, 3) + " V, Q2 collector " + juce::String (v2, 3)
                            + " V, Q3 emitter " + juce::String (ve3, 3) + " V, Q3 collector " + juce::String (v3, 3) + " V (magnitudes)");
                // A stage stuck at cutoff sits at the rail, one in saturation next to its emitter: both are silent.
                expect (v1 > 1.0 && v1 < 8.0, "Q1's collector has room both ways: " + juce::String (v1, 3));
                expect (v3 > ve3 + 1.5 && v3 < 8.0, "Q3 is not saturated and not cut off: " + juce::String (v3, 3));
                // Q3's base is Q2's collector, so it sits one junction above Q3's emitter.
                expect (v2 > ve3 && v2 < ve3 + 1.0, "Q2's collector is one base-emitter drop above Q3's emitter");
            }

            beginTest (tag + "Attack raises the gain a great deal (the 4.7 uF takes the pot out for signal)");
            {
                ToneBenderStyleFuzzProcessor lo (model), hi (model);
                lo.prepare (sr, 128, 1);
                hi.prepare (sr, 128, 1);
                setKnobs (lo, 0.0f, 1.0f);
                setKnobs (hi, 1.0f, 1.0f);
                const double a = steadyPeakToPeak (lo, 440.0, 0.0001), b = steadyPeakToPeak (hi, 440.0, 0.0001);
                logMessage (tag + "0.1 mV in: Attack 0 -> " + juce::String (a, 5) + " p-p, Attack 1 -> " + juce::String (b, 5) + " p-p");
                expect (b > a * 3.0, "far more output with Attack up: " + juce::String (a, 5) + " -> " + juce::String (b, 5));
            }

            beginTest (tag + "Volume is monotonic and near-silent at zero");
            {
                ToneBenderStyleFuzzProcessor q (model), l (model);
                q.prepare (sr, 128, 1);
                l.prepare (sr, 128, 1);
                setKnobs (q, 0.8f, 0.0f);
                setKnobs (l, 0.8f, 1.0f);
                const double quiet = steadyPeakToPeak (q, 220.0, 0.1), loud = steadyPeakToPeak (l, 220.0, 0.1);
                logMessage (tag + "Volume 0 p-p " + juce::String (quiet, 5) + ", Volume 1 p-p " + juce::String (loud, 4));
                expect (loud > quiet * 20.0, "Volume opens up the output");
            }

            beginTest (tag + "hot input and knob extremes stay finite and the solver converges");
            {
                ToneBenderStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 2);
                juce::Random rng (2028);
                juce::AudioBuffer<float> buf (2, 128);
                double phase = 0.0;
                bool finite = true;
                for (int b = 0; b < 1200; ++b)
                {
                    if (b % 40 == 0)
                        setKnobs (p, rng.nextFloat(), rng.nextFloat());
                    const double amp = b % 300 < 40 ? 3.0 : 0.3;
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

        beginTest ("the two models are genuinely different circuits");
        {
            ToneBenderStyleFuzzProcessor a (Model::germanium), b (Model::silicon);
            a.prepare (sr, 128, 1);
            b.prepare (sr, 128, 1);
            setKnobs (a, 0.8f, 1.0f);
            setKnobs (b, 0.8f, 1.0f);
            const double pa = steadyPeakToPeak (a, 220.0, 0.1), pb = steadyPeakToPeak (b, 220.0, 0.1);
            logMessage ("germanium p-p " + juce::String (pa, 4) + " vs silicon p-p " + juce::String (pb, 4));
            expect (std::abs (pa - pb) > 0.01 * juce::jmax (pa, pb), "the two models measure differently");
        }
    }
};

static ToneBenderStyleFuzzProcessorTests toneBenderStyleFuzzProcessorTests;

} // namespace openguitarmultifx
