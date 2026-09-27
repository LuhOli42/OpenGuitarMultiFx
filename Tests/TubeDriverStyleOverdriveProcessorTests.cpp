#include "Effects/TubeDriverStyleOverdriveProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>

#include "Effects/TubeModels.h"

#include "Effects/PotTaper.h"

namespace openguitarmultifx
{

class TubeDriverStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    TubeDriverStyleOverdriveProcessorTests() : juce::UnitTest ("TubeDriverStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (TubeDriverStyleOverdriveProcessor& p, float drive, float hi, float lo, float level)
    {
        const float v[4] = { drive, hi, lo, level };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 4)
                *f = v[i++];
    }

    /** Peak-to-peak of the last block, of the output (or of the IC1b output when `opAmp`). */
    static double steadyPeakToPeak (TubeDriverStyleOverdriveProcessor& p, double freq, double amplitude, bool opAmp = false, int blocks = 500)
    {
        juce::AudioBuffer<float> buf (1, 1);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        const int total = blocks * 128;
        for (int n = 0; n < total; ++n)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            buf.setSample (0, 0, (float) (amplitude * std::sin (phase)));
            p.process (buf);
            if (n >= total - (int) (sr / freq) * 2 - 8)
            {
                const double v = opAmp ? p.debugOpAmpOutput() : (double) buf.getSample (0, 0);
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
        }
        return hi - lo;
    }

    static double responseDb (float drive, float hi, float lo, float level, double freq, double refFreq, double amplitude = 0.0005)
    {
        TubeDriverStyleOverdriveProcessor a, b;
        a.prepare (sr, 128, 1);
        b.prepare (sr, 128, 1);
        setKnobs (a, drive, hi, lo, level);
        setKnobs (b, drive, hi, lo, level);
        return 20.0 * std::log10 (steadyPeakToPeak (a, freq, amplitude) / steadyPeakToPeak (b, refFreq, amplitude));
    }

    void runTest() override
    {
        beginTest ("supply rails and DC operating point (starved plates on a ~28 V supply)");
        {
            TubeDriverStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            const double vn = TubeDriverStyleOverdriveProcessor::railNegative(), vp = TubeDriverStyleOverdriveProcessor::railPositive();
            logMessage ("rails " + juce::String (vp, 2) + " / " + juce::String (vn, 2) + " V; plate 1 " + juce::String (p.debugPlate1(), 2)
                        + " V, plate 2 " + juce::String (p.debugPlate2(), 2) + " V (above the cathodes: " + juce::String (p.debugPlate1() - vn, 2)
                        + " / " + juce::String (p.debugPlate2() - vn, 2) + "); grids " + juce::String (p.debugGrid1() - vn, 3) + " / "
                        + juce::String (p.debugGrid2() - vn, 3) + " V above V-");
            expect (vp > 12.0 && vp < 17.0 && vn < -11.0 && vn > -17.0, "rails come out at the expected size");
            expect (p.debugPlate1() > vn + 3.0 && p.debugPlate1() < vp - 3.0, "plate 1 sits between the rails, not at one of them");
            expect (p.debugPlate2() > vn + 3.0 && p.debugPlate2() < vp - 3.0, "plate 2 sits between the rails");
            expect (std::abs (p.debugGrid1() - vn) < 0.4 && std::abs (p.debugGrid2() - vn) < 0.4, "grids rest at the cathode (zero bias)");
        }

        beginTest ("Koren 12AX7 plate current in the starved region (logged: the model was validated at 150-450 V for the Bassman)");
        {
            KorenTriode t;
            for (double vpk : { 5.0, 10.0, 20.0, 30.0, 50.0, 100.0, 250.0 })
                logMessage ("Vgk 0, Vpk " + juce::String (vpk, 0) + " V: Ip " + juce::String (t.evaluate (0.0, vpk).ip * 1000.0, 3)
                            + " mA;   Vgk -1: " + juce::String (t.evaluate (-1.0, vpk).ip * 1000.0, 3) + " mA");
            // Zero-bias current must rise with plate voltage, and be near zero at zero plate voltage.
            expect (t.evaluate (0.0, 20.0).ip > t.evaluate (0.0, 10.0).ip && t.evaluate (0.0, 5.0).ip < t.evaluate (0.0, 20.0).ip, "monotonic");
        }

        beginTest ("IC1b gain against the closed-form for a finite-GBW op-amp (small signal)");
        {
            for (float drive : { 0.2f, 0.5f, 0.8f })
            {
                TubeDriverStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnobs (p, drive, 0.5f, 0.5f, 0.5f);
                const double freq = 1000.0, amp = 0.001;
                const double measured = steadyPeakToPeak (p, freq, amp, true) / (2.0 * amp);

                const double rf = 500.0e3 * pots::audio (drive);
                const std::complex<double> j (0.0, 1.0);
                const double w = 2.0 * juce::MathConstants<double>::pi * freq;
                const std::complex<double> zc1 = 1.0 / (j * w * 0.033e-6), zc2 = 1.0 / (j * w * 47.0e-12);
                const std::complex<double> zR2 = 1.0 / (1.0 / 1.0e6 + 1.0 / zc2);
                const std::complex<double> input = zR2 / (10.0e3 + zc1 + zR2);
                const std::complex<double> zf = 1.0 / (1.0 / rf + j * w * 120.0e-12);
                const std::complex<double> zin = 1.5e3 + 1.0 / (j * w * 5.0e-6);
                const std::complex<double> ideal = -zf / zin;
                const std::complex<double> a = 1.0e5 / (1.0 + j * w * 1.0e5 / (2.0 * juce::MathConstants<double>::pi * 3.0e6));
                const std::complex<double> actual = ideal / (1.0 + (1.0 + zf / zin) / a);
                const double expected = std::abs (input * actual);
                logMessage ("Drive " + juce::String (drive, 1) + ": measured " + juce::String (measured, 3) + "x, closed form " + juce::String (expected, 3) + "x");
                expectWithinAbsoluteError (20.0 * std::log10 (measured / expected), 0.0, 0.3);
            }
        }

        beginTest ("Tube Drive and Level raise the output monotonically");
        {
            double prev = -1.0e9;
            for (float drive : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
            {
                TubeDriverStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnobs (p, drive, 0.5f, 0.5f, 0.7f);
                const double v = 20.0 * std::log10 (steadyPeakToPeak (p, 1000.0, 0.0005));
                logMessage ("Drive " + juce::String (drive, 1) + ": " + juce::String (v, 1) + " dB re 1 V p-p");
                expect (v > prev, "output rises with Tube Drive");
                prev = v;
            }
            prev = -1.0e9;
            for (float level : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
            {
                TubeDriverStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnobs (p, 0.5f, 0.5f, 0.5f, level);
                const double v = 20.0 * std::log10 (steadyPeakToPeak (p, 1000.0, 0.0005));
                expect (v > prev, "output rises with Level");
                prev = v;
            }
        }

        beginTest ("Hi and Lo move their own bands the way the labels say (measured on the network)");
        {
            const double hiUp = responseDb (0.5f, 1.0f, 0.5f, 0.7f, 6000.0, 1000.0), hiDown = responseDb (0.5f, 0.0f, 0.5f, 0.7f, 6000.0, 1000.0);
            const double loUp = responseDb (0.5f, 0.5f, 1.0f, 0.7f, 100.0, 1000.0), loDown = responseDb (0.5f, 0.5f, 0.0f, 0.7f, 100.0, 1000.0);
            logMessage ("6 kHz vs 1 kHz: Hi 1 = " + juce::String (hiUp, 1) + " dB, Hi 0 = " + juce::String (hiDown, 1) + " dB");
            logMessage ("100 Hz vs 1 kHz: Lo 1 = " + juce::String (loUp, 1) + " dB, Lo 0 = " + juce::String (loDown, 1) + " dB");
            expect (hiUp > hiDown + 3.0, "Hi clockwise brightens");
            expect (loUp > loDown + 3.0, "Lo clockwise adds low end");
        }

        beginTest ("hot input and random knob moves: finite, and the solver never fails");
        {
            TubeDriverStyleOverdriveProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (4242);
            juce::AudioBuffer<float> buf (2, 128);
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 1200; ++b)
            {
                if (b % 40 == 0)
                    setKnobs (p, rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat());
                const double amp = b % 300 < 40 ? 3.0 : 0.3;
                for (int i = 0; i < 128; ++i)
                {
                    phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / sr;
                    const float x = (float) (amp * std::sin (phase));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                for (int i = 0; i < 128; ++i)
                    finite = finite && std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 100.0f;
            }
            expect (finite, "output stays finite and bounded");
            logMessage ("solver failure rate " + juce::String (p.getSolveFailureRate(), 6) + ", " + juce::String (p.debugIterations(), 2) + " iterations/sample");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static TubeDriverStyleOverdriveProcessorTests tubeDriverStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
