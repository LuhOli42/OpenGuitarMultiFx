#include "Effects/CrunchBoxStyleDistortionProcessor.h"
#include "Effects/PotTaper.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class CrunchBoxStyleDistortionProcessorTests : public juce::UnitTest
{
public:
    CrunchBoxStyleDistortionProcessorTests() : juce::UnitTest ("CrunchBoxStyleDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;
    using P = CrunchBoxStyleDistortionProcessor;

    static SineProbe run (P& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        return probeSine (p, { [&p] { return p.debugStage1Out(); }, [&p] { return p.debugStage2Out(); }, [&p] { return p.debugLedNode(); } },
                          freq, amp, sr, warmup, measure);
    }

    /** Stage 1, jack to op-amp output: 22 nF -> 1K into (+) (1M to the bias, 1 nF to ground); LM833 (A0 3.16e5, 15 MHz);
        (-) leg 1K + 0.22 uF; feedback = the Drive segment || 100 pF. */
    static double stage1Gain (double f, double rFeedback)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        // 22 nF -> 1K -> the (+) node, which has 1M and 1 nF to ground: a series 22 nF + 1K into 1M || 1 nF
        const C zShunt = 1.0 / (1.0 / 1.0e6 + s * 1.0e-9);
        const C vPlus = zShunt / (zShunt + 1.0 / (s * 22.0e-9) + 1.0e3);
        const C zLeg = 1.0e3 + 1.0 / (s * 0.22e-6);
        const C zFb = 1.0 / (1.0 / rFeedback + s * 100.0e-12);
        const C beta = zLeg / (zLeg + zFb);
        const C a = 3.16e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * (15.0e6 / 3.16e5)));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        beginTest ("DC operating point: both op-amp outputs at the 4.5 V bias, the LED node at 0 V");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("stage 1 " + juce::String (p.debugStage1Out(), 4) + " V, stage 2 " + juce::String (p.debugStage2Out(), 4)
                        + " V, LED node " + juce::String (p.debugLedNode(), 5) + " V");
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugStage2Out(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugLedNode(), 0.0, 0.001);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            P p;
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

        beginTest ("stage 1 gain (Drive as the feedback, (-) leg 1K + 0.22 uF, LM833) matches the closed form");
        for (float knob : { 0.1f, 0.5f, 1.0f })
            for (double f : { 200.0, 1000.0, 4000.0 })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { knob, 0.5f, 0.5f });
                const double amp = 0.0005;
                const double measured = run (p, f, amp).tap[0] / amp;
                const double expected = stage1Gain (f, juce::jmax (1.0, 100.0e3 * knob));
                const double errDb = 20.0 * std::log10 (measured / expected);
                logMessage ("Drive " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 2)
                            + "x vs " + juce::String (expected, 2) + "x (" + juce::String (errDb, 2) + " dB)");
                expectLessThan (std::abs (errDb), 0.3);
            }

        beginTest ("stage 2 is inverting: -(1M || 100 pF) / (Drive series + 10K + 0.1 uF), up to x100");
        for (float knob : { 0.0f, 0.5f, 1.0f })
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { knob, 0.5f, 0.5f });
            const double f = 300.0;
            const auto m = run (p, f, 0.0002);
            const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
            const double rSeries = 37.0 + juce::jmax (1.0, 100.0e3 - juce::jmax (1.0, 100.0e3 * knob)) + 10.0e3;
            const C zIn = rSeries + 1.0 / (s * 0.1e-6);
            const C zFb = 1.0 / (1.0 / 1.0e6 + s * 100.0e-12);
            const double expected = std::abs (zFb / zIn);
            const double measured = m.tap[1] / m.tap[0];
            const double errDb = 20.0 * std::log10 (measured / expected);
            logMessage ("Drive " + juce::String (knob, 1) + ": stage 2 " + juce::String (measured, 2) + "x vs " + juce::String (expected, 2)
                        + "x (" + juce::String (errDb, 2) + " dB)");
            expectLessThan (std::abs (errDb), 0.4);
        }

        beginTest ("the two red LEDs clip at their knee (~1.4-2.0 V), symmetrically");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 1.0f, 1.0f });
            const auto m = run (p, 440.0, 0.1);
            logMessage ("LED node " + juce::String (m.tapMin[2], 3) + " .. " + juce::String (m.tapMax[2], 3) + " V");
            expectGreaterThan (m.tapMax[2], 1.4);
            expectLessThan (m.tapMax[2], 2.0);
            expectLessThan (std::abs (m.tapMax[2] + m.tapMin[2]) / (m.tapMax[2] - m.tapMin[2]), 0.03);
        }

        beginTest ("Tone: the knob turns the treble down (5 kHz relative to 200 Hz)");
        {
            auto ratio = [] (float tone)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.1f, tone, 1.0f });
                const double hi = run (p, 5000.0, 0.001).out;
                P q;
                q.prepare (sr, 512, 1);
                setParams (q, { 0.1f, tone, 1.0f });
                const double lo = run (q, 200.0, 0.001).out;
                return hi / lo;
            };
            const double dark = ratio (0.0f), bright = ratio (1.0f);
            logMessage ("5k/200 Hz: Tone 0 -> " + juce::String (dark, 3) + ", Tone 1 -> " + juce::String (bright, 3));
            expectGreaterThan (bright, dark * 2.0);
        }

        beginTest ("Volume: monotonic and silent at 0");
        {
            double previous = -1.0;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.5f, 0.5f, v });
                const double out = run (p, 440.0, 0.05).out;
                expectGreaterThan (out, previous);
                previous = out;
            }
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Drive setting");
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { d, 0.5f, 1.0f });
            const auto m = run (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.outMax) < 3.0f && std::abs (m.outMin) < 3.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("stereo: identical input gives identical channels; different input keeps them independent");
        {
            P p;
            p.prepare (sr, 512, 2);
            setParams (p, { 0.7f, 0.3f, 0.8f });
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
            // channel 1 starts from channel 0's state, so it rings for a few blocks (this pedal's gain is ~x1000): give it 10
            for (int b = 0; b < 10; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    buf.setSample (0, i, 0.2f * std::sin (0.05f * (float) (b * 512 + i)));
                    buf.setSample (1, i, 0.0f);
                }
                p.process (buf);
            }
            // (the 2.2 uF coupling cap into the Volume pot leaves a slowly decaying offset: compare with channel 0, not with zero)
            float peak0 = 0.0f, peak1 = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                peak0 = juce::jmax (peak0, std::abs (buf.getSample (0, i)));
                peak1 = juce::jmax (peak1, std::abs (buf.getSample (1, i)));
            }
            logMessage ("last block: channel 0 peak " + juce::String (peak0, 3) + ", channel 1 (silent input) peak " + juce::String (peak1, 3));
            expectLessThan (peak1, 0.25f * peak0);
            expectGreaterThan (peak0, 0.01f);
        }

        beginTest ("random knob moves, plucked notes and hot bursts: the solver never fails to converge (no frozen circuit)");
        for (int seed = 1234; seed < 1238; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 12.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest ("cost (informational)");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.8f, 0.3f, 0.7f });
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
            logMessage ("Crunch Box: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static CrunchBoxStyleDistortionProcessorTests crunchBoxStyleDistortionProcessorTests;

} // namespace openguitarmultifx
