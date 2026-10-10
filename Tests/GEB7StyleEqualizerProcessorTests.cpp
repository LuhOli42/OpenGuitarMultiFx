#include "Effects/GEB7StyleEqualizerProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class GEB7StyleEqualizerProcessorTests : public juce::UnitTest
{
public:
    GEB7StyleEqualizerProcessorTests() : juce::UnitTest ("GEB7StyleEqualizerProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = GEB7StyleEqualizerProcessor;

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

        beginTest ("every slider centred: the whole circuit is flat");
        {
            double worst = 0.0;
            for (double f : { 30.0, 50.0, 80.0, 150.0, 300.0, 600.0, 1200.0, 2500.0, 5000.0, 8000.0, 12000.0 })
            {
                const double g = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, f);
                logMessage (juce::String (f, 0).paddedLeft (' ', 6) + " Hz: " + juce::String (g, 2) + " dB");
                worst = juce::jmax (worst, std::abs (g));
            }
            expectLessThan (worst, 1.0);
        }

        beginTest ("each band's slider at +15 / -15 dB moves that band's centre and leaves far-away bands alone");
        {
            for (int k = 0; k < 7; ++k)
            {
                const double centre = P::bandHz[(size_t) k];
                const double flat = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, centre);
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
                const double boost = run (up, centre) - flat, cut = run (down, centre) - flat;
                logMessage (juce::String (centre, 0) + " Hz band: +15 -> " + juce::String (boost, 1) + " dB, -15 -> " + juce::String (cut, 1) + " dB");
                // the 10 kHz band is the plain RC branch (a shelf that keeps rising): ~8 dB at its nominal centre
                const double minimum = k == 6 ? 6.0 : 9.0;
                expectGreaterThan (boost, minimum);
                expectLessThan (boost, 21.0);
                expectLessThan (cut, -minimum);
                expectGreaterThan (cut, -21.0);
            }
            // a 50 Hz boost does not move 4.5 kHz
            const double far = gainDb ({ 15, 0, 0, 0, 0, 0, 0, 0 }, 4500.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 4500.0);
            expectLessThan (std::abs (far), 1.0);
        }

        beginTest ("Level: +-15 dB across the spectrum");
        {
            const double up = gainDb ({ 0, 0, 0, 0, 0, 0, 0, 15 }, 500.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 500.0);
            const double down = gainDb ({ 0, 0, 0, 0, 0, 0, 0, -15 }, 500.0) - gainDb ({ 0, 0, 0, 0, 0, 0, 0, 0 }, 500.0);
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
            const auto m = probeSine (p, {}, 150.0, 0.6, sr, 0.3, 0.1);
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
            logMessage ("GEB-7: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono)");
        }
    }
};

static GEB7StyleEqualizerProcessorTests geb7StyleEqualizerProcessorTests;

} // namespace openguitarmultifx
