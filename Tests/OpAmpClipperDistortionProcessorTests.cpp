#include "Effects/OpAmpClipperDistortionProcessor.h"
#include "Effects/PotTaper.h"

#include "PedalStress.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class OpAmpClipperDistortionProcessorTests : public juce::UnitTest
{
public:
    OpAmpClipperDistortionProcessorTests() : juce::UnitTest ("OpAmpClipperDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    using Model = OpAmpClipperDistortionProcessor::Model;

    static void setKnobs (OpAmpClipperDistortionProcessor& p, float distortion, float output)
    {
        // the two knobs are the group's first and second parameter, whatever the model's ids
        auto params = p.getParameters()->getParameters (true);
        *dynamic_cast<juce::AudioParameterFloat*> (params[0]) = distortion;
        *dynamic_cast<juce::AudioParameterFloat*> (params[1]) = output;
    }

    struct Measure { double out, opAmp, diodePeak; float pos, neg; };

    static Measure runSine (OpAmpClipperDistortionProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        juce::AudioBuffer<float> buf (1, 1);
        double oS = 0, oC = 0, aS = 0, aC = 0, diodePeak = 0;
        float pos = 0, neg = 0;

        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            buf.setSample (0, 0, (float) (amp * std::sin (ph)));
            p.process (buf);
            if (n >= start)
            {
                const double y = buf.getSample (0, 0);
                const double a = p.debugOpAmpOut();
                oS += y * std::sin (ph); oC += y * std::cos (ph);
                aS += a * std::sin (ph); aC += a * std::cos (ph);
                diodePeak = juce::jmax (diodePeak, std::abs (p.debugDiodeNode()));
                pos = juce::jmax (pos, (float) y);
                neg = juce::jmin (neg, (float) y);
            }
        }

        const double k = 2.0 / (double) len;
        return { k * std::sqrt (oS * oS + oC * oC), k * std::sqrt (aS * aS + aC * aC), diodePeak, pos, neg };
    }

    /** Small-signal gain from the input jack to the op-amp output, from the model's own component values: C2 + R1 into the
        (+) pin with its bias resistor, a 741 (A0 = 2e5, GBW = 1 MHz) with the feedback resistor (and cap) and the (-) leg
        (cap + fixed resistor + rheostat). */
    static double referenceOpAmpGain (const OpAmpClipperDistortionProcessor::ModelSpec& m, double f, double rheostat)
    {
        using C = std::complex<double>;
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        const C vPlus = m.plusBiasOhms / (m.plusBiasOhms + m.inputSeriesOhms + 1.0 / (s * m.inputCapFarads));

        const C zLeg = 1.0 / (s * m.legCapFarads) + m.legOhms + rheostat;
        const C zFb = m.feedbackCapFarads > 0.0 ? 1.0 / (1.0 / m.feedbackOhms + s * m.feedbackCapFarads) : C (m.feedbackOhms);
        const C beta = zLeg / (zLeg + zFb);            // fraction of the output fed back to (-)
        const C a = 2.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * 5.0)); // 741, dominant pole 5 Hz
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        runModel (Model::distortionPlus);
        runModel (Model::dod250);
    }

    void runModel (Model model)
    {
        const auto& spec = OpAmpClipperDistortionProcessor::specFor (model);
        const juce::String tag = juce::String (spec.displayName) + ": ";
        beginTest (tag + "DC operating point converges: op-amp output and (-) input at the 4.5 V bias, diode node at 0 V");
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("op-amp out " + juce::String (p.debugOpAmpOut(), 4) + " V, (-) " + juce::String (p.debugInverting(), 4)
                        + " V, diode node " + juce::String (p.debugDiodeNode(), 5) + " V");
            expectWithinAbsoluteError (p.debugOpAmpOut(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugInverting(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugDiodeNode(), 0.0, 0.001);
        }

        beginTest (tag + "silence in: finite, settled near silence, no solver failures");
        {
            OpAmpClipperDistortionProcessor p (model);
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

        beginTest (tag + "op-amp stage gain: min (~2x), max (~213x ideal) and its 741 bandwidth match the closed-form reference");
        {
            // Reference = 1 + R4/Z(leg) with the 741's single pole and the input filters (referenceOpAmpGain). The fixed
            // ideal figures: min 1 + 1M/(1M + 4.7K) = 1.995, max 1 + 1M/4.7K = 213.8 (ElectroSmash quotes 1.5 for the
            // min, which is not what that arithmetic gives).
            for (float knob : { 0.0f, 0.5f, 0.8f, 1.0f })
            {
                const double rheostat = juce::jmax (1.0, spec.distortionPotOhms * pots::audio (1.0 - (double) knob));
                for (double f : { 100.0, 1000.0, 4000.0 })
                {
                    OpAmpClipperDistortionProcessor p (model);
                    p.prepare (sr, 512, 1);
                    setKnobs (p, knob, 0.5f);
                    const double amp = 0.001;
                    const double measured = runSine (p, f, amp).opAmp / amp;
                    const double expected = referenceOpAmpGain (spec, f, rheostat);
                    const double errDb = 20.0 * std::log10 (measured / expected);
                    logMessage ("Distortion " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 2)
                                + "x vs reference " + juce::String (expected, 2) + "x (" + juce::String (errDb, 2) + " dB)");
                    expectLessThan (std::abs (errDb), 0.6);
                }
            }
        }

        beginTest (tag + "the diodes clip at their knee, symmetrically");
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 1.0f);
            const auto m = runSine (p, 440.0, 0.1);
            logMessage ("diode node peak " + juce::String (m.diodePeak, 3) + " V; output +" + juce::String (m.pos, 3) + " / " + juce::String (m.neg, 3));
            // Distortion+: ElectroSmash's 700-800 mVpp; DOD 250: silicon 1N4148 through 10K into 100K, ~0.55-0.75 V
            expectGreaterThan (m.diodePeak, model == Model::distortionPlus ? 0.33 : 0.5);
            expectLessThan (m.diodePeak, model == Model::distortionPlus ? 0.42 : 0.8);
            expectLessThan (std::abs (m.pos + m.neg) / (m.pos - m.neg), 0.02f);
        }

        beginTest (tag + "the 741 runs out of swing inside its rails (1.5 .. 7.5 V) when hard driven");
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f);
            double lo = 100.0, hi = -100.0;
            juce::AudioBuffer<float> buf (1, 1);
            for (int n = 0; n < (int) (0.3 * sr); ++n)
            {
                buf.setSample (0, 0, (float) (1.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * n / sr)));
                p.process (buf);
                lo = juce::jmin (lo, p.debugOpAmpOut());
                hi = juce::jmax (hi, p.debugOpAmpOut());
            }
            logMessage ("op-amp out range " + juce::String (lo, 3) + " .. " + juce::String (hi, 3) + " V");
            expectGreaterThan (lo, 1.4);
            expectLessThan (hi, 7.6);
            expectGreaterThan (hi - lo, 4.0); // and it does reach them
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest (tag + "hard clipping does not delay the op-amp: its output crosses the bias within ~2 samples of the input");
        {
            // A 741 (or its macro-model) that is driven far into its rails must leave them as soon as the input reverses.
            // A model whose internal integrator keeps charging while the output is railed comes out late by a large
            // part of a cycle (the (-)/(+) error is ~1 V x an open-loop gain of 200 000).
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f);
            const double freq = 440.0;
            juce::AudioBuffer<float> buf (1, 1);
            double worstMicros = 0.0;
            double prevIn = 0.0, prevOut = 4.5;
            double lastInCross = -1.0, lastOutCross = -1.0;
            const double period = sr / freq;
            for (long long n = 0; n < (long long) (0.5 * sr); ++n)
            {
                const double x = std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) n / sr);
                buf.setSample (0, 0, (float) x);
                p.process (buf);
                const double v = p.debugOpAmpOut();
                if (n > (long long) (0.3 * sr))
                {
                    if (prevIn < 0.0 && x >= 0.0)
                        lastInCross = (double) n;
                    if (prevOut < 4.5 && v >= 4.5)
                        lastOutCross = (double) n;
                    if (lastInCross >= 0.0 && lastOutCross >= 0.0)
                    {
                        double d = lastOutCross - lastInCross; // samples, wrapped to +-half a period
                        d -= period * std::round (d / period);
                        worstMicros = juce::jmax (worstMicros, std::abs (d) * 1.0e6 / sr);
                    }
                }
                prevIn = x;
                prevOut = v;
            }
            logMessage ("worst input-to-output zero-crossing delay " + juce::String (worstMicros, 1) + " us");
            expectLessThan (worstMicros, 150.0); // ~ 1 sample at 48 kHz plus the 741's own 5 kHz bandwidth at gain 200
        }

        beginTest (tag + "Output pot: monotonic, silent at 0, and the loudest position is the top");
        {
            double previous = -1.0;
            for (float o : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                OpAmpClipperDistortionProcessor p (model);
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.6f, o);
                const double level = runSine (p, 440.0, 0.1).out;
                logMessage ("Output " + juce::String (o, 2) + ": " + juce::String (level, 4));
                expectGreaterThan (level, previous);
                previous = level;
                if (o == 0.0f)
                    expectLessThan (level, 0.001);
            }
        }

        beginTest (tag + "stays finite/bounded and converges under a hot sine, every Distortion setting");
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            setKnobs (p, d, 1.0f);
            const auto m = runSine (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.pos) < 2.0f && std::abs (m.neg) < 2.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest (tag + "stereo: identical input gives identical channels; different input keeps them independent");
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.8f);
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

        beginTest (tag + "random knob moves, plucked notes and hot bursts: the solver never fails to converge (no frozen circuit)");
        for (int seed = 1234; seed < 1238; ++seed)
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 12.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest (tag + "cost (informational)");
        {
            OpAmpClipperDistortionProcessor p (model);
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.8f, 0.7f);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 400;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, 0.3f * std::sin (0.06f * (float) (b * 512 + i)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage (juce::String (spec.displayName) + ": " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static OpAmpClipperDistortionProcessorTests distortionPlusStyleDistortionProcessorTests;

} // namespace openguitarmultifx
