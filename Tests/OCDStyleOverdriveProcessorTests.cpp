#include "Effects/OCDStyleOverdriveProcessor.h"
#include "Effects/PotTaper.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class OCDStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    OCDStyleOverdriveProcessorTests() : juce::UnitTest ("OCDStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;
    using P = OCDStyleOverdriveProcessor;

    // parameter order: Drive, Tone, Volume, Clipping, HP/LP
    static SineProbe run (P& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        return probeSine (p, { [&p] { return p.debugStage1Out(); }, [&p] { return p.debugClipNode() - 4.5; }, [&p] { return p.debugStage2Out(); } },
                          freq, amp, sr, warmup, measure);
    }

    /** Jack to stage 1's output: 22 nF -> 10K into (+) (470K to the bias); TL082 (A0 2e5, 3 MHz); (-) leg 2K2 + 68 nF;
        feedback = (18K + the Drive rheostat) || 220 pF. */
    static double stage1Gain (double f, double drive)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        const C vPlus = 470.0e3 / (470.0e3 + 10.0e3 + 1.0 / (s * 22.0e-9));
        const double rF = 18.0e3 + 1.0e6 * pots::audio (drive);
        const C zFb = 1.0 / (1.0 / rF + s * 220.0e-12);
        const C zLeg = 2.2e3 + 1.0 / (s * 68.0e-9);
        const C beta = zLeg / (zLeg + zFb);
        const C a = 2.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * (3.0e6 / 2.0e5)));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        beginTest ("DC operating point: both op-amp outputs and the clipper node at the 4.5 V bias");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("stage 1 " + juce::String (p.debugStage1Out(), 4) + " V, clipper node " + juce::String (p.debugClipNode(), 4)
                        + " V, stage 2 " + juce::String (p.debugStage2Out(), 4) + " V");
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugClipNode(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugStage2Out(), 4.5, 0.02);
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

        beginTest ("stage 1 gain (18K + Drive, 2K2 + 68 nF leg, TL082 gain-bandwidth) matches the closed form: up to ~460x");
        for (float knob : { 0.1f, 0.5f, 1.0f })
            for (double f : { 200.0, 1000.0, 4000.0 })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { knob, 0.5f, 0.5f, 0.0f, 1.0f });
                const double amp = knob < 0.5f ? 0.0005 : 0.00003;
                const double measured = run (p, f, amp).tap[0] / amp;
                const double expected = stage1Gain (f, knob);
                const double errDb = 20.0 * std::log10 (measured / expected);
                logMessage ("Drive " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 1)
                            + "x vs " + juce::String (expected, 1) + "x (" + juce::String (errDb, 2) + " dB)");
                expectLessThan (std::abs (errDb), 0.5);
            }

        beginTest ("stage 2 is x(1 + 150K/39K) = 4.85 above its 100 nF corner");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.3f, 0.5f, 0.5f, 0.0f, 1.0f });
            const auto m = run (p, 1000.0, 0.0003);
            const C s (0.0, 2.0 * juce::MathConstants<double>::pi * 1000.0);
            const C zLeg = 39.0e3 + 1.0 / (s * 100.0e-9);
            const C zFb = 1.0 / (1.0 / 150.0e3 + s * 220.0e-12);
            const double expected = std::abs (1.0 + zFb / zLeg);
            const double measured = m.tap[2] / m.tap[1];
            logMessage ("stage 2: " + juce::String (measured, 3) + "x vs " + juce::String (expected, 3) + "x");
            expectLessThan (std::abs (20.0 * std::log10 (measured / expected)), 0.6);
        }

        beginTest ("MOSFET clipping: the 2N7000 body diodes hold the node within ~0.8-1.1 V of the bias, symmetrically");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 0.5f, 0.5f, 0.0f, 1.0f });
            const auto m = run (p, 440.0, 0.1);
            logMessage ("clipper node vs bias: " + juce::String (m.tapMin[1], 3) + " .. +" + juce::String (m.tapMax[1], 3) + " V");
            // Since 2026-09-27 the body diode's Is is fit to the 2N7000 datasheet's own operating point (VSD <= 1.2 V
            // at 200 mA) extrapolated down to this circuit's ~mA range, not a small-signal-diode guess: ~1.0 V at
            // 1 mA (docs/circuits/OCDStyleOverdrive.md), not ~0.65-0.73 V.
            expectGreaterThan (m.tapMax[1], 0.8);
            expectLessThan (m.tapMax[1], 1.1);
            expectGreaterThan (-m.tapMin[1], 0.8);
            expectLessThan (-m.tapMin[1], 1.1);
            // the 2N7000 channels (not in the netlist) need 2 V across gate-source: they never turn on
            expectLessThan (std::max (m.tapMax[1], -m.tapMin[1]), 1.9);
        }

        beginTest ("LED clipping (the switch takes the 2N7000s out): the node reaches the LEDs' ~1.4-2.0 V");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 0.5f, 0.5f, 1.0f, 1.0f });
            const auto m = run (p, 440.0, 0.1);
            logMessage ("clipper node vs bias: " + juce::String (m.tapMin[1], 3) + " .. +" + juce::String (m.tapMax[1], 3) + " V");
            expectGreaterThan (m.tapMax[1], 1.4);
            expectLessThan (m.tapMax[1], 2.0);
            expectGreaterThan (-m.tapMin[1], 1.4);
            expectLessThan (-m.tapMin[1], 2.0);
        }

        beginTest ("Tone: the knob turns the treble down (5 kHz relative to 200 Hz)");
        {
            auto ratio = [] (float tone)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.1f, tone, 1.0f, 0.0f, 1.0f });
                const double hi = run (p, 5000.0, 0.0005).out;
                P q;
                q.prepare (sr, 512, 1);
                setParams (q, { 0.1f, tone, 1.0f, 0.0f, 1.0f });
                const double lo = run (q, 200.0, 0.0005).out;
                return hi / lo;
            };
            const double dark = ratio (0.0f), bright = ratio (1.0f);
            logMessage ("5k/200 Hz: Tone 0 -> " + juce::String (dark, 3) + ", Tone 1 -> " + juce::String (bright, 3));
            expectGreaterThan (bright, dark * 3.0);
        }

        beginTest ("HP/LP: high peak (R10 22K in parallel with 33K) is louder than low peak");
        {
            auto level = [] (float hp)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.1f, 0.5f, 1.0f, 0.0f, hp });
                return run (p, 1000.0, 0.0005).out;
            };
            const double low = level (0.0f), high = level (1.0f);
            logMessage ("1 kHz: low peak " + juce::String (low, 4) + " V, high peak " + juce::String (high, 4) + " V");
            expectGreaterThan (high, low * 1.05);
        }

        beginTest ("Volume: monotonic and silent at 0");
        {
            double previous = -1.0;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.5f, 0.5f, v, 0.0f, 1.0f });
                const double out = run (p, 440.0, 0.05).out;
                expectGreaterThan (out, previous);
                previous = out;
            }
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Drive setting, both clipping modes");
        for (float clip : { 0.0f, 1.0f })
            for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { d, 0.5f, 1.0f, clip, 1.0f });
                const auto m = run (p, 220.0, 0.5, 0.3, 0.1);
                expect (std::isfinite (m.out));
                expect (std::abs (m.outMax) < 3.0f && std::abs (m.outMin) < 3.0f);
                expectLessThan (p.getSolveFailureRate(), 0.001);
            }

        beginTest ("stereo: identical input gives identical channels; different input keeps them independent");
        {
            P p;
            p.prepare (sr, 512, 2);
            setParams (p, { 0.7f, 0.3f, 0.8f, 0.0f, 1.0f });
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

        beginTest ("random knob and switch moves, plucked notes and hot bursts: the solver never fails to converge (no frozen circuit)");
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
            setParams (p, { 0.8f, 0.3f, 0.7f, 0.0f, 1.0f });
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
            logMessage ("OCD: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static OCDStyleOverdriveProcessorTests ocdStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
