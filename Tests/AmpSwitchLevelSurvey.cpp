#include "EffectRegistry.h"

#include <juce_dsp/juce_dsp.h>

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

/** Small-signal frequency response of each page-1 switch (SWITCH_FR=1; AMPS="key,key"): every position against the
    first, sine at 40 Hz - 8 kHz and 10 mV so the amp stays linear. For checking switches against a manufacturer's
    spec (e.g. Ampeg's Ultra Hi: +7 dB at 8 kHz and nothing elsewhere). */
class AmpSwitchResponseSurvey : public juce::UnitTest
{
public:
    AmpSwitchResponseSurvey() : juce::UnitTest ("AmpSwitchResponseSurvey", "Bench") {}

    static double sineDb (EffectProcessor& p, double f)
    {
        constexpr double sr = 48000.0;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int warm = (int) (1.0 * sr), len = (int) (0.5 * sr);
        juce::AudioBuffer<float> buf (1, 256);
        double sOut = 0.0;
        for (long long base = 0; base < warm + len; base += 256)
        {
            for (int i = 0; i < 256; ++i)
                buf.setSample (0, i, (float) (0.01 * std::sin (twoPi * f * (double) (base + i) / sr)));
            p.process (buf);
            if (base >= warm)
                for (int i = 0; i < 256; ++i)
                    sOut += juce::square ((double) buf.getSample (0, i));
        }
        return 10.0 * std::log10 (sOut / (double) len + 1.0e-30);
    }

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("SWITCH_FR", {}).isEmpty())
            return;
        beginTest ("switch response survey");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("AMPS", {}), ",", "");
        const double freqs[] = { 40.0, 100.0, 220.0, 500.0, 1000.0, 3000.0, 8000.0 };
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps || (! only.isEmpty() && ! only.contains (key)))
                continue;
            auto layout = registry.create (key);
            const auto pages = layout->getParameterPages();
            for (size_t idx = 0; idx < pages[0].size(); ++idx)
            {
                const auto range = pages[0][idx]->getNormalisableRange();
                if (range.interval < 1.0f)
                    continue;
                std::vector<double> ref;
                for (float v = range.start; v <= range.end + 0.01f; v += range.interval)
                {
                    auto fx = registry.create (key);
                    auto* p = fx->getParameterPages()[0][idx];
                    *p = v;
                    fx->prepare (48000.0, 256, 1);
                    juce::String line = key + " " + p->getName (24) + " = " + p->getCurrentValueAsText() + ":";
                    for (size_t i = 0; i < std::size (freqs); ++i)
                    {
                        const double db = sineDb (*fx, freqs[i]);
                        if (ref.size() < std::size (freqs))
                            ref.push_back (db);
                        line << " " << juce::String (freqs[i], 0) << " " << juce::String (db - ref[i], 1);
                    }
                    logMessage (line);
                }
            }
        }
    }
};

static AmpSwitchResponseSurvey ampSwitchResponseSurvey;

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

/** Aliasing at full drive (ALIAS_SURVEY=1; AMPS="key,key"): every page-1 knob named like gain / volume / drive /
    master / loudness at 1, the rest at their defaults; a 0.1 V sine on an exact FFT bin (330 Hz and 1318 Hz), and the
    share of the output that is NOT on a harmonic of it, in dB. A clean amp sits below -60 dB; a hard clipper rendered
    without enough oversampling folds its upper harmonics back between them (the "failing fuzz" sound). Uses the
    registry's current oversampling tier (OGMFX_QUALITY). */
class AmpAliasSurvey : public juce::UnitTest
{
public:
    AmpAliasSurvey() : juce::UnitTest ("AmpAliasSurvey", "Bench") {}

    static double inharmonicDb (EffectProcessor& p, int bin)
    {
        constexpr double sr = 48000.0;
        constexpr int order = 15, n = 1 << order;
        const double f0 = bin * sr / n;
        const int warm = (int) sr, total = warm + n;
        juce::AudioBuffer<float> buf (1, 256);
        std::vector<float> out ((size_t) n * 2, 0.0f);
        for (int base = 0; base < total; base += 256)
        {
            const int len = juce::jmin (256, total - base);
            for (int i = 0; i < len; ++i)
                buf.setSample (0, i, (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (base + i) / sr)));
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 1, len);
            p.process (view);
            for (int i = 0; i < len; ++i)
                if (base + i >= warm)
                    out[(size_t) (base + i - warm)] = buf.getSample (0, i);
        }
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (out.data());
        double all = 0.0, harm = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double e = juce::square ((double) out[(size_t) k]);
            all += e;
            if (k % bin == 0)
                harm += e;
        }
        return 10.0 * std::log10 ((all - harm) / juce::jmax (1.0e-30, all) + 1.0e-12);
    }

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("ALIAS_SURVEY", {}).isEmpty())
            return;
        beginTest ("alias survey");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto only = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("AMPS", {}), ",", "");
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps || (! only.isEmpty() && ! only.contains (key)))
                continue;
            juce::String line = key.paddedRight (' ', 40);
            for (int bin : { 225, 900 })
            {
                auto fx = registry.create (key);
                const auto pages = fx->getParameterPages(); // a temporary: keep it alive across the loop
                for (auto* p : pages[0])
                {
                    const auto name = p->getName (64).toLowerCase();
                    if (p->getNormalisableRange().interval < 1.0f
                        && (name.contains ("gain") || name.contains ("volume") || name.contains ("drive")
                            || name.contains ("master") || name.contains ("loud")))
                        *p = p->getNormalisableRange().end;
                }
                fx->prepare (48000.0, 256, 1);
                line << (bin == 225 ? "330 Hz " : "  1318 Hz ") << juce::String (inharmonicDb (*fx, bin), 1);
            }
            logMessage (line);
        }
    }
};

static AmpAliasSurvey ampAliasSurvey;

} // namespace openguitarmultifx
