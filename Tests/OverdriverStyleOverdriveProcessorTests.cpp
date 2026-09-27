#include "Effects/OverdriverStyleOverdriveProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class OverdriverStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    OverdriverStyleOverdriveProcessorTests() : juce::UnitTest ("OverdriverStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnobs (OverdriverStyleOverdriveProcessor& p, float gain, float bass, float treble)
    {
        const float v[3] = { gain, bass, treble };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 3)
                *f = v[i++];
    }

    static double steadyPeakToPeak (OverdriverStyleOverdriveProcessor& p, double freq, double amplitude, int blocks = 400)
    {
        juce::AudioBuffer<float> buf (1, 128);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                buf.setSample (0, i, (float) (amplitude * std::sin (phase)));
            }
            p.process (buf);
            if (b == blocks - 1)
                for (int i = 0; i < 128; ++i)
                {
                    lo = juce::jmin (lo, (double) buf.getSample (0, i));
                    hi = juce::jmax (hi, (double) buf.getSample (0, i));
                }
        }
        return hi - lo;
    }

    /** Gain in dB relative to a reference frequency, small signal. */
    static double responseDb (float gain, float bass, float treble, double freq, double refFreq)
    {
        OverdriverStyleOverdriveProcessor a, b;
        a.prepare (sr, 128, 1);
        b.prepare (sr, 128, 1);
        setKnobs (a, gain, bass, treble);
        setKnobs (b, gain, bass, treble);
        const double amp = 0.0003;
        return 20.0 * std::log10 (steadyPeakToPeak (a, freq, amp, 300) / steadyPeakToPeak (b, refFreq, amp, 300));
    }

    void runTest() override
    {
        beginTest ("DC operating point matches the voltages the drawing marks (TR2 and TR3: collector 5 V, emitter 1 V)");
        {
            OverdriverStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            logMessage ("TR1 collector " + juce::String (p.debugTr1Collector(), 3) + " V; TR2 collector " + juce::String (p.debugTr2Collector(), 3)
                        + " V, emitter " + juce::String (p.debugTr2Emitter(), 3) + " V; TR3 collector " + juce::String (p.debugTr3Collector(), 3)
                        + " V, emitter " + juce::String (p.debugTr3Emitter(), 3) + " V");
            expectWithinAbsoluteError (p.debugTr2Collector(), 5.0, 1.0);
            expectWithinAbsoluteError (p.debugTr3Collector(), 5.0, 1.0);
            expectWithinAbsoluteError (p.debugTr2Emitter(), 1.0, 0.4);
            expectWithinAbsoluteError (p.debugTr3Emitter(), 1.0, 0.4);
            // TR2's base is TR1's collector: one junction above TR2's emitter.
            expect (p.debugTr1Collector() > p.debugTr2Emitter() + 0.45 && p.debugTr1Collector() < p.debugTr2Emitter() + 0.9,
                    "TR1's collector is one base-emitter drop above TR2's emitter");
        }

        beginTest ("Gain raises the output a great deal (the 25 uF takes the rheostat out of TR1's emitter for signal)");
        {
            OverdriverStyleOverdriveProcessor lo, hi;
            lo.prepare (sr, 128, 1);
            hi.prepare (sr, 128, 1);
            setKnobs (lo, 0.0f, 0.5f, 0.5f);
            setKnobs (hi, 1.0f, 0.5f, 0.5f);
            const double a = steadyPeakToPeak (lo, 440.0, 0.0005), b = steadyPeakToPeak (hi, 440.0, 0.0005);
            logMessage ("0.5 mV in: Gain 0 -> " + juce::String (a, 5) + " p-p, Gain 1 -> " + juce::String (b, 5) + " p-p");
            expect (b > a * 1.5, "more output with Gain up");
        }

        beginTest ("Bass and Treble: each end of the control is a real boost or cut of its own band (checked against the network)");
        {
            // The drawing's 0.1 uF across the whole Bass pot (with 4.7K at each end) makes the control act between
            // 1/(2 pi 100K 0.1u) = 16 Hz and 1/(2 pi 9.4K 0.1u) = 170 Hz, so it is measured in that band, not at 100 Hz alone.
            double bassUp = -1.0e9, bassDown = 1.0e9;
            for (double f : { 30.0, 50.0, 80.0, 120.0 })
            {
                const double up = responseDb (0.3f, 1.0f, 0.5f, f, 1000.0), down = responseDb (0.3f, 0.0f, 0.5f, f, 1000.0);
                logMessage (juce::String (f, 0) + " Hz vs 1 kHz: Bass 1 = " + juce::String (up, 1) + " dB, Bass 0 = " + juce::String (down, 1) + " dB");
                bassUp = juce::jmax (bassUp, up - down);
                bassDown = 0.0;
            }
            const double trebleUp = responseDb (0.3f, 0.5f, 1.0f, 6000.0, 1000.0), trebleDown = responseDb (0.3f, 0.5f, 0.0f, 6000.0, 1000.0);
            logMessage ("6 kHz vs 1 kHz: Treble 1 = " + juce::String (trebleUp, 1) + " dB, Treble 0 = " + juce::String (trebleDown, 1) + " dB");
            expect (bassUp > 6.0, "Bass clockwise boosts the low end (largest swing across the band it acts in)");
            expect (trebleUp > trebleDown + 6.0, "Treble clockwise boosts the high end");
        }

        beginTest ("hot input and random knob moves: finite, and the solver never fails");
        {
            OverdriverStyleOverdriveProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (2031);
            juce::AudioBuffer<float> buf (2, 128);
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 1200; ++b)
            {
                if (b % 40 == 0)
                    setKnobs (p, rng.nextFloat(), rng.nextFloat(), rng.nextFloat());
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
                    finite = finite && std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 50.0f;
            }
            expect (finite, "output stays finite and bounded");
            logMessage ("solver failure rate " + juce::String (p.getSolveFailureRate(), 6) + ", "
                        + juce::String (p.debugIterations(), 2) + " Newton iterations/sample");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static OverdriverStyleOverdriveProcessorTests overdriverStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
