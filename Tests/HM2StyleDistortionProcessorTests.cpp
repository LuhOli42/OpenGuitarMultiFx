#include "Effects/HM2StyleDistortionProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class HM2StyleDistortionProcessorTests : public juce::UnitTest
{
public:
    HM2StyleDistortionProcessorTests() : juce::UnitTest ("HM2StyleDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (HM2StyleDistortionProcessor& p, float dist, float low, float high, float level)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
            {
                if (f->paramID == "hm2_dist") *f = dist;
                if (f->paramID == "hm2_low") *f = low;
                if (f->paramID == "hm2_high") *f = high;
                if (f->paramID == "hm2_level") *f = level;
            }
    }

    struct Measure { double out, stage, second, third; float pos, neg; };

    static Measure runSine (HM2StyleDistortionProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        juce::AudioBuffer<float> buf (1, 1);
        double oS = 0, oC = 0, sS = 0, sC = 0, hS = 0, hC = 0, tS = 0, tC = 0;
        float pos = 0, neg = 0;

        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            buf.setSample (0, 0, (float) (amp * std::sin (ph)));
            p.process (buf);
            if (n >= start)
            {
                const double y = buf.getSample (0, 0);
                oS += y * std::sin (ph); oC += y * std::cos (ph);
                hS += y * std::sin (2.0 * ph); hC += y * std::cos (2.0 * ph);
                tS += y * std::sin (3.0 * ph); tC += y * std::cos (3.0 * ph);
                const double v = p.debugStageOut();
                sS += v * std::sin (ph); sC += v * std::cos (ph);
                pos = juce::jmax (pos, (float) y);
                neg = juce::jmin (neg, (float) y);
            }
        }

        const double k = 2.0 / (double) len;
        return { k * std::sqrt (oS * oS + oC * oC), k * std::sqrt (sS * sS + sC * sC),
                 k * std::sqrt (hS * hS + hC * hC), k * std::sqrt (tS * tS + tC * tC), pos, neg };
    }

    void runTest() override
    {
        beginTest ("DC operating point converges; Q7's collector (the op-amp's (+) pin) sits inside the supply");
        {
            HM2StyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("Q7 collector DC = " + juce::String (p.debugStageOut(), 3) + " V");
            expectGreaterThan (p.debugStageOut(), 1.0);
            expectLessThan (p.debugStageOut(), 7.0);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            HM2StyleDistortionProcessor p;
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
                expect (std::abs (buf.getSample (0, i)) < 0.01f);
            }
            expectEquals (p.getSolveFailureRate(), 0.0);
        }

        beginTest ("the two transistor stages have very high gain (informational) at 1 kHz");
        {
            HM2StyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f, 0.5f, 0.5f);
            const auto m = runSine (p, 1000.0, 0.0002);
            logMessage ("Q6+Q7 stage gain @1k, Dist max: " + juce::String (m.stage / 0.0002, 1) + "x = "
                        + juce::String (20.0 * std::log10 (m.stage / 0.0002), 1) + " dB");
            expect (std::isfinite (m.stage));
            expectGreaterThan (m.stage / 0.0002, 30.0);
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Dist setting");
        for (float ds : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            HM2StyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, ds, 0.5f, 0.5f, 0.5f);
            const auto m = runSine (p, 220.0, 0.3, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.pos) < 20.0f && std::abs (m.neg) < 20.0f);
            logMessage ("dist " + juce::String (ds, 2) + ": solver failures per block = " + juce::String (p.blockFailures (0)) + " / "
                        + juce::String (p.blockFailures (1)) + " / " + juce::String (p.blockFailures (2))
                        + " of " + juce::String ((long long) (0.4 * sr)));
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("Dist raises distortion; the 1-vs-2-diode feedback makes the clipping asymmetric (2nd harmonic)");
        {
            double previous = -1.0;
            for (float ds : { 0.3f, 0.6f, 0.8f, 1.0f })
            {
                HM2StyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, ds, 0.5f, 0.5f, 0.5f);
                const auto m = runSine (p, 220.0, 0.01);
                const double thd = std::sqrt (m.second * m.second + m.third * m.third) / m.out;
                logMessage ("dist " + juce::String (ds, 1) + " -> out " + juce::String (m.out, 4) + " V, HD2 = "
                            + juce::String (100.0 * m.second / m.out, 2) + "%, HD3 = " + juce::String (100.0 * m.third / m.out, 2)
                            + "%  peaks " + juce::String (m.pos, 3) + "/" + juce::String (m.neg, 3));
                expectGreaterThan (thd, previous);
                previous = thd;
            }
        }

        beginTest ("Color section: Low boosts the bass resonance, High boosts the mid/high resonance, each in its own band");
        {
            // Small signal, moderate Dist, so the diodes stay quiet and this measures the linear Color network.
            auto level = [] (float low, float high, double freq)
            {
                HM2StyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.35f, low, high, 1.0f);
                return runSine (p, freq, 0.00002, 0.5, 0.25).out;
            };

            const double lowBoost = level (1.0f, 0.5f, 90.0), lowCut = level (0.0f, 0.5f, 90.0);
            const double highBoost = level (0.5f, 1.0f, 1200.0), highCut = level (0.5f, 0.0f, 1200.0);
            const double lowAtMid = level (1.0f, 0.5f, 1200.0), lowAtMidFlat = level (0.5f, 0.5f, 1200.0);
            logMessage ("90 Hz: Low up/down = " + juce::String (lowBoost / lowCut, 2) + "x;  1.2 kHz: High up/down = "
                        + juce::String (highBoost / highCut, 2) + "x;  1.2 kHz with Low up vs flat = " + juce::String (lowAtMid / lowAtMidFlat, 2) + "x");
            expectGreaterThan (lowBoost / lowCut, 1.5);
            expectGreaterThan (highBoost / highCut, 1.5);
        }

        beginTest ("Level scales the output");
        {
            double previous = -1.0;
            for (float l : { 0.2f, 0.5f, 0.8f, 1.0f })
            {
                HM2StyleDistortionProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.5f, 0.5f, 0.5f, l);
                const double a = runSine (p, 1000.0, 0.002).out;
                expectGreaterThan (a, previous);
                previous = a;
            }
        }

        beginTest ("stereo channels stay identical");
        {
            HM2StyleDistortionProcessor p;
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.5f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (2, 512);
            float maxDiff = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = (float) (0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) (b * 512 + i) / sr));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                if (b >= 30)
                    for (int i = 0; i < 512; ++i)
                        maxDiff = juce::jmax (maxDiff, std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
            }
            expectLessThan (maxDiff, 1.0e-5f);
        }

        beginTest ("cost: HM-2 processor time per sample (informational)");
        {
            HM2StyleDistortionProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.7f, 0.5f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 60;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, (float) (0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (b * 512 + i) / sr)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("HM-2: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static HM2StyleDistortionProcessorTests hm2StyleDistortionProcessorTests;

} // namespace openguitarmultifx
