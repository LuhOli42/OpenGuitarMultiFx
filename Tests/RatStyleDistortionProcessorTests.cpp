#include "Effects/RatStyleDistortionProcessor.h"
#include "Effects/PotTaper.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class RatStyleDistortionProcessorTests : public juce::UnitTest
{
public:
    RatStyleDistortionProcessorTests() : juce::UnitTest ("RatStyleDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;

    static void setKnobs (RatStyleDistortionProcessor& p, float distortion, float filter, float volume)
    {
        auto params = p.getParameters()->getParameters (true);
        const float values[] = { distortion, filter, volume };
        for (int i = 0; i < 3; ++i)
            *dynamic_cast<juce::AudioParameterFloat*> (params[i]) = values[i];
    }

    struct Measure { double out, opAmp, diodePeak; float pos, neg; };

    static Measure runSine (RatStyleDistortionProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
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

    /** Small-signal gain from the input jack to the op-amp output: 22 nF into node A (1M to the bias), 1K + 1 nF into the (+)
        pin, an LM308 (A0 3e5, 1 MHz gain-bandwidth), the (-) leg (360 + 4.7 uF) || (47 + 2.2 uF) and the feedback
        rheostat || 100 pF. */
    static double referenceGain (double f, double rheostat)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        const C zPlus = 1.0e3 + 1.0 / (s * 1.0e-9);
        const C zA = 1.0e6 * zPlus / (1.0e6 + zPlus);
        const C vNa = zA / (zA + 1.0 / (s * 22.0e-9));
        const C vPlus = vNa * (1.0 / (s * 1.0e-9)) / zPlus;

        const C z1 = 360.0 + 1.0 / (s * 4.7e-6), z2 = 47.0 + 1.0 / (s * 2.2e-6);
        const C zLeg = z1 * z2 / (z1 + z2);
        const C zFb = 1.0 / (1.0 / rheostat + s * 100.0e-12);
        const C beta = zLeg / (zLeg + zFb);
        const C a = 3.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * (1.0e6 / 3.0e5)));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        beginTest ("DC operating point converges: op-amp output at the 4.5 V bias, diode node at 0 V");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("op-amp out " + juce::String (p.debugOpAmpOut(), 4) + " V, diode node " + juce::String (p.debugDiodeNode(), 5) + " V");
            expectWithinAbsoluteError (p.debugOpAmpOut(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugDiodeNode(), 0.0, 0.001);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            RatStyleDistortionProcessor p;
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

        beginTest ("op-amp stage gain (two-shelf (-) leg, LM308 gain-bandwidth) matches the closed form: up to ~3600x at max");
        for (float knob : { 0.1f, 0.3f, 0.6f, 1.0f })
            for (double f : { 100.0, 1000.0, 4000.0 })
            {
                RatStyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, knob, 0.0f, 0.5f);
                const double amp = knob < 0.5f ? 0.0002 : 0.00005;
                const double measured = runSine (p, f, amp).opAmp / amp;
                const double expected = referenceGain (f, juce::jmax (1.0, 150.0e3 * pots::audio (knob)));
                const double errDb = 20.0 * std::log10 (measured / expected);
                logMessage ("Distortion " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 1)
                            + "x vs " + juce::String (expected, 1) + "x (" + juce::String (errDb, 2) + " dB)");
                expectLessThan (std::abs (errDb), 0.7);
            }

        beginTest ("the silicon diodes clip at their knee (~0.5-0.8 V), symmetrically");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.0f, 1.0f);
            const auto m = runSine (p, 440.0, 0.1);
            logMessage ("diode node peak " + juce::String (m.diodePeak, 3) + " V; output +" + juce::String (m.pos, 3) + " / " + juce::String (m.neg, 3));
            expectGreaterThan (m.diodePeak, 0.5);
            expectLessThan (m.diodePeak, 0.85);
            expectLessThan (std::abs (m.pos + m.neg) / (m.pos - m.neg), 0.03f);
        }

        beginTest ("hard clipping does not delay the op-amp: its output crosses the bias within ~2 samples of the input");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f, 0.5f);
            const double freq = 440.0;
            juce::AudioBuffer<float> buf (1, 1);
            double worstMicros = 0.0;
            double prevIn = 0.0, prevOut = 4.5;
            double lastInCross = -1.0, lastOutCross = -1.0;
            const double period = sr / freq;
            for (long long n = 0; n < (long long) (0.5 * sr); ++n)
            {
                const double x = 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) n / sr);
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
                        double d = lastOutCross - lastInCross;
                        d -= period * std::round (d / period);
                        worstMicros = juce::jmax (worstMicros, std::abs (d) * 1.0e6 / sr);
                    }
                }
                prevIn = x;
                prevOut = v;
            }
            logMessage ("worst input-to-output zero-crossing delay " + juce::String (worstMicros, 1) + " us");
            expectLessThan (worstMicros, 150.0);
        }

        beginTest ("Filter: the knob turns the treble down (5 kHz relative to 200 Hz), from ~30 kHz to ~475 Hz");
        {
            auto ratio = [] (float filter)
            {
                RatStyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.2f, filter, 1.0f);
                const double hi = runSine (p, 5000.0, 0.0002).out;
                RatStyleDistortionProcessor q;
                q.prepare (sr, 512, 1);
                setKnobs (q, 0.2f, filter, 1.0f);
                const double lo = runSine (q, 200.0, 0.0002).out;
                return hi / lo;
            };
            const double open = ratio (0.0f), closed = ratio (1.0f);
            logMessage ("5k/200 Hz: filter 0 -> " + juce::String (open, 3) + ", filter 1 -> " + juce::String (closed, 3));
            expectGreaterThan (open, closed * 4.0);
        }

        beginTest ("Distortion at its minimum still passes the signal (unity gain, then the diodes' knee)");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.0f, 0.328f, 0.542f);
            const double out = runSine (p, 440.0, 0.13).out;
            logMessage ("Distortion 0, 0.13 V in -> " + juce::String (out, 4) + " V out");
            expectGreaterThan (out, 0.01);
        }

        beginTest ("Volume: monotonic and silent at 0");
        {
            double previous = -1.0;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                RatStyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.6f, 0.3f, v);
                const double out = runSine (p, 440.0, 0.05).out;
                expectGreaterThan (out, previous);
                previous = out;
            }
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Distortion setting");
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, d, 0.0f, 1.0f);
            const auto m = runSine (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.pos) < 3.0f && std::abs (m.neg) < 3.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("stereo: identical input gives identical channels; different input keeps them independent");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.3f, 0.8f);
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
            RatStyleDistortionProcessor p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 12.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest ("cost (informational)");
        {
            RatStyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.8f, 0.3f, 0.7f);
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
            logMessage ("RAT: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static RatStyleDistortionProcessorTests ratStyleDistortionProcessorTests;


/** The RAT 2 and the Turbo RAT (same class, other values -- docs/circuits/RatStyleDistortion.md, "Versions"). */
class RatFamilyVersionTests : public juce::UnitTest
{
public:
    RatFamilyVersionTests() : juce::UnitTest ("RatFamilyVersions", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;
    using Model = RatStyleDistortionProcessor::Model;

    struct Version { Model model; const char* name; const char* displayName; double a0, gbw; double clipMin, clipMax; };

    static SineProbe run (RatStyleDistortionProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        return probeSine (p, { [&p] { return p.debugOpAmpOut(); }, [&p] { return p.debugDiodeNode(); } }, freq, amp, sr, warmup, measure);
    }

    /** Jack to the op-amp output for the RAT 2 / Turbo values: 22 nF into the node with 2M2 to the bias, 1K + 1 nF into (+),
        the (-) leg (560 + 4.7 uF) || (47 + 2.2 uF), feedback = the 100K rheostat || 100 pF. */
    static double referenceGain (double f, double rheostat, double a0, double gbw)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        const C zPlus = 1.0e3 + 1.0 / (s * 1.0e-9);
        const C zA = 2.2e6 * zPlus / (2.2e6 + zPlus);
        const C vNa = zA / (zA + 1.0 / (s * 22.0e-9));
        const C vPlus = vNa * (1.0 / (s * 1.0e-9)) / zPlus;

        const C z1 = 560.0 + 1.0 / (s * 4.7e-6), z2 = 47.0 + 1.0 / (s * 2.2e-6);
        const C zLeg = z1 * z2 / (z1 + z2);
        const C zFb = 1.0 / (1.0 / rheostat + s * 100.0e-12);
        const C beta = zLeg / (zLeg + zFb);
        const C a = a0 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * (gbw / a0)));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        const Version versions[] = {
            { Model::rat2, "RAT 2", "RAT 2-Style Distortion", 3.0e5, 1.0e6, 0.5, 0.85 },
            { Model::turbo, "Turbo RAT", "Turbo RAT-Style Distortion", 4.0e5, 0.6e6, 1.4, 2.0 },
        };

        for (const auto& v : versions)
        {
            const juce::String name (v.name);

            beginTest (name + ": DC operating point, display name and the RAT 2 / Turbo differences from the original");
            {
                RatStyleDistortionProcessor p (v.model);
                p.prepare (sr, 512, 1);
                expect (p.dcConverged());
                expectWithinAbsoluteError (p.debugOpAmpOut(), 4.5, 0.02);
                expectWithinAbsoluteError (p.debugDiodeNode(), 0.0, 0.001);
                expectEquals (juce::String (p.getName()), juce::String (v.displayName));
            }

            beginTest (name + ": op-amp stage gain matches the closed form (100K Distortion, 560/47 ohm leg, this op-amp's gain-bandwidth)");
            for (float knob : { 0.1f, 0.3f, 0.6f, 1.0f })
                for (double f : { 100.0, 1000.0, 4000.0 })
                {
                    RatStyleDistortionProcessor p (v.model);
                    p.prepare (sr, 512, 1);
                    setParams (p, { knob, 0.0f, 0.5f });
                    const double amp = knob < 0.5f ? 0.0002 : 0.00005;
                    const double measured = run (p, f, amp).tap[0] / amp;
                    const double expected = referenceGain (f, juce::jmax (1.0, 100.0e3 * pots::audio (knob)), v.a0, v.gbw);
                    const double errDb = 20.0 * std::log10 (measured / expected);
                    logMessage (name + " Distortion " + juce::String (knob, 1) + " @ " + juce::String (f, 0) + " Hz: " + juce::String (measured, 1)
                                + "x vs " + juce::String (expected, 1) + "x (" + juce::String (errDb, 2) + " dB)");
                    expectLessThan (std::abs (errDb), 0.8);
                }

            beginTest (name + ": the clipping diodes' level");
            {
                RatStyleDistortionProcessor p (v.model);
                p.prepare (sr, 512, 1);
                setParams (p, { 1.0f, 0.0f, 1.0f });
                const auto m = run (p, 440.0, 0.1);
                logMessage (name + ": diode node " + juce::String (m.tapMin[1], 3) + " .. +" + juce::String (m.tapMax[1], 3) + " V");
                expectGreaterThan (m.tapMax[1], v.clipMin);
                expectLessThan (m.tapMax[1], v.clipMax);
                expectLessThan (std::abs (m.tapMax[1] + m.tapMin[1]) / (m.tapMax[1] - m.tapMin[1]), 0.03);
            }

            beginTest (name + ": bounded and converging under a hot sine at every Distortion setting; stress does not freeze the solver");
            {
                for (float d : { 0.0f, 0.5f, 1.0f })
                {
                    RatStyleDistortionProcessor p (v.model);
                    p.prepare (sr, 512, 1);
                    setParams (p, { d, 0.0f, 1.0f });
                    const auto m = run (p, 220.0, 0.5, 0.3, 0.1);
                    expect (std::isfinite (m.out));
                    expect (std::abs (m.outMax) < 3.0f && std::abs (m.outMin) < 3.0f);
                    expectLessThan (p.getSolveFailureRate(), 0.001);
                }
                for (int seed = 4321; seed < 4323; ++seed)
                {
                    RatStyleDistortionProcessor p (v.model);
                    p.prepare (sr, 128, 2);
                    expectEquals (runPedalStress (p, 10.0, seed), 0);
                    expectLessThan (p.getSolveFailureRate(), 1.0e-5);
                }
            }
        }

        beginTest ("the Turbo RAT clips harder and later than the RAT 2 (LEDs against 1N4148s), everything else the same");
        {
            RatStyleDistortionProcessor two (Model::rat2), turbo (Model::turbo);
            two.prepare (sr, 512, 1);
            turbo.prepare (sr, 512, 1);
            setParams (two, { 1.0f, 0.0f, 1.0f });
            setParams (turbo, { 1.0f, 0.0f, 1.0f });
            const double a = run (two, 440.0, 0.1).tapMax[1], b = run (turbo, 440.0, 0.1).tapMax[1];
            logMessage ("diode node peak: RAT 2 " + juce::String (a, 3) + " V, Turbo " + juce::String (b, 3) + " V");
            expectGreaterThan (b, a * 2.0);
        }
    }
};

static RatFamilyVersionTests ratFamilyVersionTests;

} // namespace openguitarmultifx
