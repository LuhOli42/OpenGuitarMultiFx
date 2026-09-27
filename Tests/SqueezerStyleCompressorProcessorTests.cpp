#include "Effects/SqueezerStyleCompressorProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class SqueezerStyleCompressorProcessorTests : public juce::UnitTest
{
public:
    SqueezerStyleCompressorProcessorTests() : juce::UnitTest ("SqueezerStyleCompressorProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = SqueezerStyleCompressorProcessor;

    static double outAmp (P& p, double freq, double amp, double settle = 1.5, double measure = 0.2)
    {
        return probeSine (p, {}, freq, amp, sr, settle, measure).out;
    }

    void runTest() override
    {
        beginTest ("DC operating point: the op-amp output at the bias divider's 4.9 V, the gate control at ~0 V");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("op-amp " + juce::String (p.debugOpAmpOut(), 3) + " V, control " + juce::String (p.debugGateControl(), 4) + " V");
            expectWithinAbsoluteError (p.debugOpAmpOut(), 9.0 * 470.0 / 860.0, 0.1);
            expectWithinAbsoluteError (p.debugGateControl(), 0.0, 0.05);
        }

        beginTest ("static curve: the make-up stage is x23 (27 dB) until the JFET starts to compress, then the gain falls");
        for (float bias : { 0.5f, 0.56f })
        {
            juce::String line = "Bias " + juce::String (bias, 2) + ": ";
            std::array<double, 5> gains {};
            int k = 0;
            for (double amp : { 0.002, 0.008, 0.03, 0.1, 0.3 })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 1.0f, bias });
                const double out = outAmp (p, 1000.0, amp);
                gains[(size_t) k] = 20.0 * std::log10 (out / amp);
                line += juce::String (amp * 1000.0, 0) + " mV -> " + juce::String (gains[(size_t) k], 1) + " dB  ";
                ++k;
            }
            logMessage (line);
            expectLessThan (gains[4], gains[0] - 6.0);
        }

        beginTest ("attack ~6 ms and release ~200-470 ms (DryBell): the gate control follows a loud burst quickly and lets go slowly");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 0.5f });
            juce::AudioBuffer<float> buf (1, 1);
            double peak = 0.0;
            int attackSamples = -1;
            for (int n = 0; n < (int) (0.5 * sr); ++n)
            {
                buf.setSample (0, 0, (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / sr)));
                p.process (buf);
                peak = juce::jmax (peak, p.debugGateControl());
            }
            // re-run to time the attack: fresh instance, time to 63% of the final control voltage
            P q;
            q.prepare (sr, 512, 1);
            setParams (q, { 1.0f, 0.5f });
            double level = 0.0;
            for (int n = 0; n < (int) (0.5 * sr); ++n)
            {
                buf.setSample (0, 0, (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / sr)));
                q.process (buf);
                level = q.debugGateControl();
                if (attackSamples < 0 && level > 0.63 * peak)
                    attackSamples = n;
            }
            int releaseSamples = -1;
            const double top = q.debugGateControl();
            for (int n = 0; n < (int) (4.0 * sr); ++n)
            {
                buf.setSample (0, 0, 0.0f);
                q.process (buf);
                if (releaseSamples < 0 && q.debugGateControl() < 0.37 * top)
                    releaseSamples = n;
            }
            logMessage ("gate control peaks at " + juce::String (peak, 3) + " V; 63% after " + juce::String (1000.0 * attackSamples / sr, 1) + " ms; falls to 37% in "
                        + juce::String (1000.0 * releaseSamples / sr, 0) + " ms");
            expectGreaterThan (attackSamples, 0);
            expectLessThan (1000.0 * attackSamples / sr, 60.0);
            expectGreaterThan (1000.0 * releaseSamples / sr, 100.0);
            expectLessThan (1000.0 * releaseSamples / sr, 1200.0);
        }

        beginTest ("Volume scales the output; Bias moves the compression threshold (a higher bias compresses less at a fixed level)");
        {
            P a;
            a.prepare (sr, 512, 1);
            setParams (a, { 0.4f, 0.5f });
            P b;
            b.prepare (sr, 512, 1);
            setParams (b, { 0.9f, 0.5f });
            expectGreaterThan (outAmp (b, 1000.0, 0.02), outAmp (a, 1000.0, 0.02) * 1.5);

            P low;
            low.prepare (sr, 512, 1);
            setParams (low, { 1.0f, 0.25f });
            P high;
            high.prepare (sr, 512, 1);
            setParams (high, { 1.0f, 0.6f });
            const double gLow = outAmp (low, 1000.0, 0.05) / 0.05, gHigh = outAmp (high, 1000.0, 0.05) / 0.05;
            logMessage ("50 mV in: Bias 0.25 -> " + juce::String (20.0 * std::log10 (gLow), 1) + " dB, Bias 0.6 -> " + juce::String (20.0 * std::log10 (gHigh), 1) + " dB");
            expectGreaterThan (gHigh, gLow);
        }

        beginTest ("hot input: bounded and finite, no failed solves; stress test");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 0.5f });
            const auto m = probeSine (p, {}, 300.0, 1.0, sr, 0.5, 0.2);
            expect (std::isfinite (m.out));
            expectLessThan (p.getSolveFailureRate(), 0.001);
            for (int seed = 5; seed < 7; ++seed)
            {
                P q;
                q.prepare (sr, 128, 2);
                expectEquals (runPedalStress (q, 10.0, seed), 0);
                expectLessThan (q.getSolveFailureRate(), 1.0e-5);
            }
        }

        beginTest ("cost (informational)");
        {
            P p;
            p.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 400;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, 0.05f * std::sin (0.06f * (float) (b * 512 + i)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("Orange Squeezer: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static SqueezerStyleCompressorProcessorTests squeezerStyleCompressorProcessorTests;

} // namespace openguitarmultifx
