#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/**
    Every physically-modelled amp must actually EMIT SOUND: a guitar-like signal in,
    a varying, finite, bounded signal out. PedalUnityLevelTests only checks the output's
    loudness is near unity -- an amp stuck outputting silence at a calibrated trim still
    passes that. This test catches the "no sound comes out" failure mode directly:
    silence, a DC rail, NaN, or a clipped-flat output all fail here.
*/
class AmpAudibleOutputTests : public juce::UnitTest
{
public:
    AmpAudibleOutputTests() : juce::UnitTest ("AmpAudibleOutput", "Effects") {}

    static constexpr double sr = 48000.0;

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        beginTest ("every modeled amp emits audible, finite, bounded, varying output");
        for (const char* key : { "BassmanStyleAmplifier", "SuperLeadStyleAmplifier", "TwinReverbStyleAmplifier",
                                 "DeluxeReverbStyleAmplifier", "JC120StyleAmplifier", "JTM45StyleAmplifier",
                                 "JCM800StyleAmplifier", "AC15StyleAmplifier", "AC30StyleAmplifier",
                                 "SLO100StyleAmplifier", "MarkIICPlusStyleAmplifier", "DualRectifierStyleAmplifier",
                                 "EVH5150StyleAmplifier", "ENGLPowerballStyleAmplifier", "RockerverbStyleAmplifier",
                                 "SVTStyleAmplifier" })
        {
            auto amp = registry.create (key);
            amp->prepare (sr, 512, 1);
            const auto pages = amp->getParameterPages(); // keep alive: iterating a [0] subref of the temporary dangles
            for (auto* f : pages[0]) // page-1 knobs to noon, same exemptions as PedalUnityLevelTests
            {
                if (f->range.interval >= 1.0f) continue;                    // stepped selectors
                if (juce::String (f->paramID).endsWith ("_intensity")) continue; // modulation depth: off, not noon
                *f = juce::jlimit (f->range.start, f->range.end, 0.5f);
            }

            // A plucked low-E string shape: attack burst then decay, so the amp sees real
            // dynamics, not a steady tone its sag could fully settle into.
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            const int warm = (int) (2.0 * sr), plucks = 3, pluckLen = (int) (0.6 * sr);
            const long long total = warm + plucks * (long long) pluckLen;
            juce::AudioBuffer<float> buf (1, 512);
            std::vector<double> out;
            out.reserve ((size_t) plucks * pluckLen);
            bool finite = true;
            for (long long s = -(long long) warm; s < total - warm; s += 512)
            {
                const int len = (int) juce::jmin ((long long) 512, (total - warm) - s);
                for (int i = 0; i < len; ++i)
                {
                    const double t = (double) (s + i) / sr;
                    const double env = std::exp (-4.0 * std::fmod (t + warm / sr, pluckLen / sr));
                    buf.setSample (0, i, (float) (0.5 * env * std::sin (twoPi * 82.4 * t)));
                }
                buf.setSize (1, len, false, false, true);
                amp->process (buf);
                if (s >= 0)
                    for (int i = 0; i < len; ++i)
                    {
                        const double v = buf.getSample (0, i);
                        finite = finite && std::isfinite (v);
                        out.push_back (v);
                    }
            }

            double mean = 0.0, peak = 0.0;
            for (double v : out) { mean += v; peak = juce::jmax (peak, std::abs (v)); }
            mean /= (double) out.size();
            double sDev = 0.0;
            for (double v : out) { const double d = v - mean; sDev += d * d; }
            const double acRms = std::sqrt (sDev / (double) out.size()); // a rail-stuck amp (pure DC out) scores ~0

            logMessage (juce::String (key).paddedRight (' ', 28)
                        + "acRms " + juce::String (acRms, 4)
                        + " peak " + juce::String (peak, 3)
                        + " dc " + juce::String (mean, 4));
            expect (finite, juce::String (key) + ": NaN/Inf in output");
            expect (acRms > 0.005, juce::String (key) + ": inaudible output (silent or stuck on a rail)");
            expect (peak < 1.5, juce::String (key) + ": output clips hard");
            expect (std::abs (mean) < 0.3, juce::String (key) + ": large DC offset on the output");
        }
    }
};

static AmpAudibleOutputTests ampAudibleOutputTests;

} // namespace openguitarmultifx
