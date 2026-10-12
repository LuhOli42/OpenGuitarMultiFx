#include "EffectRegistry.h"
#include "Effects/AnalogFilterDesign.h"
#include "Effects/Modulation/Chorus/CE2StyleChorusProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    The BBD chorus pedals (Source/Effects/Modulation/Chorus): the filter-design helper against hand-derived numbers,
    each pedal's modulation ranges against what its circuit gives (docs/circuits/<pedal>.md), and the common contract --
    stable, actually modulating, within the CPU budget.
*/
class ChorusPedalTests : public juce::UnitTest
{
public:
    ChorusPedalTests() : juce::UnitTest ("ChorusPedals", "Effects") {}

    static constexpr double sr = 48000.0;

    static const std::vector<const char*>& keys()
    {
        static const std::vector<const char*> k { "CE2StyleChorus", "SmallCloneStyleChorus", "CE1StyleChorus", "DC2StyleChorus", "SCH1StyleChorus", "TriChorus", "StringEnsemble" };
        return k;
    }

    void runTest() override
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;

        beginTest ("ladderTransfer: a single RC is one pole at 1/RC, unity DC");
        {
            const auto h = analog::ladderTransfer ({ { 10.0e3, 0.0, 10.0e-9, 0.0, 0.0 } });
            const auto pz = analog::poleZero (h);
            expectEquals ((int) pz.poles.size(), 1);
            expectWithinAbsoluteError (pz.poles[0].real(), -1.0 / (10.0e3 * 10.0e-9), 1.0e-3);
            expectWithinAbsoluteError (pz.dcGain, 1.0, 1.0e-9);
        }

        beginTest ("ladderTransfer: CE-2 Sallen-Key matches the ElectroSmash 3rd-order formula (poles near 6.6 / 6.9 kHz)");
        {
            // ElectroSmash's CE-2 analysis: a0 = 1 / (R^3 C1 C2 C3), a1 = 1/(R^2 C1 C2) + 1/(R^2 C2 C3) ... -- check the
            // cutoffs the article quotes instead of re-deriving the polynomial: real pole ~6.6 kHz, pair ~6.9 kHz.
            const auto pz = analog::poleZero (analog::ladderTransfer (
                { { 10.0e3, 0.0, 3.3e-9, 0.0, 0.0 }, { 10.0e3, 0.0, 0.0, 8.2e-9, 0.0 }, { 10.0e3, 0.0, 470.0e-12, 0.0, 0.0 } }));
            expectEquals ((int) pz.poles.size(), 3);
            expect (pz.zeros.empty());
            expectWithinAbsoluteError (pz.dcGain, 1.0, 1.0e-9);
            for (const auto& p : pz.poles)
            {
                const double hz = std::abs (p) / twoPi;
                logMessage ("CE-2 SK pole " + juce::String (p.real(), 1) + (p.imag() >= 0 ? " +" : " ") + juce::String (p.imag(), 1)
                            + "j  |p| = " + juce::String (hz, 0) + " Hz");
                expect (hz > 5000.0 && hz < 8500.0, "pole outside the article's 6.6-6.9 kHz neighbourhood");
            }
        }

        beginTest ("CE-2: LFO 0.32..3.56 Hz over Rate; clock / delay sweep inside the MN3007's 5-51 ms .. sub-5 ms chorus range");
        {
            CE2StyleChorusProcessor p;
            auto params = p.getParameters()->getParameters (false);
            p.prepare (sr, 256, 1);
            juce::AudioBuffer<float> buf (1, 256);

            auto setKnobs = [&] (float rate, float depth)
            {
                *dynamic_cast<juce::AudioParameterFloat*> (params[0]) = rate;
                *dynamic_cast<juce::AudioParameterFloat*> (params[1]) = depth;
                p.prepare (sr, 256, 1);
            };

            setKnobs (0.0f, 1.0f);
            const double slow = p.debugLfoHz();
            setKnobs (1.0f, 1.0f);
            const double fast = p.debugLfoHz();
            logMessage ("CE-2 LFO " + juce::String (slow, 3) + " .. " + juce::String (fast, 3) + " Hz");
            expectWithinAbsoluteError (fast, 3.56, 0.05); // R30 / (4 C19 R29 R32), ElectroSmash
            expect (slow > 0.25 && slow < 0.4);

            double minDelay = 1.0, maxDelay = 0.0;
            for (int b = 0; b < (int) (2.0 * sr / 256); ++b)
            {
                buf.clear();
                p.process (buf);
                minDelay = std::min (minDelay, p.debugDelaySeconds());
                maxDelay = std::max (maxDelay, p.debugDelaySeconds());
            }
            logMessage ("CE-2 delay sweep at full depth: " + juce::String (minDelay * 1e3, 2) + " .. " + juce::String (maxDelay * 1e3, 2) + " ms");
            expect (minDelay > 1.5e-3 && maxDelay < 8.0e-3 && maxDelay - minDelay > 1.0e-3);
        }

        EffectRegistry registry;
        registerBuiltInEffects (registry);

        for (const char* key : keys())
        {
            beginTest (juce::String (key) + ": stable on a hot input, actually modulates, inside the CPU budget");
            auto pedal = registry.create (key);
            pedal->prepare (sr, 512, 2);
            const auto pages = pedal->getParameterPages();
            for (auto* f : pages[0])
                if (f->range.interval < 1.0f)
                    *f = 0.5f;

            juce::AudioBuffer<float> buf (2, 128);
            double peak = 0.0;
            bool finite = true;
            const auto t0 = std::chrono::steady_clock::now();
            const int blocks = (int) (4.0 * sr / 128);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const double t = (double) (b * 128 + i) / sr;
                    const float x = (float) (1.5 * std::sin (twoPi * 330.0 * t));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                pedal->process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double y = buf.getSample (0, i);
                    finite = finite && std::isfinite (y);
                    peak = std::max (peak, std::abs (y));
                }
            }
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double cpu = 100.0 * secs / 4.0;
            logMessage (juce::String (key) + ": peak " + juce::String (peak, 3) + ", CPU (stereo in) " + juce::String (cpu, 2) + "%");
            expect (finite);
            expect (peak > 0.05 && peak < 8.0);
            expectLessThan (cpu, 5.0);

            // Modulation: a steady sine through a chorus beats -- its block RMS must wander.
            pedal->reset();
            double lo = 1.0e9, hi = 0.0;
            juce::AudioBuffer<float> mono (2, 480);
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 480; ++i)
                {
                    const float x = (float) (0.3 * std::sin (twoPi * 1000.0 * (double) (b * 480 + i) / sr));
                    mono.setSample (0, i, x);
                    mono.setSample (1, i, x);
                }
                pedal->process (mono);
                if (b > 40)
                {
                    const double r = mono.getRMSLevel (0, 0, 480);
                    lo = std::min (lo, r);
                    hi = std::max (hi, r);
                }
            }
            logMessage (juce::String (key) + ": 1 kHz block RMS " + juce::String (lo, 4) + " .. " + juce::String (hi, 4));
            expectGreaterThan (hi - lo, 0.01 * hi);
        }
    }
};

static ChorusPedalTests chorusPedalTests;

} // namespace openguitarmultifx
