#include "Effects/GE7StyleEqualizerProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class GE7StyleEqualizerProcessorTests : public juce::UnitTest
{
public:
    GE7StyleEqualizerProcessorTests() : juce::UnitTest ("GE7StyleEqualizerProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = GE7StyleEqualizerProcessor;

    /** Gain (dB) at `freq` for a small sine, with the sliders as given (band 0..6 then Level, nominal dB). */
    static double gainDb (std::initializer_list<float> sliders, double freq, double amp = 0.01)
    {
        P p;
        p.prepare (sr, 512, 1);
        setParams (p, sliders);
        const auto m = probeSine (p, {}, freq, amp, sr, 0.5, 0.25);
        return 20.0 * std::log10 (m.out / amp);
    }

    void runTest() override
    {
        beginTest ("DC operating point converges and silence stays silent");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            expectWithinAbsoluteError (p.debugEqOut(), 4.5, 0.02);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 10; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
                expect (std::abs (buf.getSample (0, i)) < 0.001f);
            expectEquals (p.getSolveFailureRate(), 0.0);
        }

        beginTest ("every slider centred: the whole circuit is flat (the input pre-emphasis and the output de-emphasis cancel)");
        {
            double worst = 0.0;
            for (double f : { 40.0, 80.0, 150.0, 300.0, 600.0, 1200.0, 2500.0, 5000.0, 8000.0, 12000.0 })
            {
                const double g = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, f);
                logMessage (juce::String (f, 0).paddedLeft (' ', 6) + " Hz: " + juce::String (g, 2) + " dB");
                worst = juce::jmax (worst, std::abs (g));
            }
            expectLessThan (worst, 1.0); // a constant -0.65 dB (the output follower and its 100K load), no shape
        }

        beginTest ("each band's slider at +15 / -15 dB moves that band's centre by about +-15 dB and leaves far-away bands alone");
        {
            const double centres[] = { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
            for (int k = 0; k < 7; ++k)
            {
                const double flat = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, centres[k]);
                float up[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }, down[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
                up[k] = 15.0f;
                down[k] = -15.0f;
                auto run = [&] (const float* v, double f)
                {
                    P p;
                    p.prepare (sr, 512, 1);
                    setParams (p, { v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7] });
                    const auto m = probeSine (p, {}, f, 0.01, sr, 0.5, 0.25);
                    return 20.0 * std::log10 (m.out / 0.01);
                };
                const double boost = run (up, centres[k]) - flat, cut = run (down, centres[k]) - flat;
                logMessage (juce::String (centres[k], 0) + " Hz band: slider +15 -> " + juce::String (boost, 1) + " dB, -15 -> " + juce::String (cut, 1) + " dB");
                // the 6.4 kHz band is the RC branch without a gyrator (a shelf that keeps rising above the band): 8 dB AT 6.4 kHz
                const double minimum = k == 6 ? 6.0 : 10.0;
                expectGreaterThan (boost, minimum);
                expectLessThan (boost, 21.0);
                expectLessThan (cut, -minimum);
                expectGreaterThan (cut, -21.0);
            }
            // a 100 Hz boost does not move 3.2 kHz
            const double far = gainDb ({ 15, 0, 0, 0, 0, 0, 0, 0 }, 3200.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 3200.0);
            expectLessThan (std::abs (far), 1.0);
        }

        beginTest ("Level: +-15 dB across the spectrum");
        {
            const double up = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 15 }, 1000.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 1000.0);
            const double down = gainDb ({ 0, 0, 0, 0, 0, 0, 0, -15 }, 1000.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 1000.0);
            logMessage ("Level +15 -> " + juce::String (up, 1) + " dB, -15 -> " + juce::String (down, 1) + " dB");
            expectGreaterThan (up, 11.0);
            expectLessThan (up, 17.0);
            expectLessThan (down, -11.0);
            expectGreaterThan (down, -17.0);
        }

        beginTest ("a hot signal at every boost stays finite and converges");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 15, 15, 15, 15, 15, 15, 15, 15 });
            const auto m = probeSine (p, {}, 300.0, 0.6, sr, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.outMax) < 6.0f && std::abs (m.outMin) < 6.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("stereo: identical input gives identical channels");
        {
            P p;
            p.prepare (sr, 512, 2);
            setParams (p, { 6, -6, 3, 0, 9, -9, 4, 0 });
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
        }

        beginTest ("random slider moves, plucked notes and hot bursts: the solver never fails to converge");
        for (int seed = 77; seed < 79; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest ("cost (informational)");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 4, -3, 2, 0, 5, -2, 3, 0 });
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
            logMessage ("GE-7: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static GE7StyleEqualizerProcessorTests ge7StyleEqualizerProcessorTests;

} // namespace openguitarmultifx
