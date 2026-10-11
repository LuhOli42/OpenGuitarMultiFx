#include "EffectRegistry.h"
#include "Effects/GuvnorStyleDistortionProcessor.h"
#include "Effects/PickupLoadEffect.h"
#include "Effects/SlewRateLimitEffect.h"
#include "Effects/OutputTrimEffect.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

/** Dev diagnostic (GUVDIAG=1): render the DI fixture through the Guv'nor raw and through each wrapper stage and find
    which one produces the sparse-impulse death signature. */
class GuvnorDeathRepro : public juce::UnitTest
{
public:
    GuvnorDeathRepro() : juce::UnitTest ("GuvnorDeathRepro", "Probe") {}

    static bool loadDi (double seconds, std::vector<float>& mono, double& fsOut)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        const auto f = juce::File (OGMFX_TEST_FIXTURES_DIR).getChildFile ("fixtures").getChildFile ("di").getChildFile ("guitar-di.wav");
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return false;
        const int n = (int) juce::jmin ((juce::int64) (seconds * r->sampleRate), r->lengthInSamples);
        juce::AudioBuffer<float> buf ((int) r->numChannels, n);
        r->read (&buf, 0, n, 0, true, true);
        mono.assign ((size_t) n, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
                mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
        fsOut = r->sampleRate;
        return true;
    }

    void runTest() override
    {
        if (std::getenv ("GUVDIAG") == nullptr)
            return;
        beginTest ("guvnor DI death repro");

        std::vector<float> di;
        double fs = 0;
        expect (loadDi (10.0, di, fs));
        float inPeak = 0;
        for (auto v : di)
            inPeak = juce::jmax (inPeak, std::abs (v));
        const float scale = 0.5f / inPeak;

        auto makeChain = [] (int variant) -> std::unique_ptr<EffectProcessor>
        {
            std::unique_ptr<EffectProcessor> p = std::make_unique<GuvnorStyleDistortionProcessor>();
            if (variant >= 1)
                p = std::make_unique<SlewRateLimitEffect> (std::move (p), 13.0);
            if (variant >= 2)
                p = std::make_unique<PickupLoadEffect> (std::move (p), 1000000.0);
            if (variant >= 3)
                p = std::make_unique<OutputTrimEffect> (std::move (p), 4.43f);
            return p;
        };
        const char* names[] = { "raw", "+slew", "+pickup", "+trim", "registry-eco", "registry-high" };
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        for (int variant = 0; variant <= 5; ++variant)
        {
            std::unique_ptr<EffectProcessor> fx;
            if (variant == 4)
            {
                EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::eco);
                fx = registry.create ("GuvnorStyleDistortion");
            }
            else if (variant == 5)
            {
                EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::high);
                fx = registry.create ("GuvnorStyleDistortion");
            }
            else
                fx = makeChain (variant);
            const int chs = 2;
            fx->prepare (fs, 512, chs);
            auto params = fx->getParameters()->getParameters (true);
            const float values[] = { 0.7f, 0.5f, 0.5f, 0.5f, 0.8f };
            for (int i = 0; i < juce::jmin (5, params.size()); ++i)
                *dynamic_cast<juce::AudioParameterFloat*> (params[i]) = values[i];

            juce::AudioBuffer<float> buf (chs, 512);
            long long sparse = 0, windows = 0;
            double outPeak = 0;
            for (size_t base = 0; base + 512 <= di.size(); base += 512)
            {
                for (int ch = 0; ch < chs; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buf.setSample (ch, i, di[base + (size_t) i] * scale);
                fx->process (buf);
                double wPeak = 0, wIn = 0;
                for (int i = 0; i < 512; ++i)
                {
                    wPeak = juce::jmax (wPeak, (double) std::abs (buf.getSample (0, i)));
                    wIn = juce::jmax (wIn, (double) std::abs (di[base + (size_t) i] * scale));
                }
                outPeak = juce::jmax (outPeak, wPeak);
                int zeros = 0;
                for (int i = 0; i < 512; ++i) if (std::abs (buf.getSample (0, i)) < wPeak * 0.02) ++zeros;
                ++windows;
                if ((wIn > 0.05 && wPeak < 0.01) // flatline: output ~50x quieter than input
                    || (wPeak > 0.01 && zeros > 160))
                    ++sparse;
            }
            logMessage (juce::String (names[variant]) + ": sparse " + juce::String (sparse) + "/" + juce::String (windows)
                        + " outPeak " + juce::String (outPeak, 3));
        }
    }
};

static GuvnorDeathRepro guvnorDeathRepro;

} // namespace openguitarmultifx
