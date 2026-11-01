#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** How much each page-1 switch moves an amp's level (SWITCH_SURVEY=1; AMPS="key,key" narrows the list): every stepped
    control through all its positions, the rest at their defaults, on a guitar (E3) and a bass (E1) note with harmonics.
    Logs only -- a big drop that the real circuit does not make is a bug to chase. */
class AmpSwitchLevelSurvey : public juce::UnitTest
{
public:
    AmpSwitchLevelSurvey() : juce::UnitTest ("AmpSwitchLevelSurvey", "Bench") {}

    static double rmsDb (EffectProcessor& p, double f0)
    {
        constexpr double sr = 48000.0;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        double norm = 0.0;
        for (int k = 1; k <= 10; ++k)
            norm += 0.5 / (double) (k * k);
        const double scale = 0.1 / std::sqrt (norm);
        const int warm = (int) (2.0 * sr), len = (int) (1.0 * sr);
        juce::AudioBuffer<float> buf (1, 256);
        double sOut = 0.0;
        for (long long base = 0; base < warm + len; base += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                double v = 0.0;
                for (int k = 1; k <= 10; ++k)
                    v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                buf.setSample (0, i, (float) (scale * v));
            }
            p.process (buf);
            if (base >= warm)
                for (int i = 0; i < 256; ++i)
                    sOut += juce::square ((double) buf.getSample (0, i));
        }
        return 10.0 * std::log10 (sOut / (double) len + 1.0e-30);
    }

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("SWITCH_SURVEY", {}).isEmpty())
            return;
        beginTest ("switch level survey");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("AMPS", {}), ",", "");
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps || (! only.isEmpty() && ! only.contains (key)))
                continue;
            for (double f0 : { 164.81, 41.2 })
            {
                auto ref = registry.create (key);
                ref->prepare (48000.0, 256, 1);
                const double base = rmsDb (*ref, f0);
                juce::String line = key + (f0 < 100.0 ? " [E1]" : " [E3]") + ": ";
                auto layout = registry.create (key); // keep it alive: its parameters back the pointers below
                const auto pages = layout->getParameterPages();
                for (size_t idx = 0; idx < pages[0].size(); ++idx)
                {
                    auto* p0 = pages[0][idx];
                    const auto range = p0->getNormalisableRange();
                    if (range.interval < 1.0f)
                        continue;
                    line << p0->getName (24) << " {";
                    for (float v = range.start; v <= range.end + 0.01f; v += range.interval)
                    {
                        auto fx = registry.create (key);
                        auto* p = fx->getParameterPages()[0][idx];
                        *p = v;
                        fx->prepare (48000.0, 256, 1);
                        line << p->getCurrentValueAsText() << " " << juce::String (rmsDb (*fx, f0) - base, 1) << "; ";
                    }
                    line << "}  ";
                }
                logMessage (line);
            }
        }
    }
};

static AmpSwitchLevelSurvey ampSwitchLevelSurvey;

/** Hiss each amp makes from an audio interface's own noise floor (NOISE_SURVEY=1): white noise at -90 dBFS rms in,
    every knob at its default; logs the output noise in dBFS and how much the amp amplified it. */
class AmpNoiseSurvey : public juce::UnitTest
{
public:
    AmpNoiseSurvey() : juce::UnitTest ("AmpNoiseSurvey", "Bench") {}

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("NOISE_SURVEY", {}).isEmpty())
            return;
        beginTest ("noise survey");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        constexpr double sr = 48000.0;
        const double inRms = std::pow (10.0, -90.0 / 20.0);
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps)
                continue;
            auto fx = registry.create (key);
            fx->prepare (sr, 256, 1);
            juce::Random rng (7);
            juce::AudioBuffer<float> buf (1, 256);
            double sOut = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (3.0 * sr / 256); ++b)
            {
                for (int i = 0; i < 256; ++i)
                    buf.setSample (0, i, (float) (inRms * std::sqrt (3.0) * (2.0 * rng.nextDouble() - 1.0)));
                fx->process (buf);
                if (b > (int) (1.0 * sr / 256))
                    for (int i = 0; i < 256; ++i, ++n)
                        sOut += juce::square ((double) buf.getSample (0, i));
            }
            const double outDb = 10.0 * std::log10 (sOut / (double) juce::jmax (1LL, n) + 1.0e-30);
            logMessage (key.paddedRight (' ', 40) + juce::String (outDb, 1) + " dBFS out (gain " + juce::String (outDb + 90.0, 1) + " dB)");
        }
    }
};

static AmpNoiseSurvey ampNoiseSurvey;

} // namespace openguitarmultifx
