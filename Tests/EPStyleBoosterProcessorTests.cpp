#include "Effects/EPStyleBoosterProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class EPStyleBoosterProcessorTests : public juce::UnitTest
{
public:
    EPStyleBoosterProcessorTests() : juce::UnitTest ("EPStyleBoosterProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setLevel (EPStyleBoosterProcessor& p, float v)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par))
                *f = v;
    }

    static double steadyPeakToPeak (EPStyleBoosterProcessor& p, double freq, double amplitude, int blocks = 400)
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

    static double gainDb (float level, double freq, double amplitude = 0.002)
    {
        EPStyleBoosterProcessor p;
        p.prepare (sr, 128, 1);
        setLevel (p, level);
        return 20.0 * std::log10 (steadyPeakToPeak (p, freq, amplitude, 300) / (2.0 * amplitude));
    }

    void runTest() override
    {
        beginTest ("DC operating point matches the voltages the drawing marks (Q5 drain 14.4 V, source 1.1 V; Q3 base 1.0 V, collector 12.2 V, emitter 0.4 V)");
        {
            EPStyleBoosterProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            logMessage ("Q5 drain " + juce::String (p.debugJfetDrain(), 2) + " V, source " + juce::String (p.debugJfetSource(), 2)
                        + " V; Q3 base " + juce::String (p.debugBjtBase(), 2) + " V, collector " + juce::String (p.debugBjtCollector(), 2)
                        + " V, emitter " + juce::String (p.debugBjtEmitter(), 2) + " V");
            expectWithinAbsoluteError (p.debugJfetDrain(), 14.4, 1.0);
            expectWithinAbsoluteError (p.debugJfetSource(), 1.1, 0.25);
            expectWithinAbsoluteError (p.debugBjtBase(), 1.0, 0.25);
            expectWithinAbsoluteError (p.debugBjtCollector(), 12.2, 1.0);
            expectWithinAbsoluteError (p.debugBjtEmitter(), 0.4, 0.15);
        }

        beginTest ("Level moves the low/mid gain a great deal and monotonically (the 2 nF across the pot and its 47K lets the treble past regardless)");
        {
            double previous = -1.0e9, first = 0.0;
            for (float level : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f, 1.0f })
            {
                const double g = gainDb (level, 100.0);
                logMessage ("Level " + juce::String (level, 1) + ": " + juce::String (g, 1) + " dB at 100 Hz, " + juce::String (gainDb (level, 1000.0), 1) + " dB at 1 kHz, "
                            + juce::String (gainDb (level, 8000.0), 1) + " dB at 8 kHz");
                expect (g > previous, "gain rises with Level");
                if (previous < -1.0e8)
                    first = g;
                previous = g;
            }
            expect (previous - first > 15.0, "a wide range at 100 Hz");
            expect (previous > 10.0, "a real boost at full Level");
        }

        beginTest ("frequency response at full Level: the input high-pass, and a flat-to-warm top (the record pre-emphasis 2 nF is record-side EQ, deliberately not modelled)");
        {
            const double lo = gainDb (1.0f, 30.0), mid = gainDb (1.0f, 300.0), hi = gainDb (1.0f, 8000.0);
            logMessage ("Level 1: 30 Hz " + juce::String (lo, 1) + " dB, 300 Hz " + juce::String (mid, 1) + " dB, 8 kHz " + juce::String (hi, 1) + " dB");
            expect (lo < mid - 3.0, "rolls off below the mids");
            expect (hi <= mid, "no treble emphasis -- only the transistors' natural top roll");
            expect (hi > mid - 8.0, "not muffled either: mid-forward boost stays audible at 8 kHz");
        }

        beginTest ("hot input and random Level moves: finite, and the solver never fails");
        {
            EPStyleBoosterProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (77);
            juce::AudioBuffer<float> buf (2, 128);
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 1200; ++b)
            {
                if (b % 40 == 0)
                    setLevel (p, rng.nextFloat());
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
            logMessage ("solver failure rate " + juce::String (p.getSolveFailureRate(), 6) + ", " + juce::String (p.debugIterations(), 2) + " iterations/sample");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static EPStyleBoosterProcessorTests epStyleBoosterProcessorTests;

} // namespace openguitarmultifx
