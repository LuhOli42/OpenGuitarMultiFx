#include "Effects/BD2StyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class BD2StyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    BD2StyleOverdriveProcessorTests() : juce::UnitTest ("BD2StyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (BD2StyleOverdriveProcessor& p, float gain, float tone, float level)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
            {
                if (f->paramID == "bd2_gain") *f = gain;
                if (f->paramID == "bd2_tone") *f = tone;
                if (f->paramID == "bd2_level") *f = level;
            }
    }

    struct Measure { double out, stage1, stage2, third; float pos, neg; };

    static Measure runSine (BD2StyleOverdriveProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        juce::AudioBuffer<float> buf (1, 1);
        double oS = 0, oC = 0, s1S = 0, s1C = 0, s2S = 0, s2C = 0, tS = 0, tC = 0;
        float pos = 0, neg = 0;
        double dc1 = 0, dc2 = 0;
        long long dcCount = 0;

        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            buf.setSample (0, 0, (float) (amp * std::sin (ph)));
            p.process (buf);
            if (n >= start)
            {
                const double y = buf.getSample (0, 0);
                oS += y * std::sin (ph); oC += y * std::cos (ph);
                tS += y * std::sin (3.0 * ph); tC += y * std::cos (3.0 * ph);
                const double v1 = p.debugStage1Out(), v2 = p.debugStage2Out();
                dc1 += v1; dc2 += v2; ++dcCount;
                s1S += v1 * std::sin (ph); s1C += v1 * std::cos (ph);
                s2S += v2 * std::sin (ph); s2C += v2 * std::cos (ph);
                pos = juce::jmax (pos, (float) y);
                neg = juce::jmin (neg, (float) y);
            }
        }

        const double k = 2.0 / (double) len;
        return { k * std::sqrt (oS * oS + oC * oC), k * std::sqrt (s1S * s1S + s1C * s1C),
                 k * std::sqrt (s2S * s2S + s2C * s2C), k * std::sqrt (tS * tS + tC * tC), pos, neg };
    }

    void runTest() override
    {
        beginTest ("DC operating point converges and both gain stages sit near the +4 V reference");
        {
            BD2StyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("stage-1 collector DC = " + juce::String (p.debugStage1Out(), 3) + " V, stage-2 = "
                        + juce::String (p.debugStage2Out(), 3) + " V");
            expectGreaterThan (p.debugStage1Out(), 3.3);
            expectLessThan (p.debugStage1Out(), 4.8);
            expectGreaterThan (p.debugStage2Out(), 3.3);
            expectLessThan (p.debugStage2Out(), 4.8);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            BD2StyleOverdriveProcessor p;
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

        beginTest ("stage 1 gain: matches 1 + (R29 + rheostat)/Z(R31+C22) at min Gain; falls short of the ideal-op-amp figure at max");
        {
            // At 2 kHz Z = 1.5K - j530 ohm (|Z| ~ 1.59K). Ideal-op-amp closed loop: min 1 + 22K/1.59K ~ 14.8 (23 dB),
            // max 1 + 272K/1.59K ~ 172 (45 dB). This stage is a DISCRETE op-amp with finite loop gain, so at max Gain
            // (where the loop gain 1/beta is highest) the real gain lands below the ideal formula: open loop
            // A ~ gm(pair) * ... * gm(PNP)*R32 ~ a few hundred, so closed-loop ~ A/(1 + A/172) ~ 100-130.
            double gMin, gMax;
            {
                BD2StyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.0f, 0.5f, 0.5f);
                gMin = runSine (p, 2000.0, 0.0005).stage1 / 0.0005;
            }
            {
                BD2StyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 1.0f, 0.5f, 0.5f);
                gMax = runSine (p, 2000.0, 0.0005).stage1 / 0.0005;
            }
            logMessage ("stage-1 gain @2kHz: min " + juce::String (gMin, 1) + "x (" + juce::String (20.0 * std::log10 (gMin), 1)
                        + " dB), max " + juce::String (gMax, 1) + "x (" + juce::String (20.0 * std::log10 (gMax), 1) + " dB); ideal formula 14.8x / 172x");
            expectGreaterThan (gMin, 12.0);
            expectLessThan (gMin, 16.0);   // min Gain: loop gain is huge, so it follows the formula closely
            expectGreaterThan (gMax, 60.0);
            expectLessThan (gMax, 172.0);  // and can never exceed the ideal-op-amp figure
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Gain setting");
        for (float gs : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            BD2StyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, gs, 0.5f, 0.5f);
            const auto m = runSine (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.pos) < 20.0f && std::abs (m.neg) < 20.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("overall response (informational) and a low-frequency peak from the gyrator filter");
        {
            const double freqs[] = { 30.0, 60.0, 100.0, 130.0, 200.0, 400.0, 1000.0, 3000.0 };
            double out[8];
            for (int i = 0; i < 8; ++i)
            {
                BD2StyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.0f, 0.5f, 1.0f);
                out[i] = runSine (p, freqs[i], 0.0005, 0.5, 0.25).out / 0.0005;
            }
            juce::String line;
            for (int i = 0; i < 8; ++i)
                line << juce::String (freqs[i], 0) << "Hz:" << juce::String (20.0 * std::log10 (out[i]), 1) << "dB  ";
            logMessage ("BD-2 gain@min Gain, level max: " + line);
            for (int i = 0; i < 8; ++i)
                expect (std::isfinite (out[i]) && out[i] > 0.0);
        }

        beginTest ("Gain sets how hard it clips: 3rd-harmonic distortion grows with Gain");
        {
            double previous = -1.0;
            for (float gs : { 0.3f, 0.6f, 0.8f, 1.0f })
            {
                BD2StyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, gs, 0.5f, 0.5f);
                const auto m = runSine (p, 220.0, 0.05);
                const double hd3 = m.third / m.out;
                logMessage ("gain " + juce::String (gs, 1) + " -> out " + juce::String (m.out, 4) + " V, HD3 = "
                            + juce::String (100.0 * hd3, 2) + "%  peaks " + juce::String (m.pos, 3) + "/" + juce::String (m.neg, 3));
                expectGreaterThan (hd3, previous);
                previous = hd3;
            }
        }

        beginTest ("Tone moves the treble: bright > dark at 4 kHz; Level scales the output");
        {
            BD2StyleOverdriveProcessor dark, bright;
            dark.prepare (sr, 512, 1);
            bright.prepare (sr, 512, 1);
            setKnobs (dark, 0.3f, 0.0f, 0.7f);
            setKnobs (bright, 0.3f, 1.0f, 0.7f);
            const double d = runSine (dark, 4000.0, 0.002).out;
            const double b = runSine (bright, 4000.0, 0.002).out;
            logMessage ("4 kHz: tone0=" + juce::String (d, 4) + " tone1=" + juce::String (b, 4) + " ratio " + juce::String (b / d, 2));
            expectGreaterThan (b / d, 2.0);

            double previous = -1.0;
            for (float l : { 0.2f, 0.5f, 0.8f, 1.0f })
            {
                BD2StyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.4f, 0.5f, l);
                const double a = runSine (p, 1000.0, 0.005).out;
                expectGreaterThan (a, previous);
                previous = a;
            }
        }

        beginTest ("stereo channels stay identical");
        {
            BD2StyleOverdriveProcessor p;
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (2, 512);
            float maxDiff = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) (b * 512 + i) / sr));
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

        beginTest ("cost: BD-2 processor time per sample (informational)");
        {
            BD2StyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.7f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 60;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (b * 512 + i) / sr)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("BD-2: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static BD2StyleOverdriveProcessorTests bd2StyleOverdriveProcessorTests;

} // namespace openguitarmultifx
