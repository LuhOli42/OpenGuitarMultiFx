#include "EffectRegistry.h"
#include "Effects/Delay/BbdDelayProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    The delays (Source/Effects/Delay): each pedal's delay window where its spec puts it (an impulse's echo arrives when
    it should), the dry path at unity with the echo off, and the common contract -- stable with Repeat / Intensity /
    Swell at full on a hot input (bounded: the pedals that self-oscillate are held by their rails / tape saturation),
    inside the CPU budget.
*/
class DelayPedalTests : public juce::UnitTest
{
public:
    DelayPedalTests() : juce::UnitTest ("DelayPedals", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setParam (EffectProcessor& p, const juce::String& suffix, float v)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                if (f->paramID.endsWith (suffix))
                    *f = v;
    }

    /** Time (s) from an impulse to the loudest output sample after `skip` seconds. */
    static double echoArrival (EffectProcessor& p, double skip, double window)
    {
        p.reset();
        const int len = (int) ((skip + window) * sr);
        juce::AudioBuffer<float> buf (1, 256);
        int best = -1;
        double bestAbs = 0.0;
        for (int base = 0; base < len; base += 256)
        {
            buf.clear();
            if (base == 0)
                for (int i = 0; i < 48; ++i)
                    buf.setSample (0, i, (float) (0.8 * std::sin (juce::MathConstants<double>::pi * i / 48.0))); // a 1 ms bump
            p.process (buf);
            for (int i = 0; i < 256; ++i)
                if (base + i > (int) (skip * sr) && std::abs (buf.getSample (0, i)) > bestAbs)
                {
                    bestAbs = std::abs (buf.getSample (0, i));
                    best = base + i;
                }
        }
        return best / sr;
    }

    void runTest() override
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;

        beginTest ("DM-2: the echo arrives at 20 ms with Repeat Rate at minimum and 300 ms at maximum (its published range)");
        {
            BbdDelayProcessor p (DelayModel::dm2);
            setParam (p, "_intensity", 0.0f);
            setParam (p, "_echo", 1.0f);
            setParam (p, "_rate", 0.0f);
            p.prepare (sr, 256, 1);
            const double shortest = echoArrival (p, 0.008, 0.05);
            setParam (p, "_rate", 1.0f);
            p.prepare (sr, 256, 1);
            const double longest = echoArrival (p, 0.1, 0.3);
            logMessage ("DM-2 echo at " + juce::String (shortest * 1e3, 1) + " ms .. " + juce::String (longest * 1e3, 1) + " ms");
            expectWithinAbsoluteError (shortest, 0.020, 0.004); // + the filters' group delay
            expectWithinAbsoluteError (longest, 0.300, 0.006);
        }

        beginTest ("DD-2: modes S / M / L reach 50 / 200 / 800 ms at full D.Time");
        {
            BbdDelayProcessor p (DelayModel::dd2);
            setParam (p, "_fback", 0.0f);
            setParam (p, "_elevel", 1.0f);
            setParam (p, "_dtime", 1.0f);
            const double expected[3] { 0.050, 0.200, 0.800 };
            for (int m = 0; m < 3; ++m)
            {
                setParam (p, "_mode", (float) m);
                p.prepare (sr, 256, 1);
                const double t = echoArrival (p, expected[m] * 0.5, expected[m]);
                logMessage ("DD-2 mode " + juce::String (m) + ": " + juce::String (t * 1e3, 1) + " ms");
                expectWithinAbsoluteError (t, expected[m], 0.004 + 0.01 * expected[m]);
            }
        }

        EffectRegistry registry;
        registerBuiltInEffects (registry);
        struct Case { const char* key; const char* repeatSuffix; const char* levelSuffix; };
        const Case cases[] {
            { "DM2StyleDelay", "_intensity", "_echo" }, { "MemoryManStyleDelay", "_feedback", nullptr }, { "MXR118StyleDelay", "_regen", nullptr },
            { "AD9StyleDelay", "_repeat", "_level" }, { "AD80StyleDelay", "_repeat", nullptr }, { "FX90StyleDelay", "_repeat", nullptr },
            { "SAD1StyleDelay", "_repeat", "_depth" }, { "DD2StyleDelay", "_fback", "_elevel" },
            { "SpaceEchoStyleDelay", "_intensity", "_echo" }, { "EchoplexStyleDelay", "_repeats", "_echo" }, { "EchorecStyleDelay", "_swell", "_echo" },
            { "DuckingDelay", "_feedback", "_level" }, { "ShimmerDelay", "_feedback", "_level" }, { "LoFiDelay", "_feedback", "_level" },
            { "GranularDelay", "_feedback", "_level" } };

        for (const auto& cs : cases)
        {
            beginTest (juce::String (cs.key) + ": dry at unity with the echo off; stable at full repeat; inside the CPU budget");
            auto fx = registry.create (cs.key);
            fx->prepare (sr, 512, 2);

            // Dry path: echo level off (or Blend / Mix fully dry) -> a 330 Hz sine comes out at its own level.
            if (cs.levelSuffix != nullptr)
                setParam (*fx, cs.levelSuffix, 0.0f);
            setParam (*fx, "_blend", 0.0f);
            setParam (*fx, "_mix", 0.0f);
            setParam (*fx, cs.repeatSuffix, 0.0f);
            const bool coloured = juce::String (cs.key) == "MemoryManStyleDelay" || juce::String (cs.key) == "EchoplexStyleDelay"; // their preamps
            {
                juce::AudioBuffer<float> b (2, 480);
                double sIn = 0.0, sOut = 0.0;
                for (int k = 0; k < 200; ++k)
                {
                    for (int i = 0; i < 480; ++i)
                    {
                        const float x = (float) (0.1 * std::sin (twoPi * 330.0 * (k * 480 + i) / sr));
                        b.setSample (0, i, x);
                        b.setSample (1, i, x);
                        if (k > 100) sIn += (double) x * x;
                    }
                    fx->process (b);
                    if (k > 100)
                        for (int i = 0; i < 480; ++i)
                            sOut += (double) b.getSample (0, i) * b.getSample (0, i);
                }
                const double db = 10.0 * std::log10 (sOut / sIn);
                logMessage (juce::String (cs.key) + ": dry path " + juce::String (db, 2) + " dB");
                if (! coloured)
                    expectWithinAbsoluteError (db, 0.0, 1.0);
            }

            // Full repeat, echo up, hot input: bounded and finite.
            setParam (*fx, cs.repeatSuffix, 1.0f);
            if (cs.levelSuffix != nullptr)
                setParam (*fx, cs.levelSuffix, 1.0f);
            setParam (*fx, "_blend", 0.5f);
            setParam (*fx, "_mix", 0.5f);
            fx->reset();
            juce::AudioBuffer<float> buf (2, 128);
            double peak = 0.0;
            bool finite = true;
            const auto t0 = std::chrono::steady_clock::now();
            const int blocks = (int) (6.0 * sr / 128);
            for (int bl = 0; bl < blocks; ++bl)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const double t = (double) (bl * 128 + i) / sr;
                    const float x = t < 1.0 ? (float) (1.2 * std::sin (twoPi * 196.0 * t)) : 0.0f;
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                fx->process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            const double cpu = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 6.0;
            logMessage (juce::String (cs.key) + ": peak at full repeat " + juce::String (peak, 2) + ", CPU (stereo in) " + juce::String (cpu, 2) + "%");
            expect (finite);
            expect (peak < 20.0);
            expectLessThan (cpu, 5.0);
        }
    }
};

static DelayPedalTests delayPedalTests;

} // namespace openguitarmultifx
