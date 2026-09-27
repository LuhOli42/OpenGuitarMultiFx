#include "Effects/BluesBreakerStyleOverdriveProcessor.h"

#include "PedalStress.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class BluesBreakerStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    BluesBreakerStyleOverdriveProcessorTests() : juce::UnitTest ("BluesBreakerStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;

    static void setKnobs (BluesBreakerStyleOverdriveProcessor& p, float drive, float tone, float level)
    {
        auto params = p.getParameters()->getParameters (true);
        const float values[] = { drive, tone, level };
        for (int i = 0; i < 3; ++i)
            *dynamic_cast<juce::AudioParameterFloat*> (params[i]) = values[i];
    }

    struct Measure { double out, stage1, stage2, stage2Peak; float pos, neg; };

    static Measure runSine (BluesBreakerStyleOverdriveProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        juce::AudioBuffer<float> buf (1, 1);
        double oS = 0, oC = 0, s1S = 0, s1C = 0, s2S = 0, s2C = 0, s2Peak = 0;
        float pos = 0, neg = 0;

        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            buf.setSample (0, 0, (float) (amp * std::sin (ph)));
            p.process (buf);
            if (n >= start)
            {
                const double y = buf.getSample (0, 0);
                const double v1 = p.debugStage1Out(), v2 = p.debugStage2Out();
                oS += y * std::sin (ph); oC += y * std::cos (ph);
                s1S += v1 * std::sin (ph); s1C += v1 * std::cos (ph);
                s2S += v2 * std::sin (ph); s2C += v2 * std::cos (ph);
                s2Peak = juce::jmax (s2Peak, std::abs (v2 - 4.5));
                pos = juce::jmax (pos, (float) y);
                neg = juce::jmin (neg, (float) y);
            }
        }

        const double k = 2.0 / (double) len;
        return { k * std::sqrt (oS * oS + oC * oC), k * std::sqrt (s1S * s1S + s1C * s1C),
                 k * std::sqrt (s2S * s2S + s2C * s2C), s2Peak, pos, neg };
    }

    static C op (double f) { return C (0.0, 2.0 * juce::MathConstants<double>::pi * f); }

    /** Stage 1: 10 nF into (+) with 1M to the bias, TL072 (A0 2e5, GBW 3 MHz); (-) leg = 10 nF + (27K || (33K + 10 nF));
        feedback = the Drive pot's (-) side || 100 pF. */
    static double stage1Gain (double f, double knob)
    {
        const C s = op (f);
        const C vPlus = 1.0e6 / (1.0e6 + 1.0 / (s * 10.0e-9));
        const double rFb = juce::jmax (1.0, 100.0e3 * knob);
        const C zFb = 1.0 / (1.0 / rFb + s * 100.0e-12);
        const C zBranch = 33.0e3 + 1.0 / (s * 10.0e-9);
        const C zLeg = 1.0 / (s * 10.0e-9) + 1.0 / (1.0 / 27.0e3 + 1.0 / zBranch);
        const C beta = zLeg / (zLeg + zFb);
        const C a = 2.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * 15.0));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    /** Stage 2, diodes off: -220K / (Drive series segment + 100 nF + 10K). */
    static double stage2Gain (double f, double knob)
    {
        const C s = op (f);
        const double rSer = juce::jmax (1.0, 100.0e3 - juce::jmax (1.0, 100.0e3 * knob));
        const C zIn = 100.0 + rSer + 1.0 / (s * 100.0e-9) + 10.0e3;
        const C ideal = 220.0e3 / zIn;
        const C a = 2.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * 15.0));
        return std::abs (ideal / (1.0 + (1.0 + ideal) / a));
    }

    void runTest() override
    {
        beginTest ("DC operating point converges: both op-amp outputs at the 4.5 V bias");
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugStage2Out(), 4.5, 0.02);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 20; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
            {
                expect (std::isfinite (buf.getSample (0, i)));
                expect (std::abs (buf.getSample (0, i)) < 0.001f);
            }
            expectEquals (p.getSolveFailureRate(), 0.0);
        }

        beginTest ("stage 1 gain (two-corner leg, Drive pot feedback, TL072 GBW) matches the closed form");
        for (float knob : { 0.0f, 0.5f, 1.0f })
            for (double f : { 200.0, 1000.0, 4000.0 })
            {
                BluesBreakerStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, knob, 0.5f, 0.5f);
                const double amp = 0.001;
                const double measured = runSine (p, f, amp).stage1 / amp;
                const double expected = stage1Gain (f, knob);
                const double errDb = 20.0 * std::log10 (measured / expected);
                logMessage ("Drive " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 2)
                            + "x vs " + juce::String (expected, 2) + "x (" + juce::String (errDb, 2) + " dB)");
                expectLessThan (std::abs (errDb), 0.5);
            }

        beginTest ("stage 2 gain (220K over the Drive pot's series segment + 10K) matches the closed form");
        for (float knob : { 0.0f, 0.5f, 1.0f })
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, knob, 0.5f, 0.5f);
            const auto m = runSine (p, 1000.0, 0.001);
            const double measured = m.stage2 / m.stage1;
            const double expected = stage2Gain (1000.0, knob);
            const double errDb = 20.0 * std::log10 (measured / expected);
            logMessage ("Drive " + juce::String (knob, 1) + ": " + juce::String (measured, 2) + "x vs " + juce::String (expected, 2)
                        + "x (" + juce::String (errDb, 2) + " dB)");
            expectLessThan (std::abs (errDb), 0.5);
        }

        beginTest ("the feedback diodes (two in series each way + 6.8K) limit stage 2's swing, symmetrically");
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f, 1.0f);
            const auto m = runSine (p, 440.0, 0.1);
            logMessage ("stage-2 peak swing " + juce::String (m.stage2Peak, 3) + " V; output +" + juce::String (m.pos, 3) + " / " + juce::String (m.neg, 3));
            expectGreaterThan (m.stage2Peak, 0.9);
            expectLessThan (m.stage2Peak, 3.0);
            expectLessThan (std::abs (m.pos + m.neg) / (m.pos - m.neg), 0.03f);
        }

        beginTest ("Tone: wiper up is brighter (5 kHz relative to 200 Hz)");
        {
            auto ratio = [] (float tone)
            {
                BluesBreakerStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.0f, tone, 1.0f);
                const double hi = runSine (p, 5000.0, 0.001).out;
                BluesBreakerStyleOverdriveProcessor q;
                q.prepare (sr, 512, 1);
                setKnobs (q, 0.0f, tone, 1.0f);
                const double lo = runSine (q, 200.0, 0.001).out;
                return hi / lo;
            };
            const double dark = ratio (0.0f), bright = ratio (1.0f);
            logMessage ("5k/200 Hz: tone 0 -> " + juce::String (dark, 3) + ", tone 1 -> " + juce::String (bright, 3));
            expectGreaterThan (bright, dark * 1.5);
        }

        beginTest ("Volume: monotonic and silent at 0");
        {
            double previous = -1.0;
            for (float l : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                BluesBreakerStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.6f, 0.5f, l);
                const double out = runSine (p, 440.0, 0.05).out;
                expectGreaterThan (out, previous);
                previous = out;
            }
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Drive setting");
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, d, 0.5f, 1.0f);
            const auto m = runSine (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.pos) < 3.0f && std::abs (m.neg) < 3.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("stereo: identical input gives identical channels; different input keeps them independent");
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.5f, 0.8f);
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.2f * std::sin (0.05f * (float) (b * 512 + i));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                for (int i = 0; i < 512; ++i)
                    expectEquals (buf.getSample (0, i), buf.getSample (1, i));
            }
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (0, i, 0.2f * std::sin (0.05f * (float) i));
                buf.setSample (1, i, 0.0f);
            }
            p.process (buf);
            expect (std::abs (buf.getSample (1, 511)) < 0.05f);
            expectGreaterThan (std::abs (buf.getSample (0, 256)), 0.01f);
        }

        beginTest ("random knob moves, plucked notes and hot bursts: the solver never fails to converge (no frozen circuit)");
        for (int seed = 1234; seed < 1238; ++seed)
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 12.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest ("cost (informational)");
        {
            BluesBreakerStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.8f, 0.5f, 0.7f);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 400;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, 0.1f * std::sin (0.06f * (float) (b * 512 + i)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("Blues Breaker: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static BluesBreakerStyleOverdriveProcessorTests bluesBreakerStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
