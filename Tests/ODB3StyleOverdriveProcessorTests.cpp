#include "Effects/ODB3StyleOverdriveProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class ODB3StyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    ODB3StyleOverdriveProcessorTests() : juce::UnitTest ("ODB3StyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = ODB3StyleOverdriveProcessor;

    /** Output fundamental for a probe at `freq`: params are Level, High, Low, Balance, Gain. */
    static double outAmp (std::initializer_list<float> params, double freq, double amp = 0.1)
    {
        P p;
        p.prepare (sr, 512, 1);
        setParams (p, params);
        return probeSine (p, {}, freq, amp, sr, 0.6, 0.2).out;
    }

    void runTest() override
    {
        // The unit suite probes the transistor-level reference except where a test says otherwise.
        const bool shipped = P::reducedOrder;
        P::reducedOrder = false;
        beginTest ("audible output at noon");
        {
            const double out = outAmp ({ 0.5, 0.5, 0.5, 0.5, 0.5 }, 220.0);
            expectGreaterThan (out, 0.01);
            expect (std::isfinite (out));
        }

        beginTest ("bias: the input JFET follower and both discrete stages sit where the schematic puts them");
        {
            P p;
            p.prepare (sr, 512, 1);
            probeSine (p, {}, 220.0, 0.001, sr, 0.6, 0.1);
            const auto bp = p.getDebugBiasPoint();
            logMessage ("Q1 gate " + juce::String (bp.vGate, 2) + " V, source " + juce::String (bp.vSource, 2)
                        + " V; stage outputs " + juce::String (p.debugStage1Out(), 2) + " / "
                        + juce::String (p.debugStage2Out(), 2) + " V");
            expectWithinAbsoluteError ((double) bp.vGate, 4.0, 0.3);            // R2 1M to Vref
            expectWithinAbsoluteError ((double) bp.vSource, 4.0 + 0.82, 0.4);   // a JFET follower sits ABOVE its gate
            expectWithinAbsoluteError (p.debugStage1Out(), 4.2, 0.8);           // closed-loop at ~Vref (+ the pair's offset)
            expectWithinAbsoluteError (p.debugStage2Out(), 4.4, 0.8);
            expectEquals (p.getClipperFailureRate(), 0.0);
        }

        beginTest ("Balance: 0 is the clean IC1b path (linear), 1 the LED-clipped path (compresses)");
        {
            const double dryLo = outAmp ({ 0.9, 0.5, 0.5, 0.0, 1.0 }, 100.0, 0.05);
            const double dryHi = outAmp ({ 0.9, 0.5, 0.5, 0.0, 1.0 }, 100.0, 0.1);
            const double wetLo = outAmp ({ 0.9, 0.5, 0.5, 1.0, 1.0 }, 100.0, 0.05);
            const double wetHi = outAmp ({ 0.9, 0.5, 0.5, 1.0, 1.0 }, 100.0, 0.1);
            logMessage ("100 Hz x2 input: clean " + juce::String (dryLo, 4) + " -> " + juce::String (dryHi, 4)
                        + ", driven " + juce::String (wetLo, 4) + " -> " + juce::String (wetHi, 4));
            expectGreaterThan (dryLo, 0.01);
            expectWithinAbsoluteError (dryHi / dryLo, 2.0, 0.2);
            expectLessThan (wetHi / wetLo, 1.4);
        }

        beginTest ("Gain saturates: the clipped path stops growing");
        {
            const double lo = outAmp ({ 0.9, 0.5, 0.5, 1.0, 0.1 }, 220.0, 0.05);
            const double hi = outAmp ({ 0.9, 0.5, 0.5, 1.0, 1.0 }, 220.0, 0.05);
            logMessage ("gain 0.1 -> " + juce::String (lo, 3) + ", gain 1 -> " + juce::String (hi, 3));
            expectGreaterThan (hi, lo);
            expectLessThan (hi, 4.0);
        }

        beginTest ("Low and High move their own bands");
        {
            const double lowUp = outAmp ({ 0.9, 0.5, 1.0, 1.0, 0.5 }, 120.0) - outAmp ({ 0.9, 0.5, 0.5, 1.0, 0.5 }, 120.0);
            const double highUp = outAmp ({ 0.9, 1.0, 0.5, 1.0, 0.5 }, 4000.0) - outAmp ({ 0.9, 0.5, 0.5, 1.0, 0.5 }, 4000.0);
            logMessage ("low+ @120Hz " + juce::String (lowUp, 3) + ", high+ @4k " + juce::String (highUp, 3));
            expectGreaterThan (lowUp, 0.0);
            expectGreaterThan (highUp, 0.0);
        }

        beginTest ("reduced (macro stages) matches the transistor-level reference");
        {
            struct Case { std::initializer_list<float> params; double freq, amp; };
            const Case cases[] = { { { 0.5, 0.5, 0.5, 0.5, 0.5 }, 220.0, 0.1 }, { { 0.9, 0.5, 0.5, 1.0, 0.1 }, 220.0, 0.01 },
                                   { { 0.9, 0.5, 0.5, 1.0, 1.0 }, 110.0, 0.1 }, { { 0.9, 0.5, 0.5, 1.0, 0.5 }, 1000.0, 0.02 },
                                   { { 0.9, 0.5, 0.5, 0.0, 0.5 }, 100.0, 0.1 } };
            double worst = 0.0;
            for (const auto& cs : cases)
            {
                P::reducedOrder = false;
                const double full = outAmp (cs.params, cs.freq, cs.amp);
                P::reducedOrder = true;
                const double red = outAmp (cs.params, cs.freq, cs.amp);
                const double db = 20.0 * std::log10 (red / juce::jmax (1.0e-12, full));
                logMessage (juce::String (cs.freq) + " Hz @ " + juce::String (cs.amp) + ": full " + juce::String (full, 4)
                            + ", reduced " + juce::String (red, 4) + " (" + juce::String (db, 2) + " dB)");
                worst = juce::jmax (worst, std::abs (db));
            }
            expectLessThan (worst, 1.0);
        }

        beginTest ("random knob moves, plucked notes and hot bursts: the clippers never fail to converge");
        for (int seed = 85; seed < 87; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
            expectLessThan (p.getClipperFailureRate(), 1.0e-5);
        }
        P::reducedOrder = shipped;
    }
};

static ODB3StyleOverdriveProcessorTests odb3StyleOverdriveProcessorTests;

} // namespace openguitarmultifx
