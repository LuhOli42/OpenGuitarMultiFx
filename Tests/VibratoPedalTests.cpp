#include "EffectRegistry.h"
#include "Effects/Modulation/Vibrato/VibratoPedalProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    The vibratos (Source/Effects/Modulation/Vibrato): every one actually moves the pitch (a 1 kHz sine's period, measured
    zero-crossing to zero-crossing, wanders), the VB-2's Rise Time ramps the depth in, and the common contract --
    finite, bounded, inside the CPU budget.
*/
class VibratoPedalTests : public juce::UnitTest
{
public:
    VibratoPedalTests() : juce::UnitTest ("VibratoPedals", "Effects") {}

    static constexpr double sr = 48000.0;

    /** Largest relative deviation of the instantaneous frequency of a 1 kHz sine after `skip` seconds. */
    static double pitchWander (EffectProcessor& p, double seconds, double skip)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> b (1, 256);
        double lastCross = -1.0, prevY = 0.0, worst = 0.0;
        long long n = 0;
        for (int blk = 0; blk < (int) (seconds * sr / 256); ++blk)
        {
            for (int i = 0; i < 256; ++i)
                b.setSample (0, i, (float) (0.3 * std::sin (twoPi * 1000.0 * (double) (n + i) / sr)));
            p.process (b);
            for (int i = 0; i < 256; ++i, ++n)
            {
                const double y = b.getSample (0, i);
                if (prevY < 0.0 && y >= 0.0)
                {
                    const double t = (double) n - 1.0 + (-prevY) / (y - prevY);
                    if (lastCross >= 0.0 && n > (long long) (skip * sr))
                    {
                        const double f = sr / (t - lastCross);
                        worst = std::max (worst, std::abs (f / 1000.0 - 1.0));
                    }
                    lastCross = t;
                }
                prevY = y;
            }
        }
        return worst;
    }

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        for (const char* key : { "VB2StyleVibrato", "MagnatoneStyleVibrato", "CloneTheoryStyleVibrato", "ScannerVibrato",
                                 "TapeWarbleVibrato", "StereoVibrato", "RandomVibrato" })
        {
            beginTest (juce::String (key) + ": moves the pitch; bounded; inside the CPU budget");
            auto fx = registry.create (key);
            fx->prepare (sr, 512, 1);
            const auto t0 = std::chrono::steady_clock::now();
            const double wander = pitchWander (*fx, 4.0, 1.0);
            const double cpu = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 4.0;
            logMessage (juce::String (key) + ": pitch wanders +-" + juce::String (wander * 100.0, 2) + " % (" +
                        juce::String (1200.0 * std::log2 (1.0 + wander), 1) + " cents), CPU (mono) " + juce::String (cpu, 2) + "%");
            expect (wander > 0.002 && wander < 0.2); // audible (> ~3 cents) and not absurd (< ~3 semitones)
            expectLessThan (cpu, 5.0);
        }

        beginTest ("VB-2: Rise Time ramps the depth in after an engage");
        {
            VibratoPedalProcessor p (VibratoModel::vb2);
            for (auto* prm : p.getParameters()->getParameters (false))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                    if (f->paramID == "vb2_rise") *f = 1.0f; // ~3 s
            p.prepare (sr, 256, 1);
            juce::AudioBuffer<float> b (1, 256);
            b.clear();
            p.process (b);
            const double early = p.debugRise();
            for (int k = 0; k < (int) (3.5 * sr / 256); ++k) { b.clear(); p.process (b); }
            logMessage ("rise after one block " + juce::String (early, 4) + ", after 3.5 s " + juce::String (p.debugRise(), 3));
            expect (early < 0.01 && p.debugRise() > 0.99);
        }
    }
};

static VibratoPedalTests vibratoPedalTests;

} // namespace openguitarmultifx
