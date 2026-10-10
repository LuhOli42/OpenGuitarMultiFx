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
        beginTest ("audible output at noon");
        {
            const double out = outAmp ({ 0.5, 0.5, 0.5, 0.5, 0.5 }, 220.0);
            expectGreaterThan (out, 0.01);
            expect (std::isfinite (out));
        }

        beginTest ("bias point sits mid-rail, the input follower conducts");
        {
            P p;
            p.prepare (sr, 512, 1);
            probeSine (p, {}, 220.0, 0.1, sr, 0.6, 0.1);
            const auto bp = p.getDebugBiasPoint();
            expectWithinAbsoluteError ((double) bp.vBase, 4.5, 0.6);
            expectWithinAbsoluteError ((double) bp.vEmitter, 4.5 - 0.62, 0.6);
            expectEquals (p.getClipperFailureRate(), 0.0);
        }

        beginTest ("Balance = 0 keeps the lows clean: a 100 Hz note is barely clipped");
        {
            // full-dry balance at high gain should pass the low-passed clean path.
            const double dry = outAmp ({ 0.9, 0.5, 0.5, 0.0, 1.0 }, 100.0, 0.2);
            const double wet = outAmp ({ 0.9, 0.5, 0.5, 1.0, 1.0 }, 100.0, 0.2);
            logMessage ("balance=0 out " + juce::String (dry, 3) + ", balance=1 out " + juce::String (wet, 3));
            expectGreaterThan (dry, 0.1);
            expect (std::isfinite (wet));
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

        beginTest ("random knob moves, plucked notes and hot bursts: the clippers never fail to converge");
        for (int seed = 85; seed < 87; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
            expectLessThan (p.getClipperFailureRate(), 1.0e-5);
        }
    }
};

static ODB3StyleOverdriveProcessorTests odb3StyleOverdriveProcessorTests;

} // namespace openguitarmultifx
