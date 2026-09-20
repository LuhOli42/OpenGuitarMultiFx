#include "Effects/CentaurStyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class CentaurStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    CentaurStyleOverdriveProcessorTests() : juce::UnitTest ("CentaurStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (CentaurStyleOverdriveProcessor& p, float gain, float treble, float level)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
            {
                if (f->paramID == "centaur_gain") *f = gain;
                if (f->paramID == "centaur_treble") *f = treble;
                if (f->paramID == "centaur_level") *f = level;
            }
    }

    struct Measure { double outFundamental, gainStageFundamental, outThirdHarmonic; float posPeak, negPeak; };

    /** Sample-by-sample so the gain-stage node can be read back too. */
    static Measure runSine (CentaurStyleOverdriveProcessor& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        juce::AudioBuffer<float> buf (1, 1);
        double oS = 0, oC = 0, gS = 0, gC = 0, tS = 0, tC = 0;
        float pos = 0.0f, neg = 0.0f;

        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            buf.setSample (0, 0, (float) (amp * std::sin (ph)));
            p.process (buf);

            if (n >= start)
            {
                const double y = (double) buf.getSample (0, 0);
                const double gs = p.debugGainStageOutput() - 4.5;
                oS += y * std::sin (ph); oC += y * std::cos (ph);
                gS += gs * std::sin (ph); gC += gs * std::cos (ph);
                tS += y * std::sin (3.0 * ph); tC += y * std::cos (3.0 * ph);
                pos = juce::jmax (pos, (float) y);
                neg = juce::jmin (neg, (float) y);
            }
        }

        return { 2.0 * std::sqrt (oS * oS + oC * oC) / (double) len,
                 2.0 * std::sqrt (gS * gS + gC * gC) / (double) len,
                 2.0 * std::sqrt (tS * tS + tC * tC) / (double) len, pos, neg };
    }

    void runTest() override
    {
        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            CentaurStyleOverdriveProcessor p;
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

        beginTest ("gain stage reaches the published ~40 dB (100x) near 1 kHz at max Gain");
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f, 0.5f);
            const auto m = runSine (p, 1000.0, 0.002); // 2 mV keeps the diodes barely on
            const double gainDb = 20.0 * std::log10 (m.gainStageFundamental / 0.002);
            logMessage ("gain stage @1k, Gain max: " + juce::String (m.gainStageFundamental / 0.002, 1) + "x = "
                        + juce::String (gainDb, 1) + " dB (ElectroSmash: max ~40 dB around 1 kHz)");
            expectGreaterThan (gainDb, 33.0);
            expectLessThan (gainDb, 43.0);
        }

        beginTest ("gain stage has a mid hump: less gain at 100 Hz and at 10 kHz than at 1 kHz");
        {
            double g[3];
            const double freqs[3] = { 100.0, 1000.0, 10000.0 };
            for (int i = 0; i < 3; ++i)
            {
                CentaurStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 1.0f, 0.5f, 0.5f);
                g[i] = runSine (p, freqs[i], 0.002).gainStageFundamental / 0.002;
            }
            logMessage ("gain stage 100Hz/1k/10k = " + juce::String (g[0], 1) + " / " + juce::String (g[1], 1) + " / " + juce::String (g[2], 1));
            expectGreaterThan (g[1], g[0] * 1.5);
            expectGreaterThan (g[1], g[2] * 1.5);
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Gain setting");
        for (float gainSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, gainSetting, 0.5f, 0.5f);
            const auto m = runSine (p, 220.0, 0.8, 0.3, 0.1);
            expect (std::isfinite (m.outFundamental));
            expect (std::abs (m.posPeak) < 30.0f && std::abs (m.negPeak) < 30.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("clipping is symmetric (matched germanium pair, one each way)");
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 1.0f, 0.5f, 1.0f);
            const auto m = runSine (p, 220.0, 0.3, 0.5, 0.2);
            logMessage ("posPeak=" + juce::String (m.posPeak, 4) + " negPeak=" + juce::String (m.negPeak, 4));
            expectGreaterThan (m.posPeak, 0.01f);
            expectLessThan (std::abs (m.posPeak - std::abs (m.negPeak)) / m.posPeak, 0.10f);
        }

        beginTest ("Treble boosts highs far more than lows (active shelving stage)");
        {
            CentaurStyleOverdriveProcessor lo, hi, loB, hiB;
            for (auto* p : { &lo, &hi, &loB, &hiB })
                p->prepare (sr, 512, 1);
            setKnobs (lo, 0.5f, 0.0f, 0.5f);
            setKnobs (hi, 0.5f, 1.0f, 0.5f);
            setKnobs (loB, 0.5f, 0.0f, 0.5f);
            setKnobs (hiB, 0.5f, 1.0f, 0.5f);
            const double treble = runSine (hi, 8000.0, 0.005).outFundamental / runSine (lo, 8000.0, 0.005).outFundamental;
            const double bass = runSine (hiB, 100.0, 0.005).outFundamental / runSine (loB, 100.0, 0.005).outFundamental;
            logMessage ("8 kHz treble1/treble0 = " + juce::String (treble, 2) + ", 100 Hz = " + juce::String (bass, 2));
            expectGreaterThan (treble, 4.0);
            expectGreaterThan (treble, bass * 3.0);
        }

        beginTest ("Level scales the output");
        {
            double previous = -1.0;
            for (float l : { 0.1f, 0.4f, 0.7f, 1.0f })
            {
                CentaurStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, 0.5f, 0.5f, l);
                const double a = runSine (p, 1000.0, 0.05).outFundamental;
                expectGreaterThan (a, previous);
                previous = a;
            }

        }

        beginTest ("Gain sets how hard the signal is clipped: 3rd-harmonic distortion grows with Gain");
        {
            // The Centaur's second Gain gang deliberately BALANCES level against drive, so output level is not
            // monotonic in Gain -- distortion is what Gain controls.
            double previous = -1.0;
            for (float gsetting : { 0.2f, 0.5f, 0.8f, 1.0f })
            {
                CentaurStyleOverdriveProcessor p;
                p.prepare (sr, 512, 1);
                setKnobs (p, gsetting, 0.5f, 0.5f);
                const auto m = runSine (p, 220.0, 0.1);
                const double hd3 = m.outThirdHarmonic / m.outFundamental;
                logMessage ("gain " + juce::String (gsetting, 1) + " -> out " + juce::String (m.outFundamental, 4)
                            + " V, HD3 = " + juce::String (100.0 * hd3, 2) + "%");
                expectGreaterThan (hd3, previous);
                previous = hd3;
            }
        }

        beginTest ("clean feed-forward/bleed path: output is not silent with Gain at minimum");
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.0f, 0.5f, 1.0f);
            const auto m = runSine (p, 500.0, 0.1);
            logMessage ("Gain min, 500 Hz, 0.1 V in -> " + juce::String (m.outFundamental, 4) + " V out");
            expectGreaterThan (m.outFundamental, 0.005);
        }

        beginTest ("stereo channels stay identical");
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 2);
            setKnobs (p, 0.7f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (2, 512);
            float maxDiff = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) (b * 512 + i) / sr));
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

        beginTest ("cost: Centaur processor time per sample (informational)");
        {
            CentaurStyleOverdriveProcessor p;
            p.prepare (sr, 512, 1);
            setKnobs (p, 0.7f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 100;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) (b * 512 + i) / sr)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("Centaur: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static CentaurStyleOverdriveProcessorTests centaurStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
