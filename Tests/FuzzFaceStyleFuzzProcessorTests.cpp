#include "Effects/FuzzFaceStyleFuzzProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>

namespace openguitarmultifx
{

class FuzzFaceStyleFuzzProcessorTests : public juce::UnitTest
{
public:
    FuzzFaceStyleFuzzProcessorTests() : juce::UnitTest ("FuzzFaceStyleFuzzProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using Model = FuzzFaceStyleFuzzProcessor::Model;

    static void setKnobs (FuzzFaceStyleFuzzProcessor& p, float fuzz, float volume)
    {
        const float v[2] = { fuzz, volume };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 2)
                *f = v[i++];
    }

    /** Peak-to-peak of the output over the last block of a long run (see BigMuffStyleFuzzProcessorTests). */
    static double steadyPeakToPeak (FuzzFaceStyleFuzzProcessor& p, double freq, double amplitude, int blocks = 400)
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

    /** Small-signal gain of Q2's stage: the swing at Q2's collector over the swing at its base (= Q1's collector),
        read node by node, sample by sample, so it is the stage's own gain and not the whole pedal's. */
    static double stage2Gain (FuzzFaceStyleFuzzProcessor& p, double freq, double amplitude)
    {
        juce::AudioBuffer<float> one (1, 1);
        double phase = 0.0, c1lo = 1e9, c1hi = -1e9, c2lo = 1e9, c2hi = -1e9;
        const int total = (int) (sr * 0.6), measureFrom = (int) (sr * 0.4);
        for (int n = 0; n < total; ++n)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            one.setSample (0, 0, (float) (amplitude * std::sin (phase)));
            p.process (one);
            if (n >= measureFrom)
            {
                c1lo = juce::jmin (c1lo, p.debugQ1Collector()); c1hi = juce::jmax (c1hi, p.debugQ1Collector());
                c2lo = juce::jmin (c2lo, p.debugQ2Collector()); c2hi = juce::jmax (c2hi, p.debugQ2Collector());
            }
        }
        return (c2hi - c2lo) / juce::jmax (1.0e-12, c1hi - c1lo);
    }

    void runTest() override
    {
        for (auto model : { Model::germanium, Model::silicon })
        {
            const bool ge = model == Model::germanium;
            const juce::String tag (ge ? "Germanium: " : "Silicon: ");

            beginTest (tag + "DC operating point -- both transistors forward-active, neither pinned at a rail");
            {
                FuzzFaceStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 1);
                expect (p.dcConverged(), "DC solve converged");
                // Magnitudes: the PNP model runs from a NEGATIVE rail, so its voltages are negative.
                const double vc1 = std::abs (p.debugQ1Collector()), ve2 = std::abs (p.debugQ2Emitter()), vc2 = std::abs (p.debugQ2Collector());
                logMessage (tag + "Q1 collector " + juce::String (vc1, 3) + " V, Q2 emitter " + juce::String (ve2, 3)
                            + " V, Q2 collector " + juce::String (vc2, 3) + " V (magnitudes)");
                // Q2 is an emitter follower for DC: its emitter sits one junction drop below Q1's collector.
                expect (vc1 > ve2 && vc1 < ve2 + 1.0, "Q1's collector is one base-emitter drop above Q2's emitter");
                // Q2's collector has to have room to swing both ways: a bias point near either rail is the bug
                // this test exists to catch (a stuck-at-cutoff pedal is finite, stable and silent).
                expect (vc2 > 1.5 && vc2 < 7.5, "Q2's collector sits mid-supply: " + juce::String (vc2, 3) + " V");
            }

            beginTest (tag + "Q2's stage gain is ~8 with the Fuzz control down (Geofex: collector load / the pot)");
            {
                FuzzFaceStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 1);
                setKnobs (p, 0.0f, 1.0f);
                const double g = stage2Gain (p, 1000.0, 0.001);
                logMessage (tag + "stage-2 small-signal gain at Fuzz 0: " + juce::String (g, 2));
                // (8.2k + the supply-side resistor) over the 1K pot plus Q2's own emitter resistance (~40 ohm).
                expect (g > 6.5 && g < 9.5, "gain ~8 with the pot fully in circuit: " + juce::String (g, 2));
            }

            beginTest (tag + "turning Fuzz up raises the gain a great deal (the 22 uF takes the pot out for signal)");
            {
                FuzzFaceStyleFuzzProcessor lo (model), hi (model);
                lo.prepare (sr, 128, 1);
                hi.prepare (sr, 128, 1);
                setKnobs (lo, 0.0f, 1.0f);
                setKnobs (hi, 1.0f, 1.0f);
                // 0.5 mV, not 5 mV: since 2026-09-27 the guitar's own source resistance is modelled externally
                // (PickupLoadEffect, EffectRegistry.cpp), not inside this processor's own netlist -- so the bare
                // processor sees more of a given input than before, and 5 mV was already enough to clip at Fuzz 1,
                // masking the knob's own gain ratio (steadyPeakToPeak saturating at both settings).
                const double a = steadyPeakToPeak (lo, 440.0, 0.0005), b = steadyPeakToPeak (hi, 440.0, 0.0005);
                logMessage (tag + "0.5 mV in: Fuzz 0 -> " + juce::String (a, 5) + " p-p, Fuzz 1 -> " + juce::String (b, 5) + " p-p");
                expect (b > a * 3.0, "far more output with Fuzz up: " + juce::String (a, 5) + " -> " + juce::String (b, 5));
            }

            beginTest (tag + "Volume is monotonic and near-silent at zero");
            {
                FuzzFaceStyleFuzzProcessor q (model), l (model);
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
                FuzzFaceStyleFuzzProcessor p (model);
                p.prepare (sr, 128, 2);
                juce::Random rng (2027);
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
            FuzzFaceStyleFuzzProcessor a (Model::germanium), b (Model::silicon);
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

static FuzzFaceStyleFuzzProcessorTests fuzzFaceStyleFuzzProcessorTests;

} // namespace openguitarmultifx
