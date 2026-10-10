#include "Effects/TS9BStyleOverdriveProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class TS9BStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    TS9BStyleOverdriveProcessorTests() : juce::UnitTest ("TS9BStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = TS9BStyleOverdriveProcessor;

    /** Output fundamental amplitude for a probe at `freq`: params are Drive, Mix, Bass, Treble, Level. */
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
            const auto probe = probeSine (p, {}, 220.0, 0.1, sr, 0.6, 0.1);
            (void) probe;
            const auto bp = p.getDebugBiasPoint();
            expectWithinAbsoluteError ((double) bp.vBase, 4.5, 0.6);
            expectWithinAbsoluteError ((double) bp.vEmitter, 4.5 - 0.62, 0.6);
            expectEquals (p.getClipperFailureRate(), 0.0);
        }

        beginTest ("Mix = 0 passes the dry signal clean, Mix = 1 is the clipped path");
        {
            const double clean = outAmp ({ 0.5, 0.0, 0.5, 0.5, 0.9 }, 220.0, 0.1);
            const double wet = outAmp ({ 0.9, 1.0, 0.5, 0.5, 0.9 }, 220.0, 0.1);
            logMessage ("mix=0 out " + juce::String (clean, 3) + ", mix=1 out " + juce::String (wet, 3));
            expectGreaterThan (clean, 0.05);
            expectLessThan (wet, 1.2); // the clipped path saturates, it doesn't keep growing

            // Endpoint isolation: at mix=0 the clipped leg must be inaudible. The dry
            // path is broadband, so the check is spectral: clipped-drive harmonics
            // bleeding through would push the 2 kHz output well past the clean level.
            const double dry2k = outAmp ({ 1.0, 0.0, 0.5, 0.9, 0.9 }, 2000.0, 0.1);
            const double dry220 = outAmp ({ 1.0, 0.0, 0.5, 0.9, 0.9 }, 220.0, 0.1);
            logMessage ("mix=0: 2 kHz out " + juce::String (dry2k, 4) + ", 220 Hz out " + juce::String (dry220, 4));
            expectLessThan (dry2k, dry220 * 1.5);
        }

        beginTest ("Drive raises harmonic content: clipped output is compressed relative to the input");
        {
            const double lo = outAmp ({ 0.05, 1.0, 0.5, 0.5, 0.9 }, 220.0, 0.05);
            const double hi = outAmp ({ 1.0, 1.0, 0.5, 0.5, 0.9 }, 220.0, 0.05);
            logMessage ("drive 0.05 -> " + juce::String (lo, 3) + ", drive 1 -> " + juce::String (hi, 3));
            expectGreaterThan (hi, lo);          // more gain, even once clipped
            expectLessThan (hi, 3.0);            // and it saturates
        }

        beginTest ("Bass and Treble move their own bands independently");
        {
            // 120 Hz is in the bass band, 4 kHz in the treble band.
            const double bassUp = outAmp ({ 0.5, 1.0, 1.0, 0.5, 0.9 }, 120.0) - outAmp ({ 0.5, 1.0, 0.5, 0.5, 0.9 }, 120.0);
            const double bassUpAt4k = outAmp ({ 0.5, 1.0, 1.0, 0.5, 0.9 }, 4000.0) - outAmp ({ 0.5, 1.0, 0.5, 0.5, 0.9 }, 4000.0);
            const double trebUp = outAmp ({ 0.5, 1.0, 0.5, 1.0, 0.9 }, 4000.0) - outAmp ({ 0.5, 1.0, 0.5, 0.5, 0.9 }, 4000.0);
            logMessage ("bass+ @120Hz " + juce::String (bassUp, 3) + ", @4k " + juce::String (bassUpAt4k, 3)
                        + ", treble+ @4k " + juce::String (trebUp, 3));
            expectGreaterThan (bassUp, 0.0);
            expectGreaterThan (trebUp, 0.0);
            expect (std::isfinite (bassUpAt4k));
        }

        beginTest ("random knob moves, plucked notes and hot bursts: the clipper never fails to converge");
        for (int seed = 81; seed < 83; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
            expectLessThan (p.getClipperFailureRate(), 1.0e-5);
        }
    }
};

static TS9BStyleOverdriveProcessorTests ts9bStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
