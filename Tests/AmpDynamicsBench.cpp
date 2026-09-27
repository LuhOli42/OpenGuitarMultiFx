#include "EffectRegistry.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>

namespace openguitarmultifx
{

/** Dev-only (AMP_DYNAMICS=1): how the amp answers the pick. For a real amp, playing softer must mean less distortion and
    proportionally less output; a model that squashes its input loses exactly that. Prints, per input level, the output
    level and the harmonic distortion. */
class AmpDynamicsBench : public juce::UnitTest
{
public:
    AmpDynamicsBench() : juce::UnitTest ("AmpDynamicsBench", "Probe") {}

    static constexpr double sr = 48000.0;

    void runTest() override
    {
        if (std::getenv ("AMP_DYNAMICS") == nullptr)
            return;

        beginTest ("output level and distortion versus how hard you play");
        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const juce::String key = std::getenv ("AMP_KEY") != nullptr ? std::getenv ("AMP_KEY") : "";
        const double knob = std::getenv ("AMP_KNOB") != nullptr ? std::atof (std::getenv ("AMP_KNOB")) : -1.0;
        logMessage (key.isEmpty() ? "Bassman (direct)" : key + "  first knob at " + juce::String (knob, 2));

        logMessage ("  input (V) | output rms | gain (dB) | THD-ish (harmonics / fundamental)");
        double previousGainDb = 0.0;
        for (double level : { 0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.75, 1.0 })
        {
            std::unique_ptr<EffectProcessor> fx;
            if (key.isNotEmpty())
            {
                fx = registry.create (key);
                auto params = fx->getParameters()->getParameters (true);
                // first knob = drive/gain; leave the rest at their defaults (setting Level to 0 too would just mute it)
                if (knob >= 0.0)
                    for (auto* p : params)
                        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        {
                            *f = (float) knob;
                            break;
                        }
                fx->prepare (sr, 128, 1);
            }
            BassmanStyleAmplifierProcessor amp;
            if (key.isEmpty() && std::getenv ("AMP_VOL") != nullptr)
                for (auto* p : amp.getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == "bm_vol_normal" || f->paramID == "bm_vol_bright")
                            *f = (float) std::atof (std::getenv ("AMP_VOL"));
            if (key.isEmpty())
                amp.prepare (sr, 128, 1);

            const double freq = 196.0;
            const long long warm = (long long) (0.5 * sr), measure = (long long) (0.3 * sr);
            juce::AudioBuffer<float> buf (1, 128);
            double fundS = 0.0, fundC = 0.0, total = 0.0;
            long long n = 0, counted = 0;

            for (long long b = 0; b < (warm + measure) / 128; ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    const double ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) n / sr;
                    const double inGain = std::getenv ("AMP_INGAIN") != nullptr ? std::pow (10.0, std::atof (std::getenv ("AMP_INGAIN")) / 20.0) : 1.0;
                    buf.setSample (0, i, (float) (inGain * level * std::sin (ph)));
                }
                if (key.isEmpty())
                    amp.process (buf);
                else
                    fx->process (buf);
                if (n > warm)
                    for (int i = 0; i < 128; ++i)
                    {
                        const double ph = 2.0 * juce::MathConstants<double>::pi * freq * (double) (n - 128 + i) / sr;
                        const double y = buf.getSample (0, i);
                        fundS += y * std::sin (ph);
                        fundC += y * std::cos (ph);
                        total += y * y;
                        ++counted;
                    }
            }

            const double fundamental = 2.0 * std::sqrt (fundS * fundS + fundC * fundC) / (double) counted;
            const double rms = std::sqrt (total / (double) counted);
            const double fundRms = fundamental / std::sqrt (2.0);
            const double harmonics = std::sqrt (juce::jmax (0.0, rms * rms - fundRms * fundRms));
            const double gainDb = 20.0 * std::log10 (rms / level);
            logMessage ("  " + juce::String (level, 3) + "  |  " + juce::String (rms, 5) + "  |  " + juce::String (gainDb, 2)
                        + " dB  |  " + juce::String (100.0 * harmonics / juce::jmax (1.0e-9, fundRms), 1) + "%"
                        + (level > 0.02 ? "   (gain change from the level below: " + juce::String (gainDb - previousGainDb, 2) + " dB)" : ""));
            previousGainDb = gainDb;
        }
    }
};

static AmpDynamicsBench ampDynamicsBench;

} // namespace openguitarmultifx
