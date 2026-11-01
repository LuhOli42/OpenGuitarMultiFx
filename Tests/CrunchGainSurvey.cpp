#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** Where each amp starts to break up (CRUNCH_SURVEY=1): THD of a 196 Hz sine at single-coil (0.1 V rms) and humbucker
    (0.3 V rms) levels, across the drive knob, every other knob at its default. Compared by hand against where the
    original amps break up (docs/circuits/CrunchGain.md). Logs only; no bounds. AMPS="key,key" narrows the list. */
class CrunchGainSurvey : public juce::UnitTest
{
public:
    CrunchGainSurvey() : juce::UnitTest ("CrunchGainSurvey", "Bench") {}

    static double thdOf (EffectProcessor& fx, double rms, double sr)
    {
        constexpr double f0 = 187.5; // 256 samples per period at 48 kHz: coherent DFT bins
        const int block = 256, warm = (int) (1.0 * sr), measure = 48 * 256;
        juce::AudioBuffer<float> buf (2, block);
        std::vector<double> y;
        const double amp = rms * std::sqrt (2.0);
        for (int n0 = 0; n0 < warm + measure; n0 += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (double) (n0 + i) / sr));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            fx.process (buf);
            if (n0 >= warm)
                for (int i = 0; i < block; ++i)
                    y.push_back (buf.getSample (0, i));
        }
        const auto mag = [&] (int k)
        {
            double re = 0.0, im = 0.0;
            for (size_t i = 0; i < y.size(); ++i)
            {
                const double ph = 2.0 * juce::MathConstants<double>::pi * f0 * k * (double) i / sr;
                re += y[i] * std::cos (ph);
                im += y[i] * std::sin (ph);
            }
            return std::sqrt (re * re + im * im);
        };
        const double fund = mag (1);
        double h = 0.0;
        for (int k = 2; k <= 9; ++k)
            h += juce::square (mag (k));
        return fund > 0.0 ? 100.0 * std::sqrt (h) / fund : 0.0;
    }

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("CRUNCH_SURVEY", {}).isEmpty())
            return;
        beginTest ("break-up survey");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("AMPS", {}), ",", "");
        const double sr = 48000.0;
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps || registry.subcategoryForKey (key) != "Crunch")
                continue;
            if (! only.isEmpty() && ! only.contains (key))
                continue;
            juce::String line = key.paddedRight (' ', 34);
            juce::String driveName;
            for (double knob : { 0.2, 0.35, 0.5, 0.65, 0.8, 1.0 })
            {
                double thd[2] = {};
                for (int level = 0; level < 2; ++level)
                {
                    auto fx = registry.create (key);
                    // The drive knob: every page-1 continuous control named like a volume / gain.
                    const auto pages = fx->getParameterPages(); // keep the pages alive: iterating a temporary's element dangles
                    for (auto* p : pages[0])
                    {
                        const auto name = p->getName (32).toLowerCase();
                        if (p->getNormalisableRange().interval < 1.0f
                            && (name.contains ("volume") || name.contains ("gain") || name.contains ("loud") || name.startsWith ("vol")))
                        {
                            *p = (float) knob;
                            if (driveName.isEmpty())
                                driveName = p->getName (32);
                        }
                    }
                    fx->prepare (sr, 256, 2);
                    thd[level] = thdOf (*fx, level == 0 ? 0.1 : 0.3, sr);
                }
                line << juce::String (knob, 2) << ": " << juce::String (thd[0], 1) << "/" << juce::String (thd[1], 1) << "%  ";
            }
            logMessage (line + "(" + driveName + "; THD % at 0.1 / 0.3 V rms)");
        }
    }
};

static CrunchGainSurvey crunchGainSurvey;

} // namespace openguitarmultifx
