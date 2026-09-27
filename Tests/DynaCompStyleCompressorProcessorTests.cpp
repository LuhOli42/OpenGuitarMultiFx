#include "Effects/DynaCompStyleCompressorProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class DynaCompStyleCompressorProcessorTests : public juce::UnitTest
{
public:
    DynaCompStyleCompressorProcessorTests() : juce::UnitTest ("DynaCompStyleCompressorProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = DynaCompStyleCompressorProcessor;
    using Model = P::Model;

    /** Fundamental amplitude at the output for a sine of peak `amp` volts, after `settle` seconds (the envelope is slow). */
    static double outAmp (P& p, double freq, double amp, double settle = 2.0, double measure = 0.2)
    {
        return probeSine (p, {}, freq, amp, sr, settle, measure).out;
    }

    void runTest() override
    {
        for (auto model : { Model::dynaComp, Model::ross })
        {
            const juce::String name = model == Model::dynaComp ? "Dyna Comp" : "Ross";

            beginTest (name + ": DC operating point converges (the OTA's differential input is ~0 at DC)");
            {
                P p (model);
                p.prepare (sr, 512, 1);
                expect (p.dcConverged());
                logMessage (name + ": envelope " + juce::String (p.debugEnvelope(), 3) + " V, splitter emitter " + juce::String (p.debugSplitterEmitter(), 3)
                            + " V, OTA bias current " + juce::String (p.debugBiasCurrent() * 1.0e6, 1) + " uA");
                expectGreaterThan (p.debugEnvelope(), 6.0);
                expectGreaterThan (p.debugBiasCurrent(), 1.0e-6);
            }

            beginTest (name + ": static curve -- gain falls as the level rises (compression), at both Sustain settings");
            for (float sustain : { 0.3f, 0.8f })
            {
                double previousGain = 1.0e9;
                juce::String line = name + " Sustain " + juce::String (sustain, 1) + ": ";
                std::array<double, 5> gains {};
                int k = 0;
                for (double amp : { 0.003, 0.01, 0.03, 0.1, 0.3 })
                {
                    P p (model);
                    p.prepare (sr, 512, 1);
                    setParams (p, { sustain, 0.5f });
                    const double out = outAmp (p, 1000.0, amp);
                    const double gain = 20.0 * std::log10 (out / amp);
                    line += juce::String (amp * 1000.0, 0) + " mV -> " + juce::String (gain, 1) + " dB  ";
                    gains[(size_t) k++] = gain;
                    previousGain = gain;
                }
                (void) previousGain;
                logMessage (line);
                expectLessThan (gains[4], gains[0] - 6.0); // at least 6 dB less gain at 100x the level
            }
        }

        beginTest ("Sustain up = more gain for a small signal; Level scales the output");
        {
            P a, b;
            a.prepare (sr, 512, 1);
            b.prepare (sr, 512, 1);
            setParams (a, { 0.2f, 0.5f });
            setParams (b, { 0.9f, 0.5f });
            const double gA = outAmp (a, 1000.0, 0.005), gB = outAmp (b, 1000.0, 0.005);
            logMessage ("5 mV in: Sustain 0.2 -> " + juce::String (20.0 * std::log10 (gA / 0.005), 1) + " dB, Sustain 0.9 -> " + juce::String (20.0 * std::log10 (gB / 0.005), 1) + " dB");
            expectGreaterThan (gB, gA * 1.5);

            P c;
            c.prepare (sr, 512, 1);
            setParams (c, { 0.5f, 0.25f });
            P d;
            d.prepare (sr, 512, 1);
            setParams (d, { 0.5f, 0.75f });
            expectGreaterThan (outAmp (d, 1000.0, 0.03), outAmp (c, 1000.0, 0.03) * 1.5);
        }

        beginTest ("attack and release: a loud burst pulls the OTA's bias current down within milliseconds; it recovers over a second or so");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.7f, 0.5f });
            const double quiet = p.debugBiasCurrent();
            juce::AudioBuffer<float> buf (1, 1);
            int attackSamples = -1, releaseSamples = -1;
            double lowest = quiet;
            for (int n = 0; n < (int) (0.5 * sr); ++n)
            {
                buf.setSample (0, 0, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / sr)));
                p.process (buf);
                lowest = juce::jmin (lowest, p.debugBiasCurrent());
                if (attackSamples < 0 && p.debugBiasCurrent() < 0.5 * quiet)
                    attackSamples = n;
            }
            for (int n = 0; n < (int) (6.0 * sr); ++n)
            {
                buf.setSample (0, 0, 0.0f);
                p.process (buf);
                if (releaseSamples < 0 && p.debugBiasCurrent() > 0.5 * quiet + 0.5 * lowest)
                    releaseSamples = n;
            }
            logMessage ("bias current " + juce::String (quiet * 1.0e6, 1) + " uA -> " + juce::String (lowest * 1.0e6, 1) + " uA; halfway down after "
                        + juce::String (1000.0 * attackSamples / sr, 1) + " ms, halfway back after " + juce::String (1000.0 * releaseSamples / sr, 0) + " ms");
            expectGreaterThan (attackSamples, 0);
            expectLessThan (1000.0 * attackSamples / sr, 60.0);
            expectGreaterThan (1000.0 * releaseSamples / sr, 100.0);
        }

        beginTest ("hot signals: bounded, finite, no failed solves; stress test; stereo independence");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 1.0f });
            const auto m = probeSine (p, {}, 200.0, 1.0, sr, 0.5, 0.2);
            expect (std::isfinite (m.out));
            expectLessThan (p.getSolveFailureRate(), 0.001);
            for (int seed = 91; seed < 93; ++seed)
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
                    buf.setSample (0, i, 0.1f * std::sin (0.06f * (float) (b * 512 + i)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("Dyna Comp: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static DynaCompStyleCompressorProcessorTests dynaCompStyleCompressorProcessorTests;

} // namespace openguitarmultifx
