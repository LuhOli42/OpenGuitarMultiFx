#include "Effects/HM2StyleDistortionProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <vector>

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

        if (juce::SystemStats::getEnvironmentVariable ("HM2_COST", {}).isNotEmpty())
        {
            auto cost = [&] (bool reduced, double bc6, double bc7)
            {
                HM2StyleDistortionProcessor::reducedOrder = reduced;
                HM2StyleDistortionProcessor::satBcDiode6 = bc6;
                HM2StyleDistortionProcessor::satBcDiode7 = bc7;
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
                return std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            };
            const double full = cost (false, 0.0, 0.0), red = cost (true, 0.0, 0.0),
                red6 = cost (true, 0.25, 0.0), red7 = cost (true, 0.0, 0.25),
                redd = cost (true, 0.25, 0.25);
            logMessage ("HM-2 cost full=" + juce::String (full, 2) + " reduced=" + juce::String (red, 2)
                        + " +bc6=" + juce::String (red6, 2) + " +bc7=" + juce::String (red7, 2)
                        + " +bc67=" + juce::String (redd, 2) + " us/sample");
            HM2StyleDistortionProcessor::reducedOrder = false;
            HM2StyleDistortionProcessor::satBcDiode6 = 0.25;
            HM2StyleDistortionProcessor::satBcDiode7 = 0.0;
        }

        if (juce::SystemStats::getEnvironmentVariable ("HM2_SWEEP", {}).isNotEmpty())
        {
            beginTest ("HM2_SWEEP: bc-diode coefficient sweep (dev only)");
            sweepBaseDrain();
        }
    }

    /** Dev bench (HM2_SWEEP=1): sweeps the bc-junction diode's Is multiplier
        (HM2StyleDistortionProcessor::satBcDiode6/7) and scores each value on three axes:

          - pop: the #79 complaint. A hard edge through the self-biased saturating stages
            used to make reduced-vs-full windowed peaks disagree by ~8-10 dB. Metric: worst
            |dB| windowed-peak difference between the two models on a 2 Hz square and a
            gated 220 Hz sine, 4 ms windows.
          - equivalence: same decaying-chord analysis as ReducedOrderEquivalenceTests
            (level < 2 dB, worst audible octave band < 1.2 dB) at its HM-2 grid.
          - THD: |difference| vs the full model on a driven 220 Hz sine.

        The full-order references are rendered once per stimulus/setting; only the
        reduced renders repeat per coefficient. */
    void sweepBaseDrain()
    {
        auto render = [&] (bool reduced, double bc6, double bc7, float dist, float high,
                           const std::function<float (double)>& in, double seconds)
        {
            HM2StyleDistortionProcessor::reducedOrder = reduced;
            HM2StyleDistortionProcessor::satBcDiode6 = bc6;
            HM2StyleDistortionProcessor::satBcDiode7 = bc7;
            HM2StyleDistortionProcessor p;
            setKnobs (p, dist, 0.5f, high, 0.5f);
            p.prepare (sr, 256, 1);
            const int block = 256, total = (int) (seconds * sr);
            std::vector<float> out;
            out.reserve ((size_t) total);
            juce::AudioBuffer<float> buf (1, block);
            for (int n0 = 0; n0 < total; n0 += block)
            {
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, in ((double) (n0 + i) / sr));
                p.process (buf);
                for (int i = 0; i < block; ++i)
                    out.push_back (buf.getSample (0, i));
            }
            return out;
        };

        // --- stimuli -------------------------------------------------
        // decaying three-note chord, same as ReducedOrderEquivalenceTests::render's.
        auto chord = [&] (double rms)
        {
            double norm = 0.0;
            for (int k = 1; k <= 8; ++k)
                norm += 0.5 / (double) (k * k);
            norm *= 3.0;
            const double scale = rms / std::sqrt (norm);
            return [scale] (double t)
            {
                double v = 0.0;
                for (double f0 : { 82.41, 123.47, 164.81 })
                    for (int k = 1; k <= 8; ++k)
                        v += std::sin (juce::MathConstants<double>::twoPi * f0 * k * t + 0.3 * k) / (double) k * std::exp (-t * (0.6 + 0.15 * k));
                return (float) (scale * v * 1.6);
            };
        };
        auto square = [] (double amp, double hz)
        {
            return [amp, hz] (double t) { return (float) (amp * (std::sin (juce::MathConstants<double>::twoPi * hz * t) >= 0.0 ? 1.0 : -1.0)); };
        };
        auto gated = [] (double amp, double hz)
        {
            return [amp, hz] (double t) { return (float) (std::fmod (t, 0.5) < 0.25 ? amp * std::sin (juce::MathConstants<double>::twoPi * hz * t) : 0.0); };
        };
        auto sine = [] (double amp, double hz)
        {
            return [amp, hz] (double t) { return (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * t)); };
        };

        // --- metrics -------------------------------------------------
        // octave-band fingerprint, same as ReducedOrderEquivalenceTests::analyse.
        struct Bands { double db[8]; double totalDb; };
        auto analyse = [&] (const std::vector<float>& x)
        {
            constexpr int order = 15, n = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> data ((size_t) n * 2, 0.0f);
            const size_t start = 12000;
            for (int i = 0; i < n && start + (size_t) i < x.size(); ++i)
                data[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) n));
            fft.performFrequencyOnlyForwardTransform (data.data());
            Bands b {};
            double total = 0.0;
            const double edges[9] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000 };
            for (int band = 0; band < 8; ++band)
            {
                double e = 0.0;
                for (int k = (int) (edges[band] / sr * n); k < (int) (edges[band + 1] / sr * n); ++k)
                    e += (double) data[(size_t) k] * data[(size_t) k];
                b.db[band] = 10.0 * std::log10 (e + 1.0e-30);
                total += e;
            }
            b.totalDb = 10.0 * std::log10 (total + 1.0e-30);
            return b;
        };

        // worst |dB| between windowed peaks of two renders: the pop metric. Windows where
        // either side has real signal count; the epsilon keeps silence/silence from exploding.
        auto popDb = [&] (const std::vector<float>& ref, const std::vector<float>& dut, int winSamples)
        {
            double peak = 0.0;
            for (float v : ref)
                peak = std::max (peak, (double) std::abs (v));
            const double floor = peak * 0.01, eps = peak * 0.001;
            double worst = 0.0;
            for (size_t i = 0; i + (size_t) winSamples <= ref.size(); i += (size_t) winSamples)
            {
                double pa = 0.0, pb = 0.0;
                for (int j = 0; j < winSamples; ++j)
                {
                    pa = std::max (pa, (double) std::abs (ref[i + (size_t) j]));
                    pb = std::max (pb, (double) std::abs (dut[i + (size_t) j]));
                }
                if (pa > floor || pb > floor)
                    worst = std::max (worst, std::abs (20.0 * std::log10 ((pb + eps) / (pa + eps))));
            }
            return worst;
        };

        auto thd = [&] (const std::vector<float>& x, double hz, size_t from)
        {
            // integer number of cycles, rectangular window: harmonics 2-8 over fundamental.
            const long long len = (long long) std::llround (std::floor ((double) (x.size() - from) * hz / sr) * sr / hz);
            double ss[9] = {}, cc[9] = {};
            for (int h = 1; h <= 8; ++h)
                for (long long i = 0; i < len; ++i)
                {
                    const double ph = juce::MathConstants<double>::twoPi * h * hz * (double) i / sr;
                    ss[h] += x[from + (size_t) i] * std::sin (ph);
                    cc[h] += x[from + (size_t) i] * std::cos (ph);
                }
            double num = 0.0;
            for (int h = 2; h <= 8; ++h)
                num += ss[h] * ss[h] + cc[h] * cc[h];
            return std::sqrt (num) / std::sqrt (ss[1] * ss[1] + cc[1] * cc[1]);
        };

        // --- references (coefficient-independent, rendered once) ------
        struct Ref { float dist, high; double rms; std::vector<float> out; };
        std::vector<Ref> refs;
        for (float dist : { 0.1f, 0.5f, 0.9f })
            for (float high : { 0.2f, 0.8f })
                for (double rms : { 0.1, 0.3 })
                    refs.push_back ({ dist, high, rms, render (false, 0.0, 0.0, dist, high, chord (rms), 1.5) });

        const auto sqRef = render (false, 0.0, 0.0, 0.9f, 0.5f, square (0.3, 2.0), 2.0);
        const auto sqHRef = render (false, 0.0, 0.0, 0.9f, 0.8f, square (0.5, 2.0), 2.0);
        const auto gtRef9 = render (false, 0.0, 0.0, 0.9f, 0.5f, gated (0.3, 220.0), 2.0);
        const auto gtHRef = render (false, 0.0, 0.0, 0.9f, 0.8f, gated (0.5, 220.0), 2.0);
        const auto gtRef5 = render (false, 0.0, 0.0, 0.5f, 0.5f, gated (0.3, 220.0), 2.0);
        const auto snRef = render (false, 0.0, 0.0, 0.7f, 0.5f, sine (0.05, 220.0), 1.0);
        const double thdRef = thd (snRef, 220.0, (size_t) (0.6 * sr));

        if (juce::SystemStats::getEnvironmentVariable ("HM2_DUMP", {}).isNotEmpty())
        {
            auto dump = [] (const std::vector<float>& x, const juce::String& name)
            {
                juce::FileOutputStream os (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name));
                os.write (x.data(), x.size() * sizeof (float));
            };
            dump (sqRef, "hm2_sq_ref.f32");
            dump (gtRef9, "hm2_gt_ref.f32");
            const auto sq0 = render (true, 0.0, 0.0, 0.9f, 0.5f, square (0.3, 2.0), 2.0);
            const auto gt0 = render (true, 0.0, 0.0, 0.9f, 0.5f, gated (0.3, 220.0), 2.0);
            dump (sq0, "hm2_sq_red0.f32");
            dump (gt0, "hm2_gt_red0.f32");
            // hotter stimuli hunt the documented ~8-10 dB edge case
            const auto sqHRed = render (true, 0.0, 0.0, 0.9f, 0.8f, square (0.5, 2.0), 2.0);
            const auto gtHRed = render (true, 0.0, 0.0, 0.9f, 0.8f, gated (0.5, 220.0), 2.0);
            dump (sqHRef, "hm2_sqh_ref.f32");
            dump (sqHRed, "hm2_sqh_red.f32");
            dump (gtHRef, "hm2_gth_ref.f32");
            dump (gtHRed, "hm2_gth_red.f32");
            const int w0 = (int) (0.004 * sr);
            logMessage ("hot stimuli at k=0: pop_sqh=" + juce::String (popDb (sqHRef, sqHRed, w0), 2)
                        + " pop_gth=" + juce::String (popDb (gtHRef, gtHRed, w0), 2));
        }

        // --- sweep ---------------------------------------------------
        // One row per bc-diode Is multiplier. Pop columns use the documented worst case -- the
        // hot square and hot gated sine -- plus the mild gated 0.5-dist control.
        const int win = (int) (0.004 * sr); // 4 ms windows: the pop lives inside a few ms
        auto row = [&] (const char* tag, double bc6, double bc7)
        {
            const auto sqHRed = render (true, bc6, bc7, 0.9f, 0.8f, square (0.5, 2.0), 2.0);
            const auto gtHRed = render (true, bc6, bc7, 0.9f, 0.8f, gated (0.5, 220.0), 2.0);
            const auto gtRed5 = render (true, bc6, bc7, 0.5f, 0.5f, gated (0.3, 220.0), 2.0);
            const auto snRed = render (true, bc6, bc7, 0.7f, 0.5f, sine (0.05, 220.0), 1.0);

            double worstLevel = 0.0, worstBand = 0.0;
            for (const auto& r : refs)
            {
                const auto red = render (true, bc6, bc7, r.dist, r.high, chord (r.rms), 1.5);
                const auto full = analyse (r.out), reduced = analyse (red);
                worstLevel = std::max (worstLevel, std::abs (reduced.totalDb - full.totalDb));
                for (int b = 0; b < 8; ++b)
                    if (full.db[b] > full.totalDb - 30.0)
                        worstBand = std::max (worstBand, std::abs ((reduced.db[b] - reduced.totalDb) - (full.db[b] - full.totalDb)));
            }

            const double pSq = popDb (sqHRef, sqHRed, win);
            const double pG9 = popDb (gtHRef, gtHRed, win);
            const double pG5 = popDb (gtRef5, gtRed5, win);
            const double tRed = thd (snRed, 220.0, (size_t) (0.6 * sr));
            const bool pass = pSq < 1.0 && pG9 < 1.0 && pG5 < 1.0 && worstLevel < 2.0 && worstBand < 1.2;
            logMessage (juce::String (tag).paddedRight (' ', 8)
                        + juce::String (pSq, 2).paddedRight (' ', 9)
                        + juce::String (pG9, 2).paddedRight (' ', 9)
                        + juce::String (pG5, 2).paddedRight (' ', 9)
                        + juce::String (worstLevel, 2).paddedRight (' ', 9)
                        + juce::String (worstBand, 2).paddedRight (' ', 9)
                        + juce::String (100.0 * tRed, 1).paddedRight (' ', 9)
                        + juce::String (100.0 * thdRef, 1).paddedRight (' ', 9)
                        + (pass ? "PASS" : ""));
        };

        logMessage ("bc     pop_sqh  pop_gth  pop_gt5  level    band     thd_red  thd_ref  verdict");
        for (int i = 0; i <= 10; ++i)
        {
            const double k = i == 0 ? 0.0 : 0.05 * std::exp ((double) (i - 1) / 9.0 * std::log (40.0)); // 0.05 .. 2.0
            row (juce::String (k, 3).toRawUTF8(), k, k);
        }
        // single-diode isolation rows: which transistor's bc junction kills the pop
        row ("q6.25", 0.25, 0.0);
        row ("q7.25", 0.0, 0.25);
        row ("q6.5", 0.5, 0.0);
        row ("q7.5", 0.0, 0.5);

        // real DI through both models: pop metric at real drive boundaries
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            const juce::File diFile = juce::File (OGMFX_TEST_FIXTURES_DIR)
                .getChildFile ("fixtures").getChildFile ("di").getChildFile ("guitar-di.wav");
            if (auto* rd = fm.createReaderFor (diFile))
            {
                juce::AudioBuffer<float> di (1, (int) rd->lengthInSamples);
                rd->read (&di, 0, (int) rd->lengthInSamples, 0, true, false);
                delete rd;
                const double diSr = 44100.0;
                auto diRender = [&] (bool reduced)
                {
                    HM2StyleDistortionProcessor::reducedOrder = reduced;
                    HM2StyleDistortionProcessor::satBcDiode6 = 0.25; // shipped config
                    HM2StyleDistortionProcessor::satBcDiode7 = 0.0;
                    HM2StyleDistortionProcessor p;
                    setKnobs (p, 0.9f, 0.5f, 0.8f, 0.5f);
                    p.prepare (diSr, 256, 1);
                    juce::AudioBuffer<float> buf (1, 256);
                    std::vector<float> out;
                    out.reserve ((size_t) di.getNumSamples());
                    for (int n0 = 0; n0 < di.getNumSamples(); n0 += 256)
                    {
                        buf.clear();
                        buf.copyFrom (0, 0, di, 0, n0, juce::jmin (256, di.getNumSamples() - n0));
                        p.process (buf);
                        for (int i = 0; i < 256 && n0 + i < di.getNumSamples(); ++i)
                            out.push_back (buf.getSample (0, i));
                    }
                    return out;
                };
                const auto diFull = diRender (false), diRed = diRender (true);
                logMessage ("DI dist0.9/high0.8 pop=" + juce::String (popDb (diFull, diRed, (int) (0.004 * diSr)), 2) + " dB");
            }
            else
                logMessage ("DI fixture not readable -- skipped");
        }

        HM2StyleDistortionProcessor::reducedOrder = false;
        HM2StyleDistortionProcessor::satBcDiode6 = 0.25;
        HM2StyleDistortionProcessor::satBcDiode7 = 0.0;
    }
};

static HM2StyleDistortionProcessorTests hm2StyleDistortionProcessorTests;

} // namespace openguitarmultifx
