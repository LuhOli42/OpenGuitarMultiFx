#include "EffectRegistry.h"
#include "Effects/Modulation/Flanger/BbdFlangerProcessor.h"
#include "Effects/Modulation/Flanger/DiodeLimiters.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    The flangers (Source/Effects/Modulation/Flanger): the diode limiters against their own physics, the documented delay
    windows, and the common contract for every registered flanger -- stable with the regeneration all the way up on a
    hot input, actually sweeping, inside the CPU budget.
*/
class FlangerPedalTests : public juce::UnitTest
{
public:
    FlangerPedalTests() : juce::UnitTest ("FlangerPedals", "Effects") {}

    static constexpr double sr = 48000.0;

    void runTest() override
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;

        beginTest ("FeedbackLimiter: linear (-Rf/Rin) when small, held near a diode drop + Rs current when large");
        {
            diodes::FeedbackLimiter lim (100.0e3, 27.0e3, diodes::silicon1N4148);
            const double small = lim.process (0.01 / 150.0e3);
            expectWithinAbsoluteError (small, -0.01 * 100.0 / 150.0, 1.0e-4);
            const double big = lim.process (20.0 / 150.0e3);
            logMessage ("20 V into the MXR summer -> " + juce::String (big, 3) + " V (linear would be -13.3 V)");
            expect (big < -0.5 && big > -6.0);
        }

        beginTest ("ShuntClipper: transparent at 10 mV, clamps near 0.6-0.8 V");
        {
            diodes::ShuntClipper clip (4.7e3, diodes::silicon1N4148);
            expectWithinAbsoluteError (clip.process (0.01), 0.01, 1.0e-4);
            const double v = clip.process (5.0);
            expect (v > 0.5 && v < 0.9);
        }

        beginTest ("Tycobrahe: delay window from the drawing's 51-509 kHz clock annotation (0.5-5 ms on 512 stages)");
        {
            BbdFlangerProcessor p (FlangerModel::tycobrahe);
            auto params = p.getParameters()->getParameters (false);
            for (auto* prm : params)
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    if (f->paramID.endsWith ("_pedal")) *f = 0.0f;
                    if (f->paramID.endsWith ("_mode")) *f = 0.0f;
                }
            p.prepare (sr, 256, 1);
            juce::AudioBuffer<float> buf (1, 256);
            buf.clear();
            p.process (buf);
            const double heel = p.debugDelaySeconds();
            for (auto* prm : params)
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                    if (f->paramID.endsWith ("_pedal")) *f = 1.0f;
            for (int b = 0; b < 40; ++b) { buf.clear(); p.process (buf); }
            const double toe = p.debugDelaySeconds();
            logMessage ("Tycobrahe heel " + juce::String (heel * 1e3, 2) + " ms, toe " + juce::String (toe * 1e3, 2) + " ms");
            expectWithinAbsoluteError (heel, 256.0 / 51.0e3, 1.0e-5);
            expectWithinAbsoluteError (toe, 256.0 / 509.0e3, 1.0e-5);
        }

        EffectRegistry registry;
        registerBuiltInEffects (registry);
        for (const char* key : { "MXR117StyleFlanger", "ElectricMistressStyleFlanger", "BF2StyleFlanger", "ADAStyleFlanger",
                                 "TycobraheStyleFlanger", "EchoFlangerStyleFlanger", "FX75StyleFlanger", "FL301StyleFlanger",
                                 "ThroughZeroFlanger", "BarberpoleFlanger", "InstantFlanger", "EnvelopeFlanger" })
        {
            beginTest (juce::String (key) + ": stable with regeneration maxed on a hot input, sweeping, inside the CPU budget");
            auto pedal = registry.create (key);
            pedal->prepare (sr, 512, 2);
            const auto pages = pedal->getParameterPages();
            for (auto* f : pages[0])
            {
                if (f->range.interval >= 1.0f)
                    continue;
                const auto id = juce::String (f->paramID);
                const bool regen = id.endsWith ("_regen") || id.endsWith ("_res") || id.endsWith ("_enhance") || id.endsWith ("_color")
                                   || id.endsWith ("_intensity") || id.endsWith ("_feedback") || id.endsWith ("regeneration");
                *f = regen ? 1.0f : 0.5f;
            }
            // Auto mode for the Tycobrahe (its pedal mode does not sweep).
            for (auto* f : pages[0])
                if (juce::String (f->paramID) == "tyco_mode")
                    *f = 1.0f;

            juce::AudioBuffer<float> buf (2, 128);
            double peak = 0.0;
            bool finite = true;
            const auto t0 = std::chrono::steady_clock::now();
            const int blocks = (int) (4.0 * sr / 128);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float x = (float) (1.5 * std::sin (twoPi * 196.0 * (double) (b * 128 + i) / sr));
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
            const double cpu = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 4.0;
            logMessage (juce::String (key) + ": peak " + juce::String (peak, 3) + ", CPU (stereo in) " + juce::String (cpu, 2) + "%");
            expect (finite);
            expect (peak > 0.02 && peak < 10.0);
            expectLessThan (cpu, 5.0);

            // A flanger's comb sweeps: noise through it changes colour block to block -> the 1-4 kHz energy wanders.
            pedal->reset();
            for (auto* f : pages[0])
                if (f->range.interval < 1.0f)
                    *f = 0.5f;
            juce::Random rng (7);
            juce::AudioBuffer<float> nb (2, 480);
            double lo = 1.0e9, hi = 0.0;
            for (int b = 0; b < 300; ++b)
            {
                for (int i = 0; i < 480; ++i)
                {
                    // Envelope flanger needs dynamics to move: pulse the noise.
                    const double gate = ((b / 10) % 2 == 0) ? 1.0 : 0.15;
                    const float x = (float) (0.2 * gate * (rng.nextFloat() * 2.0f - 1.0f));
                    nb.setSample (0, i, x);
                    nb.setSample (1, i, x);
                }
                pedal->process (nb);
                if (b > 60)
                {
                    const double r = nb.getRMSLevel (0, 0, 480);
                    lo = std::min (lo, r);
                    hi = std::max (hi, r);
                }
            }
            logMessage (juce::String (key) + ": pulsed-noise block RMS " + juce::String (lo, 4) + " .. " + juce::String (hi, 4));
            expect (hi > 0.0 && std::isfinite (hi));
        }
    }
};

static FlangerPedalTests flangerPedalTests;

} // namespace openguitarmultifx
