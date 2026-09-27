#include "Effects/DT1StyleDistortionProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class DT1StyleDistortionProcessorTests : public juce::UnitTest
{
public:
    DT1StyleDistortionProcessorTests() : juce::UnitTest ("DT1StyleDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using Cx = std::complex<double>;

    static void setKnobs (DT1StyleDistortionProcessor& p, float distortion, float tone, float level)
    {
        const float v[3] = { distortion, tone, level };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 3)
                *f = v[i++];
    }

    static double steadyPeakToPeak (DT1StyleDistortionProcessor& p, double freq, double amplitude, int blocks = 400)
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

    /** Extremes of a stage's output node over the last 0.2 s of a run, one sample at a time. */
    template <typename Reader>
    static std::pair<double, double> stageExtremes (DT1StyleDistortionProcessor& p, double freq, double amplitude, Reader read)
    {
        juce::AudioBuffer<float> one (1, 1);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        const int total = (int) (sr * 0.7), from = (int) (sr * 0.5);
        for (int n = 0; n < total; ++n)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            one.setSample (0, 0, (float) (amplitude * std::sin (phase)));
            p.process (one);
            if (n >= from)
            {
                lo = juce::jmin (lo, read (p));
                hi = juce::jmax (hi, read (p));
            }
        }
        return { lo, hi };
    }

    /** Stage 1's closed-form small-signal gain at `freq`, from the schematic: the input divider and high-pass (C1, R4,
        Q5, R5, C10, R10), then a non-inverting stage with a finite-gain-bandwidth op-amp. */
    static double stage1GainClosedForm (double freq, double distortionKnob)
    {
        const Cx j (0.0, 1.0);
        const double w = 2.0 * juce::MathConstants<double>::pi * freq;
        const auto zc = [&] (double farads) { return Cx (1.0) / (j * w * farads); };
        const auto par = [] (Cx a, Cx b) { return a * b / (a + b); };

        const double divider = 1.0e6 / (33.0e3 + 1.0e6);
        const Cx zAfterB = zc (33.0e-9) + Cx (22.0e3);                 // C10 into R10
        const Cx zB = par (Cx (330.0e3), zAfterB);                     // R5 || that
        const Cx zLoadA = Cx (200.0) + zB;                             // the JFET switch, then node B
        const Cx zA = par (Cx (330.0e3), zLoadA);                      // R4 || the rest
        const Cx vA = zA / (zA + zc (0.22e-6));                        // C1 into node A
        const Cx vB = zB / zLoadA;
        const Cx vP = Cx (22.0e3) / zAfterB;

        const double rFeedback = 10.0e3 + 250.0e3 * distortionKnob;
        const Cx zf = par (Cx (rFeedback), zc (1.5e-9));
        const Cx zLeg = Cx (3.3e3) + zc (0.68e-6);
        const Cx idealGain = Cx (1.0) + zf / zLeg;
        const Cx a = Cx (1.0e5) / (Cx (1.0) + j * (freq / 30.0));      // 100 dB, 3 MHz GBW
        const Cx closed = idealGain / (Cx (1.0) + idealGain / a);
        return std::abs (divider * vA * vB * vP * closed);
    }

    void runTest() override
    {
        beginTest ("DC operating point: both op-amp outputs sit at the 4.5 V bias, both blocks converged");
        {
            DT1StyleDistortionProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            logMessage ("stage 1 out " + juce::String (p.debugStage1Out(), 3) + " V, stage 2 out " + juce::String (p.debugStage2Out(), 3) + " V");
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.05);
            expectWithinAbsoluteError (p.debugStage2Out(), 4.5, 0.05);
        }

        beginTest ("stage 1's small-signal gain matches the closed form from the schematic (3 Distortion settings x 3 frequencies)");
        {
            double worst = 0.0;
            for (float d : { 0.0f, 0.5f, 1.0f })
                for (double f : { 300.0, 1000.0, 3000.0 })
                {
                    DT1StyleDistortionProcessor p;
                    p.prepare (sr, 128, 1);
                    setKnobs (p, d, 0.5f, 1.0f);
                    const double amp = 0.0005;
                    const auto e = stageExtremes (p, f, amp, [] (DT1StyleDistortionProcessor& q) { return q.debugStage1Out(); });
                    const double measured = (e.second - e.first) / (2.0 * amp);
                    const double expected = stage1GainClosedForm (f, d);
                    const double dB = 20.0 * std::log10 (measured / expected);
                    worst = juce::jmax (worst, std::abs (dB));
                    logMessage ("Distortion " + juce::String (d, 1) + ", " + juce::String (f, 0) + " Hz: measured " + juce::String (measured, 3)
                                + "x, closed form " + juce::String (expected, 3) + "x (" + juce::String (dB, 2) + " dB)");
                }
            expectLessThan (worst, 0.5);
        }

        beginTest ("stage 1 clips softly on its two red LEDs (~1.7-2 V), symmetrically");
        {
            DT1StyleDistortionProcessor p;
            p.prepare (sr, 128, 1);
            setKnobs (p, 1.0f, 0.5f, 1.0f);
            const auto e = stageExtremes (p, 220.0, 0.05, [] (DT1StyleDistortionProcessor& q) { return q.debugStage1Out(); });
            const double pos = e.second - 4.5, neg = 4.5 - e.first;
            logMessage ("stage 1 peaks: +" + juce::String (pos, 3) + " V / -" + juce::String (neg, 3) + " V");
            expect (pos > 1.3 && pos < 2.4, "positive peak on the LEDs: " + juce::String (pos, 3));
            expectWithinAbsoluteError (pos, neg, 0.15 * pos);
        }

        beginTest ("stage 2 clips ASYMMETRICALLY: one 4148 one way (~-0.6 V), two in series the other (~+1.2 V)");
        {
            DT1StyleDistortionProcessor p;
            p.prepare (sr, 128, 1);
            setKnobs (p, 1.0f, 0.5f, 1.0f);
            const auto e = stageExtremes (p, 220.0, 0.05, [] (DT1StyleDistortionProcessor& q) { return q.debugStage2Out(); });
            const double pos = e.second - 4.5, neg = 4.5 - e.first;
            logMessage ("stage 2 peaks: +" + juce::String (pos, 3) + " V / -" + juce::String (neg, 3) + " V, ratio " + juce::String (pos / neg, 2));
            expect (neg > 0.35 && neg < 0.85, "negative peak on the single diode: " + juce::String (neg, 3));
            expect (pos > 0.9 && pos < 1.6, "positive peak on the two in series: " + juce::String (pos, 3));
            expect (pos / neg > 1.5 && pos / neg < 2.8, "about 2:1 asymmetry: " + juce::String (pos / neg, 2));
        }

        beginTest ("the one Distortion pot raises the gain of BOTH stages: 5 mV in, output p-p grows by more than 10x");
        {
            DT1StyleDistortionProcessor lo, hi;
            lo.prepare (sr, 128, 1);
            hi.prepare (sr, 128, 1);
            setKnobs (lo, 0.0f, 0.5f, 1.0f);
            setKnobs (hi, 1.0f, 0.5f, 1.0f);
            const double a = steadyPeakToPeak (lo, 440.0, 0.005), b = steadyPeakToPeak (hi, 440.0, 0.005);
            logMessage ("Distortion 0 -> " + juce::String (a, 4) + " p-p, Distortion 1 -> " + juce::String (b, 4) + " p-p");
            expect (b > a * 10.0, "far more output with Distortion up");
        }

        beginTest ("Level is monotonic and near-silent at zero; Tone moves the treble/bass balance");
        {
            DT1StyleDistortionProcessor q, l;
            q.prepare (sr, 128, 1);
            l.prepare (sr, 128, 1);
            setKnobs (q, 0.6f, 0.5f, 0.0f);
            setKnobs (l, 0.6f, 0.5f, 1.0f);
            const double quiet = steadyPeakToPeak (q, 220.0, 0.05), loud = steadyPeakToPeak (l, 220.0, 0.05);
            logMessage ("Level 0 p-p " + juce::String (quiet, 5) + ", Level 1 p-p " + juce::String (loud, 4));
            expect (loud > quiet * 20.0, "Level opens up the output");

            const auto ratio = [&] (float tone)
            {
                DT1StyleDistortionProcessor a, b;
                a.prepare (sr, 128, 1);
                b.prepare (sr, 128, 1);
                setKnobs (a, 0.3f, tone, 1.0f);
                setKnobs (b, 0.3f, tone, 1.0f);
                return steadyPeakToPeak (b, 3000.0, 0.02) / juce::jmax (1.0e-9, steadyPeakToPeak (a, 120.0, 0.02));
            };
            const double dark = ratio (0.0f), bright = ratio (1.0f);
            logMessage ("treble/bass ratio: Tone 0 = " + juce::String (dark, 3) + ", Tone 1 = " + juce::String (bright, 3));
            expect (bright > dark * 1.5, "Tone clockwise is brighter");
        }

        beginTest ("hot input and random knob moves: finite, and the solver never fails");
        {
            DT1StyleDistortionProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (2029);
            juce::AudioBuffer<float> buf (2, 128);
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 1200; ++b)
            {
                if (b % 40 == 0)
                    setKnobs (p, rng.nextFloat(), rng.nextFloat(), rng.nextFloat());
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
            logMessage ("solver failure rate " + juce::String (p.getSolveFailureRate(), 6) + ", "
                        + juce::String (p.debugIterations(), 2) + " Newton iterations/sample (both blocks)");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static DT1StyleDistortionProcessorTests dt1StyleDistortionProcessorTests;

} // namespace openguitarmultifx
