#include "Effects/Dbx160StyleCompressorProcessor.h"
#include "Effects/GSeriesStyleBusCompressorProcessor.h"
#include "Effects/Urei1176StyleCompressorProcessor.h"
#include "Effects/La2aStyleCompressorProcessor.h"

#include "Effects/CompressorCommon.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/** The studio compressors against the figures their manuals publish (docs/circuits/*Compressor.md). */
class StudioCompressorTests : public juce::UnitTest
{
public:
    StudioCompressorTests() : juce::UnitTest ("StudioCompressors", "Effects") {}

    static constexpr double sr = 48000.0;

    /** Runs `seconds` of a 1 kHz sine of `amp` and returns the output/input level ratio in dB over the last 50 ms. */
    template <typename P>
    static double gainDb (P& p, double amp, double seconds = 3.0)
    {
        juce::AudioBuffer<float> b (1, 1);
        const int n = (int) (seconds * sr);
        double in2 = 0.0, out2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
            b.setSample (0, 0, x);
            p.process (b);
            if (i > n - (int) (0.05 * sr))
            {
                in2 += (double) x * x;
                out2 += (double) b.getSample (0, 0) * b.getSample (0, 0);
            }
        }
        return 10.0 * std::log10 (out2 / in2);
    }

    /** Time (ms) for the gain reduction to go from 0 to `fraction` of its final value after a step of `amp`. */
    template <typename P>
    static double stepTimeMs (P& p, double amp, double targetGrDb)
    {
        juce::AudioBuffer<float> b (1, 1);
        for (int i = 0; i < (int) (20.0 * sr); ++i)
        {
            b.setSample (0, 0, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr)));
            p.process (b);
            if (p.getGainReductionDb() >= targetGrDb)
                return 1000.0 * i / sr;
        }
        return -1.0;
    }

    /** Peak of the output (dBFS) over the first `ms` of a loud 3 kHz burst: a feedback compressor with a slow attack lets the front of the burst through. */
    template <typename P>
    static double burstFrontPeakDb (P& p, double amp, double ms)
    {
        juce::AudioBuffer<float> b (1, 1);
        double peak = 0.0;
        for (int i = 0; i < (int) (ms * 0.001 * sr); ++i)
        {
            b.setSample (0, 0, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 3000.0 * i / sr)));
            p.process (b);
            if (i >= 3) // the feedback loop reacts one sample after the signal: the first samples always pass
                peak = std::max (peak, (double) std::abs (b.getSample (0, 0)));
        }
        return dyn::toDb (peak);
    }

    void runTest() override
    {
        using Dbx = Dbx160StyleCompressorProcessor;
        using G = GSeriesStyleBusCompressorProcessor;
        using U = Urei1176StyleCompressorProcessor;

        beginTest ("dbx 160: static curve at 4:1 -- the slope above the threshold is 1/4 (Over Easy off)");
        {
            Dbx p;
            p.prepare (sr, 512, 1);
            setParams (p, { -30.0f, 4.0f, 0.0f, 0.0f });
            const double g1 = gainDb (p, dyn_amp (-20.0));
            const double g2 = gainDb (p, dyn_amp (-10.0));
            logMessage ("gain at -20 / -10 dBFS RMS-ish: " + juce::String (g1, 2) + " / " + juce::String (g2, 2));
            // 10 dB more input = 2.5 dB more output = 7.5 dB less gain
            expectWithinAbsoluteError (g1 - g2, 7.5, 0.6);
        }

        beginTest ("dbx 160: release runs at ~125 dB/s (the manual's 8 ms/1 dB, 80 ms/10 dB, 400 ms/50 dB)");
        {
            Dbx p;
            p.prepare (sr, 512, 1);
            setParams (p, { -40.0f, 10.0f, 0.0f, 0.0f });
            juce::AudioBuffer<float> b (1, 1);
            for (int i = 0; i < (int) (2.0 * sr); ++i) { b.setSample (0, 0, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr))); p.process (b); }
            const double gr0 = p.getGainReductionDb();
            int i = 0;
            while (p.getGainReductionDb() > gr0 - 10.0 && i < (int) (2.0 * sr)) { b.setSample (0, 0, 0.0f); p.process (b); ++i; }
            const double ms = 1000.0 * i / sr;
            logMessage ("10 dB of release after " + juce::String (ms, 1) + " ms (starting at " + juce::String (gr0, 1) + " dB)");
            expectWithinAbsoluteError (ms, 80.0, 30.0);
        }

        beginTest ("G-series: ratio 4:1 lowers the threshold by 3 dB and the reduction settles at the static curve");
        {
            G p;
            p.prepare (sr, 512, 1);
            setParams (p, { -20.0f, 1.0f, 3.0f, 2.0f, 0.0f, 0.0f }); // 4:1, 3 ms attack, 0.6 s release
            const double g = gainDb (p, dyn_amp (-4.0));
            const double expected = -dyn::staticReductionDb (-4.0 + 3.0103, -20.0 - 3.0, 4.0, G::kneeDb);
            logMessage ("gain " + juce::String (g, 2) + " dB, static " + juce::String (expected, 2));
            expectWithinAbsoluteError (g, expected, 1.0);
        }

        beginTest ("G-series: attack switch changes the time to reach the reduction");
        {
            double t[2];
            int k = 0;
            for (float a : { 0.0f, 5.0f })
            {
                G p;
                p.prepare (sr, 512, 1);
                setParams (p, { -30.0f, 2.0f, a, 2.0f, 0.0f, 0.0f });
                t[k++] = stepTimeMs (p, 0.5, 6.0);
            }
            logMessage ("0.1 ms attack: " + juce::String (t[0], 2) + " ms; 30 ms attack: " + juce::String (t[1], 2) + " ms");
            expectGreaterThan (t[0], 0.0);
            expectLessThan (t[0] * 20.0, t[1]);
        }

        beginTest ("1176: the four ratio buttons give ~4/8/12/20:1 above the threshold; all buttons is beyond 20:1");
        {
            const double expectedRatio[] = { 4.0, 8.0, 12.0, 20.0, 40.0 };
            for (int r = 0; r < 5; ++r)
            {
                U p;
                p.prepare (sr, 512, 1);
                setParams (p, { 1.0f, 0.0f, 0.5f, 0.5f, (float) r }); // Input at maximum
                const double thr = U::specFor (r).thresholdDb - 18.0;
                const double lo = thr + 8.0, hi = thr + 20.0;
                U a, b;
                a.prepare (sr, 512, 1); b.prepare (sr, 512, 1);
                setParams (a, { 1.0f, 0.0f, 0.5f, 0.5f, (float) r });
                setParams (b, { 1.0f, 0.0f, 0.5f, 0.5f, (float) r });
                const double outLo = lo + gainDb (a, dyn_amp (lo));
                const double outHi = hi + gainDb (b, dyn_amp (hi));
                const double slope = (hi - lo) / std::max (0.05, outHi - outLo);
                logMessage ("button " + juce::String (r) + ": " + juce::String (slope, 1) + ":1 (want " + juce::String (expectedRatio[r], 0) + ")");
                if (r < 4)
                {
                    expectGreaterThan (slope, expectedRatio[r] * 0.6);
                    expectLessThan (slope, expectedRatio[r] * 1.6);
                }
                else
                    expectGreaterThan (slope, 20.0);
            }
        }

        beginTest ("LA-2A: the output stage distorts ~0.35% (2nd harmonic) at +10 dBm with no compression");
        {
            using L = La2aStyleCompressorProcessor;
            L p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.0f, 50.0f, 0.0f });
            const double d = thd (p, 0.4);
            logMessage ("THD at -8 dBFS: " + juce::String (d * 100.0, 3) + " %");
            expectWithinAbsoluteError (d, 0.0035, 0.0008);
        }

        beginTest ("LA-2A: Compress is a ~3:1 law, Limit ~10:1 (at Peak Reduction 60), and the loop never takes more than 40 dB");
        {
            using L = La2aStyleCompressorProcessor;
            const double target[] = { 3.0, 10.0 };
            for (int m = 0; m < 2; ++m)
            {
                double outDb[2];
                int k = 0;
                for (double inDb : { -8.0, 0.0 })
                {
                    L p;
                    p.prepare (sr, 512, 1);
                    setParams (p, { 60.0f, 50.0f, (float) m });
                    outDb[k++] = inDb + gainDb (p, dyn_amp (inDb), 8.0);
                }
                const double slope = 8.0 / std::max (0.05, outDb[1] - outDb[0]);
                logMessage (juce::String (m == 0 ? "Compress" : "Limit") + ": " + juce::String (slope, 1) + ":1 between -8 and 0 dBFS RMS");
                expectGreaterThan (slope, target[m] * 0.6);
                expectLessThan (slope, target[m] * 1.7);
            }
            L p;
            p.prepare (sr, 512, 1);
            setParams (p, { 100.0f, 50.0f, 1.0f });
            drive (p, 1.0, 5.0);
            logMessage ("gain reduction on a full-scale sine at Peak Reduction 100 / Limit: " + juce::String (p.getGainReductionDb(), 1) + " dB");
            expectLessThan (p.getGainReductionDb(), 40.01);
            expectGreaterThan (p.getGainReductionDb(), 15.0);
        }

        beginTest ("LA-2A: the cell releases half of the reduction in ~60 ms, and the rest slower the longer it has been lit");
        {
            using L = La2aStyleCompressorProcessor;
            double half[2], full[2];
            int k = 0;
            for (double seconds : { 0.4, 40.0 })
            {
                L p;
                p.prepare (sr, 512, 1);
                setParams (p, { 60.0f, 50.0f, 0.0f });
                const double gr = drive (p, dyn_amp (-6.0), seconds);
                half[k] = releaseTime (p, 0.5);
                full[k] = releaseTime (p, 0.1);
                logMessage ("lit for " + juce::String (seconds, 1) + " s (" + juce::String (gr, 1) + " dB, memory " + juce::String (p.getMemory(), 2)
                            + "): half " + juce::String (half[k] * 1000.0, 0) + " ms, 90% " + juce::String (full[k], 2) + " s");
                ++k;
            }
            expectWithinAbsoluteError (half[0], 0.06, 0.03);
            expectGreaterThan (full[1], full[0] * 1.5);
            expectGreaterThan (full[1], 0.5);
            expectLessThan (full[1], 6.0);
        }

        beginTest ("LA-2A: attack ~10 ms (63% of the final reduction of a step)");
        {
            using L = La2aStyleCompressorProcessor;
            L p;
            p.prepare (sr, 512, 1);
            setParams (p, { 60.0f, 50.0f, 0.0f });
            juce::AudioBuffer<float> b (1, 1);
            std::vector<double> gr;
            for (int i = 0; i < (int) (4.0 * sr); ++i)
            {
                b.setSample (0, 0, (float) (dyn_amp (-6.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr)));
                p.process (b);
                gr.push_back (p.getGainReductionDb());
            }
            size_t i63 = 0;
            while (i63 < gr.size() && gr[i63] < 0.63 * gr.back())
                ++i63;
            logMessage ("63% of " + juce::String (gr.back(), 1) + " dB after " + juce::String (1000.0 * (double) i63 / sr, 1) + " ms");
            expectGreaterThan (1000.0 * (double) i63 / sr, 3.0);
            expectLessThan (1000.0 * (double) i63 / sr, 60.0);
        }

        beginTest ("1176: attack 20 us .. 800 us and release 50 ms .. 1.1 s the slow attack lets the front of a burst through");
        {
            double tFast, tSlow;
            {
                U p; p.prepare (sr, 512, 1); setParams (p, { 1.0f, 0.0f, 1.0f, 0.5f, 1.0f }); tFast = burstFrontPeakDb (p, 0.05, 1.5);
            }
            {
                U p; p.prepare (sr, 512, 1); setParams (p, { 1.0f, 0.0f, 0.0f, 0.5f, 1.0f }); tSlow = burstFrontPeakDb (p, 0.05, 1.5);
            }
            logMessage ("front of a burst, attack 20 us: " + juce::String (tFast, 2) + " dBFS; 800 us: " + juce::String (tSlow, 2) + " dBFS");
            expectGreaterThan (tSlow, tFast + 1.0);
        }
    }

    /** Feeds a 1 kHz sine of peak `amp` for `seconds`; returns the settled gain reduction. */
    template <typename P>
    static double drive (P& p, double amp, double seconds, double freq = 1000.0, long long start = 0)
    {
        juce::AudioBuffer<float> b (1, 1);
        const int n = (int) (seconds * sr);
        for (int i = 0; i < n; ++i)
        {
            b.setSample (0, 0, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) (start + i) / sr)));
            p.process (b);
        }
        return p.getGainReductionDb();
    }

    /** Seconds of silence until the gain reduction has fallen to `fraction` of what it was (or `limit`). */
    template <typename P>
    static double releaseTime (P& p, double fraction, double limit = 30.0)
    {
        juce::AudioBuffer<float> b (1, 1);
        const double gr0 = p.getGainReductionDb();
        for (int i = 0; i < (int) (limit * sr); ++i)
        {
            b.setSample (0, 0, 0.0f);
            p.process (b);
            if (p.getGainReductionDb() <= gr0 * fraction)
                return (double) i / sr;
        }
        return limit;
    }

    /** Total harmonic distortion (2nd..6th) of a 1 kHz sine of peak `amp` after a warm-up. */
    template <typename P>
    static double thd (P& p, double amp)
    {
        drive (p, amp, 0.6);
        const int n = 48 * 100; // 100 cycles of 1 kHz at 48 kHz
        std::vector<double> y ((size_t) n);
        juce::AudioBuffer<float> b (1, 1);
        for (int i = 0; i < n; ++i)
        {
            b.setSample (0, 0, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr)));
            p.process (b);
            y[(size_t) i] = b.getSample (0, 0);
        }
        auto mag = [&] (int h)
        {
            double c = 0.0, s = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * 1000.0 * h * i / sr;
                c += y[(size_t) i] * std::cos (ph);
                s += y[(size_t) i] * std::sin (ph);
            }
            return std::hypot (c, s);
        };
        double h2 = 0.0;
        for (int h = 2; h <= 6; ++h)
            h2 += mag (h) * mag (h);
        return std::sqrt (h2) / mag (1);
    }

    static double dyn_amp (double dbfsRms) { return std::sqrt (2.0) * std::pow (10.0, dbfsRms / 20.0); }
};

static StudioCompressorTests studioCompressorTests;

} // namespace openguitarmultifx
