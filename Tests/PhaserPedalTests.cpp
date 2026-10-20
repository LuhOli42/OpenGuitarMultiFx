#include "EffectRegistry.h"
#include "Effects/Modulation/Phaser/PhaserParts.h"
#include "Effects/Modulation/Phaser/PhaserPedalProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    The phasers (Source/Effects/Modulation/Phaser): the all-pass stage against its analytic response, the JFET / LDR
    elements, the Phase 90's sweep against ElectroSmash's numbers, and the common contract for every registered phaser
    -- stable (feedback included) on a hot input, sweeping, inside the CPU budget.
*/
class PhaserPedalTests : public juce::UnitTest
{
public:
    PhaserPedalTests() : juce::UnitTest ("PhaserPedals", "Effects") {}

    static constexpr double sr = 48000.0;

    void runTest() override
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;

        beginTest ("AllpassStage: unity magnitude and -90 degrees at f = 1 / (2 pi R C)");
        {
            phaser::AllpassStage st;
            const double r = 24.0e3, c = 47.0e-9, f = 1.0 / (twoPi * r * c); // 141 Hz, the Phase 90's stage with the FET off
            double sumSin = 0.0, sumCos = 0.0, energyIn = 0.0, energyOut = 0.0;
            const int n = (int) sr * 2;
            for (int k = 0; k < n; ++k)
            {
                const double x = std::sin (twoPi * f * k / sr);
                const double y = st.process (x, r, c, sr);
                if (k > n / 2)
                {
                    sumSin += y * std::sin (twoPi * f * k / sr);
                    sumCos += y * std::cos (twoPi * f * k / sr);
                    energyIn += x * x;
                    energyOut += y * y;
                }
            }
            const double phaseDeg = std::atan2 (sumCos, sumSin) * 180.0 / juce::MathConstants<double>::pi;
            logMessage ("stage at " + juce::String (f, 1) + " Hz: gain " + juce::String (std::sqrt (energyOut / energyIn), 4)
                        + ", phase " + juce::String (phaseDeg, 2) + " deg");
            expectWithinAbsoluteError (std::sqrt (energyOut / energyIn), 1.0, 1.0e-3);
            expectWithinAbsoluteError (std::abs (phaseDeg), 90.0, 0.5);
        }

        beginTest ("JFET as a resistor: ~200 ohms near Vgs = 0, rising steeply toward cut-off (2N5952 square law)");
        {
            const phaser::Jfet j { 6.0e-3, -2.5 };
            expectWithinAbsoluteError (j.ohms (0.0, 0.0), 1.0 / (2.0 * 6.0e-3 / 6.25 * 2.5), 1.0);
            expect (j.ohms (-2.4, 0.0) > 10.0 * j.ohms (-0.5, 0.0));
            expect (j.ohms (-2.6, 0.0) > 1.0e8);
        }

        beginTest ("LDR under a lamp: lights fast, darkens slowly (the Uni-Vibe throb)");
        {
            phaser::LdrCell cell;
            cell.prepare (sr, 0.04, 0.012, 0.09);
            cell.reset (0.0);
            int litAt = -1, darkAt = -1;
            for (int k = 0; k < (int) sr; ++k)
                if (cell.process (1.0, 4.0e3, 500.0e3) < 20.0e3 && litAt < 0)
                    litAt = k;
            for (int k = 0; k < (int) sr; ++k)
                if (cell.process (0.0, 4.0e3, 500.0e3) > 100.0e3 && darkAt < 0)
                    darkAt = k;
            logMessage ("LDR: below 20K after " + juce::String (litAt / sr * 1e3, 1) + " ms of light, above 100K after "
                        + juce::String (darkAt / sr * 1e3, 1) + " ms of dark");
            expect (litAt > 0 && darkAt > litAt);
        }

        beginTest ("Phase 90: the first stage's corner sweeps a few hundred Hz to a few kHz (24K || 2N5952, 47n)");
        {
            PhaserPedalProcessor p (PhaserModel::phase90);
            p.prepare (sr, 256, 1);
            juce::AudioBuffer<float> buf (1, 256);
            double lo = 1.0e9, hi = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 256); ++b)
            {
                buf.clear();
                p.process (buf);
                lo = std::min (lo, p.debugCornerHz());
                hi = std::max (hi, p.debugCornerHz());
            }
            logMessage ("Phase 90 stage corner " + juce::String (lo, 0) + " .. " + juce::String (hi, 0) + " Hz");
            expect (lo > 141.0 && lo < 600.0 && hi > 2000.0 && hi < 20000.0);
        }

        EffectRegistry registry;
        registerBuiltInEffects (registry);
        for (const char* key : { "Phase90StylePhaser", "Phase45StylePhaser", "SmallStoneStylePhaser", "PS1StylePhaser", "BiPhaseStylePhaser",
                                 "UniVibe905StylePhaser", "PT909StylePhaser", "MicroFazerStylePhaser", "BarberpolePhaser", "EnvelopePhaser",
                                 "QuadraturePhaser", "StepPhaser" })
        {
            beginTest (juce::String (key) + ": stable with feedback up on a hot input, inside the CPU budget");
            auto fx = registry.create (key);
            fx->prepare (sr, 512, 2);
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    const auto id = f->paramID;
                    if (id.endsWith ("_feedback") || id.endsWith ("_color") || id.endsWith ("_logo"))
                        *f = f->range.end;
                }
            juce::AudioBuffer<float> buf (2, 128);
            double peak = 0.0, lo = 1.0e9, hi = 0.0;
            bool finite = true;
            const auto t0 = std::chrono::steady_clock::now();
            const int blocks = (int) (4.0 * sr / 128);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float x = (float) (1.2 * std::sin (twoPi * 700.0 * (b * 128 + i) / sr) * ((b / 40) % 2 == 0 ? 1.0 : 0.2));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                fx->process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
                if (b > 40 && (b / 40) % 2 == 0)
                {
                    const double r = buf.getRMSLevel (0, 0, 128);
                    lo = std::min (lo, r);
                    hi = std::max (hi, r);
                }
            }
            const double cpu = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 4.0;
            logMessage (juce::String (key) + ": peak " + juce::String (peak, 2) + ", 700 Hz block RMS " + juce::String (lo, 3) + " .. "
                        + juce::String (hi, 3) + ", CPU (stereo in) " + juce::String (cpu, 2) + "%");
            expect (finite);
            expect (peak < 10.0);
            expectLessThan (cpu, 5.0);
            expectGreaterThan (hi - lo, 0.02 * hi); // the notches move past 700 Hz
        }
    }
};

static PhaserPedalTests phaserPedalTests;

} // namespace openguitarmultifx
