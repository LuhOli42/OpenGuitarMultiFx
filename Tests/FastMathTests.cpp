#include "Effects/FastMath.h"
#include "Effects/PhaserProcessor.h"
#include "Effects/UniVibeProcessor.h"
#include "Effects/PitchModProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/**
    Locks the measured accuracy bounds of the fast LFO/modulation math in
    Source/Effects/FastMath.h against libm, swept densely over each call
    site's real argument range (see that header's doc comment for the
    audibility analysis behind the bounds). If a future change raises a
    sweep range (e.g. a higher Phaser maxFreqHz or a lower minimum sample
    rate), the domain-contract tests here are what fail -- the polynomials
    themselves stay exactly as accurate as fitted.
*/
class FastMathTests : public juce::UnitTest
{
public:
    FastMathTests() : juce::UnitTest ("FastMath", "Effects") {}

    void runTest() override
    {
        constexpr double twoPi = 6.283185307179586;

        beginTest ("sinCycles stays within 5e-8 of libm over the whole period");
        {
            double maxAbs = 0.0, maxRel = 0.0;
            for (int i = 0; i < 4000001; ++i)
            {
                const double u = -0.25 + 1.5 * (double) i / 4000000.0; // wrap edges + both signs
                const double ref = std::sin (twoPi * u);
                const double err = std::abs ((double) fast::sinCycles (u) - ref);
                maxAbs = std::max (maxAbs, err);
                if (std::abs (ref) > 1.0e-3)
                    maxRel = std::max (maxRel, err / std::abs (ref));
            }
            logMessage ("  sinCycles: max abs err " + juce::String (maxAbs, 12) + ", max rel err " + juce::String (maxRel, 12));
            // |err| is bounded below by the float result's own rounding
            // (~6e-8 at |sin| = 1); the double-evaluated poly itself is 3.8e-9.
            expect (maxAbs < 5.0e-8);
            expect (maxRel < 1.0e-7);
        }

        beginTest ("sinRadians/cosRadians keep the same bound on the real phase domain");
        {
            double maxAbsSin = 0.0, maxAbsCos = 0.0;
            for (int i = 0; i < 2000001; ++i)
            {
                const double r = twoPi * (double) i / 2000000.0; // [0, 2pi) -- every LFO phase accumulator's domain
                maxAbsSin = std::max (maxAbsSin, std::abs ((double) fast::sinRadians (r) - std::sin (r)));
                maxAbsCos = std::max (maxAbsCos, std::abs ((double) fast::cosRadians (r) - std::cos (r)));
            }
            logMessage ("  sinRadians: " + juce::String (maxAbsSin, 12) + "  cosRadians: " + juce::String (maxAbsCos, 12));
            expect (maxAbsSin < 5.0e-8);
            expect (maxAbsCos < 5.0e-8);
        }

        beginTest ("tanAllpass stays within 1e-10 relative over the allpass argument domain");
        {
            double maxRel = 0.0, maxAbs = 0.0;
            for (int i = 0; i < 2000001; ++i)
            {
                const double x = -0.25 + 0.5 * (double) i / 2000000.0;
                const double ref = std::tan (x);
                const double err = std::abs (fast::tanAllpass (x) - ref);
                maxAbs = std::max (maxAbs, err);
                if (std::abs (ref) > 1.0e-4)
                    maxRel = std::max (maxRel, err / std::abs (ref));
            }
            logMessage ("  tanAllpass: max abs err " + juce::String (maxAbs, 12) + ", max rel err " + juce::String (maxRel, 12));
            expect (maxAbs < 3.0e-9);
            expect (maxRel < 1.0e-10);
        }

        beginTest ("pow2 stays within 2e-7 relative over the pitch-ratio domain");
        {
            double maxRel = 0.0;
            for (int i = 0; i < 2000001; ++i)
            {
                const float x = -0.5f + 1.0f * (float) i / 2000000.0f;
                const double ref = std::pow (2.0, (double) x);
                maxRel = std::max (maxRel, std::abs (((double) fast::pow2 (x) - ref) / ref));
            }
            logMessage ("  pow2: max rel err " + juce::String (maxRel, 12));
            expect (maxRel < 2.0e-7);
        }

        beginTest ("call-site argument ranges stay inside each approximation's domain");
        {
            // Phaser/UniVibe sweep breakHz up to maxFreqHz; the allpass
            // argument is pi*breakHz/fs. Worst supported fs is 32 kHz
            // (LowSampleRateStabilityTests), giving 0.196 rad at 2 kHz --
            // still inside tanAllpass's 0.25 rad domain.
            constexpr double minSampleRate = 32000.0;
            expect (juce::MathConstants<double>::pi * (double) PhaserProcessor::maxFreqHz / minSampleRate < 0.25);
            expect (juce::MathConstants<double>::pi * (double) UniVibeProcessor::maxFreqHz / minSampleRate < 0.25);

            // PitchMod feeds pow2 with semitones/12, |semitones| <= maxSemitones.
            expect ((double) PitchModProcessor::maxSemitones / 12.0 <= 0.5);
        }
    }
};

static FastMathTests fastMathTests;

} // namespace openguitarmultifx
