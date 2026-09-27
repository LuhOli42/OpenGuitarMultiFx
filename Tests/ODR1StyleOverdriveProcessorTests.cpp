#include "Effects/ODR1StyleOverdriveProcessor.h"
#include "Effects/PotTaper.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class ODR1StyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    ODR1StyleOverdriveProcessorTests() : juce::UnitTest ("ODR1StyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using Cx = std::complex<double>;

    static void setKnobs (ODR1StyleOverdriveProcessor& p, float drive, float spectrum, float level)
    {
        const float v[3] = { drive, spectrum, level };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 3)
                *f = v[i++];
    }

    static double steadyPeakToPeak (ODR1StyleOverdriveProcessor& p, double freq, double amplitude, int blocks = 400)
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

    template <typename Reader>
    static std::pair<double, double> stageExtremes (ODR1StyleOverdriveProcessor& p, double freq, double amplitude, Reader read)
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

    /** Stage 1's closed-form small-signal gain from the schematic: the input divider and network (C1/R4/Q5/R5, then C10, R10,
        R11 || C11), then a non-inverting stage with a finite-GBW op-amp, the two-shelf leg and the Drive pot's feedback. */
    static double stage1GainClosedForm (double freq, double driveKnob)
    {
        const Cx j (0.0, 1.0);
        const double w = 2.0 * juce::MathConstants<double>::pi * freq;
        const auto zc = [&] (double farads) { return Cx (1.0) / (j * w * farads); };
        const auto par = [] (Cx a, Cx b) { return a * b / (a + b); };

        const double divider = 1.0e6 / (33.0e3 + 1.0e6);
        const Cx zp = par (Cx (10.0e3), zc (22.0e-9));                 // R11 || C11
        const Cx zAfterB = zc (0.1e-6) + Cx (2.7e3) + zp;              // C10, R10, then that
        const Cx zB = par (Cx (330.0e3), zAfterB);
        const Cx zLoadA = Cx (200.0) + zB;
        const Cx zA = par (Cx (330.0e3), zLoadA);
        const Cx vA = zA / (zA + zc (0.22e-6));
        const Cx vB = zB / zLoadA;
        const Cx vP = zp / zAfterB;

        const double rOut = 250.0e3 * pots::audio (driveKnob), rIn = 250.0e3 - rOut;
        const double rFeedback = rOut + rIn * 1.8e3 / (rIn + 1.8e3);
        // The two 4148 in the feedback are not off at small signal: each has a zero-bias conductance Is/nVt, and there are two
        // of them (back to back), ~9 Mohm across the feedback. Against a 250K feedback that is 2.7% of gain, which is what a
        // 0.2 mV input measures; ignoring it would make the closed form 0.24 dB high at full Drive.
        const double gDiodes = 2.0 * 2.52e-9 / (1.752 * 25.85e-3);
        const Cx zf = par (par (Cx (rFeedback), Cx (1.0 / gDiodes)), zc (120.0e-12));
        const Cx zLeg = par (Cx (820.0) + zc (82.0e-9), Cx (1.5e3) + zc (2.2e-6));
        const Cx idealGain = Cx (1.0) + zf / zLeg;
        const Cx a = Cx (1.0e5) / (Cx (1.0) + j * (freq / 30.0));
        const Cx closed = idealGain / (Cx (1.0) + idealGain / a);
        return std::abs (divider * vA * vB * vP * closed);
    }

    void runTest() override
    {
        beginTest ("DC operating point: the three op-amp outputs sit at the 4.5 V bias, all three blocks converged");
        {
            ODR1StyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            logMessage ("stage 1 out " + juce::String (p.debugStage1Out(), 3) + " V, stage 2 out " + juce::String (p.debugStage2Out(), 3)
                        + " V, stage 3 out " + juce::String (p.debugStage3Out(), 3) + " V");
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.05);
            expectWithinAbsoluteError (p.debugStage2Out(), 4.5, 0.05);
            expectWithinAbsoluteError (p.debugStage3Out(), 4.5, 0.05);
        }

        beginTest ("stage 1's small-signal gain matches the closed form from the schematic (3 Drive settings x 3 frequencies)");
        {
            double worst = 0.0;
            for (float d : { 0.0f, 0.5f, 1.0f })
                for (double f : { 300.0, 1000.0, 2500.0 })
                {
                    ODR1StyleOverdriveProcessor p;
                    p.prepare (sr, 128, 1);
                    setKnobs (p, d, 0.5f, 1.0f);
                    const double amp = 0.0002;
                    const auto e = stageExtremes (p, f, amp, [] (ODR1StyleOverdriveProcessor& q) { return q.debugStage1Out(); });
                    const double measured = (e.second - e.first) / (2.0 * amp);
                    const double expected = stage1GainClosedForm (f, d);
                    const double dB = 20.0 * std::log10 (measured / expected);
                    worst = juce::jmax (worst, std::abs (dB));
                    logMessage ("Drive " + juce::String (d, 1) + ", " + juce::String (f, 0) + " Hz: measured " + juce::String (measured, 3)
                                + "x, closed form " + juce::String (expected, 3) + "x (" + juce::String (dB, 2) + " dB)");
                }
            expectLessThan (worst, 0.5);
        }

        beginTest ("stage 1 clips on its two 4148 in the feedback (~0.5-0.7 V), symmetrically");
        {
            ODR1StyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            setKnobs (p, 1.0f, 0.5f, 1.0f);
            const auto e = stageExtremes (p, 220.0, 0.05, [] (ODR1StyleOverdriveProcessor& q) { return q.debugStage1Out(); });
            const double pos = e.second - 4.5, neg = 4.5 - e.first;
            logMessage ("stage 1 peaks: +" + juce::String (pos, 3) + " V / -" + juce::String (neg, 3) + " V");
            expect (pos > 0.4 && pos < 0.85, "clip level on a 4148: " + juce::String (pos, 3));
            expectWithinAbsoluteError (pos, neg, 0.1 * pos);
        }

        beginTest ("Drive raises the output by more than 10x (5 mV in)");
        {
            ODR1StyleOverdriveProcessor lo, hi;
            lo.prepare (sr, 128, 1);
            hi.prepare (sr, 128, 1);
            setKnobs (lo, 0.0f, 0.5f, 1.0f);
            setKnobs (hi, 1.0f, 0.5f, 1.0f);
            const double a = steadyPeakToPeak (lo, 440.0, 0.005), b = steadyPeakToPeak (hi, 440.0, 0.005);
            logMessage ("Drive 0 -> " + juce::String (a, 4) + " p-p, Drive 1 -> " + juce::String (b, 4) + " p-p");
            expect (b > a * 10.0, "far more output with Drive up");
        }

        beginTest ("Spectrum: the response changes a great deal across its range (logged), and clockwise is brighter");
        {
            const auto response = [&] (float spectrum)
            {
                juce::String line ("Spectrum " + juce::String (spectrum, 1) + ":");
                std::vector<double> v;
                for (double f : { 150.0, 400.0, 800.0, 1600.0, 3000.0, 6000.0 })
                {
                    ODR1StyleOverdriveProcessor p;
                    p.prepare (sr, 128, 1);
                    setKnobs (p, 0.1f, spectrum, 1.0f);
                    const double pp = steadyPeakToPeak (p, f, 0.004, 300);
                    v.push_back (pp);
                    line += "  " + juce::String (f, 0) + " Hz " + juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, pp)), 1) + " dB";
                }
                logMessage (line);
                return v;
            };
            const auto dark = response (0.0f), bright = response (1.0f);
            (void) response (0.5f);
            const double darkTilt = dark[4] / juce::jmax (1.0e-9, dark[1]), brightTilt = bright[4] / juce::jmax (1.0e-9, bright[1]);
            logMessage ("3 kHz / 400 Hz: Spectrum 0 = " + juce::String (darkTilt, 3) + ", Spectrum 1 = " + juce::String (brightTilt, 3));
            expect (brightTilt > darkTilt * 1.2, "Spectrum clockwise is brighter");
        }

        beginTest ("Level is monotonic and near-silent at zero");
        {
            ODR1StyleOverdriveProcessor q, l;
            q.prepare (sr, 128, 1);
            l.prepare (sr, 128, 1);
            setKnobs (q, 0.6f, 0.5f, 0.0f);
            setKnobs (l, 0.6f, 0.5f, 1.0f);
            const double quiet = steadyPeakToPeak (q, 220.0, 0.05), loud = steadyPeakToPeak (l, 220.0, 0.05);
            logMessage ("Level 0 p-p " + juce::String (quiet, 5) + ", Level 1 p-p " + juce::String (loud, 4));
            expect (loud > quiet * 20.0, "Level opens up the output");
        }

        beginTest ("hot input and random knob moves: finite, and the solver never fails");
        {
            ODR1StyleOverdriveProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (2030);
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
                        + juce::String (p.debugIterations(), 2) + " Newton iterations/sample (three blocks)");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static ODR1StyleOverdriveProcessorTests odr1StyleOverdriveProcessorTests;

} // namespace openguitarmultifx
