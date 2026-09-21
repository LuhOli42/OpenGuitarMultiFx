#include "Effects/OversampledEffect.h"
#include "Effects/OutputTrimEffect.h"
#include "EffectRegistry.h"

#include "Effects/BD2StyleOverdriveProcessor.h"
#include "Effects/CentaurStyleOverdriveProcessor.h"
#include "Effects/DS1StyleDistortionProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"
#include "Effects/TubeScreamerStyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

namespace
{
    /** A memoryless hard clipper: the simplest thing that generates harmonics far above Nyquist. */
    class HardClipper : public EffectProcessor
    {
    public:
        HardClipper()
        {
            parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("clip", "Clip", "|");
        }

        void prepare (double sr, int maxBlock, int channels) override
        {
            ++prepareCalls;
            preparedRate = sr;
            preparedBlock = maxBlock;
            preparedChannels = channels;
        }

        void process (juce::AudioBuffer<float>& buffer) override
        {
            largestBlock = juce::jmax (largestBlock, buffer.getNumSamples());
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, juce::jlimit (-clipLevel, clipLevel, gain * buffer.getSample (ch, i)));
        }

        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
        const char* getName() const override { return "Hard Clipper"; }

        float gain = 1.0f, clipLevel = 1.0f;
        double preparedRate = 0.0;
        int preparedBlock = 0, preparedChannels = 0, largestBlock = 0, prepareCalls = 0;

    private:
        std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    };
}

class OversampledEffectTests : public juce::UnitTest
{
public:
    OversampledEffectTests() : juce::UnitTest ("OversampledEffect", "Effects") {}

    static constexpr double sr = 48000.0;
    static constexpr int N = 16384, bin0 = 535; // input sine sits exactly on FFT bin 535, so every harmonic is bin-exact

    /** Renders N samples (after a warmup) of a bin-exact sine through the processor. */
    static std::vector<float> render (EffectProcessor& p, double amp, int warmupSamples = 96000, int totalToKeep = N)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> buf (1, 512);
        std::vector<float> out;
        out.reserve ((size_t) totalToKeep);
        long long n = 0;
        while ((int) out.size() < totalToKeep)
        {
            for (int i = 0; i < 512; ++i, ++n)
                buf.setSample (0, i, (float) (amp * std::sin (twoPi * bin0 * (double) n / N)));
            p.process (buf);
            if (n > warmupSamples)
                for (int i = 0; i < 512 && (int) out.size() < totalToKeep; ++i)
                    out.push_back (buf.getSample (0, i));
        }
        return out;
    }

    /** Non-harmonic / harmonic energy in dB: aliasing shows up as energy at bins that are not multiples of the input's. */
    static double aliasDb (const std::vector<float>& x)
    {
        std::vector<float> data (2 * N, 0.0f);
        for (int i = 0; i < N; ++i)
            data[(size_t) i] = x[(size_t) i];
        juce::dsp::FFT fft (14);
        fft.performRealOnlyForwardTransform (data.data(), true);

        double harmonic = 0.0, other = 0.0;
        for (int k = 40; k < N / 2; ++k)
        {
            const double e = (double) data[(size_t) (2 * k)] * data[(size_t) (2 * k)]
                           + (double) data[(size_t) (2 * k + 1)] * data[(size_t) (2 * k + 1)];
            if (k % bin0 == 0 && k / bin0 <= 15)
                harmonic += e;
            else
                other += e;
        }
        return 10.0 * std::log10 (juce::jmax (other, 1.0e-30) / juce::jmax (harmonic, 1.0e-30));
    }

    /** Largest difference between the output and itself one input period later, relative to the output's RMS (dB).
        A steady periodic input must give a periodic output whatever aliasing does (aliased components are periodic
        too), so anything left is noise or instability in the solver itself -- the "hiss" this guards against. */
    static double nonPeriodicDb (EffectProcessor& p, double amp)
    {
        constexpr int period = 96; // 500 Hz at 48 kHz
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> buf (1, 96);
        std::vector<float> y;
        for (int block = 0; block < 4 * 48000 / period + 200; ++block)
        {
            for (int i = 0; i < period; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (twoPi * i / (double) period)));
            p.process (buf);
            if (block >= 4 * 48000 / period)
                for (int i = 0; i < period; ++i)
                    y.push_back (buf.getSample (0, i));
        }
        double err = 0.0, energy = 0.0;
        for (size_t i = 0; i + (size_t) period < y.size(); ++i)
        {
            const double d = (double) y[i + (size_t) period] - (double) y[i];
            err += d * d;
            energy += (double) y[i] * (double) y[i];
        }
        return 10.0 * std::log10 (juce::jmax (err, 1.0e-40) / juce::jmax (energy, 1.0e-40));
    }

    static void setKnobs (EffectProcessor& p, float v)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                *f = juce::jlimit (f->range.start, f->range.end, v);
    }

    void runTest() override
    {
        beginTest ("the inner processor is prepared at the oversampled rate and sees the longer blocks");
        {
            auto* clipper = new HardClipper();
            OversampledEffect wrapped (std::unique_ptr<EffectProcessor> (clipper), 2); // 4x
            wrapped.prepare (sr, 512, 2);
            expectEquals (clipper->preparedRate, sr * 4.0);
            expectEquals (clipper->preparedBlock, 512 * 4);
            expectEquals (clipper->preparedChannels, 2);

            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            wrapped.process (buf);
            expectEquals (clipper->largestBlock, 128 * 4);
        }

        beginTest ("preparing again with identical arguments (the engine does it whenever a block is dragged) is a no-op");
        {
            auto* clipper = new HardClipper();
            OversampledEffect wrapped (std::unique_ptr<EffectProcessor> (clipper), 1);
            wrapped.prepare (sr, 512, 1);
            wrapped.prepare (sr, 512, 1);
            expectEquals (clipper->prepareCalls, 1);
            wrapped.prepare (44100.0, 512, 1);
            expectEquals (clipper->prepareCalls, 2);
        }

        beginTest ("the registry builds the distortion pedals according to the rendering quality (eco = no oversampling)");
        {
            using Q = EffectRegistry::OversamplingQuality;
            const auto original = EffectRegistry::getOversamplingQuality();
            EffectRegistry registry;
            registerBuiltInEffects (registry);
            expect (registry.dependsOnOversamplingQuality ("DS1StyleDistortion"));
            expect (registry.dependsOnOversamplingQuality ("BD2StyleOverdrive"));
            expect (! registry.dependsOnOversamplingQuality ("CentaurStyleOverdrive"));

            auto orderOf = [&] (const char* key, Q quality) -> int
            {
                EffectRegistry::setOversamplingQuality (quality);
                auto pedal = registry.create (key);
                auto* trimmed = dynamic_cast<OutputTrimEffect*> (pedal.get());
                if (trimmed == nullptr)
                    return -1;
                return dynamic_cast<OversampledEffect*> (&trimmed->getInner()) != nullptr ? 1 : 0; // wrapped or not
            };
            expectEquals (orderOf ("DS1StyleDistortion", Q::eco), 0);
            expectEquals (orderOf ("DS1StyleDistortion", Q::balanced), 1);
            expectEquals (orderOf ("DS1StyleDistortion", Q::high), 1);
            expectEquals (orderOf ("OD1StyleOverdrive", Q::balanced), 0); // the OD-1 only oversamples in High
            expectEquals (orderOf ("OD1StyleOverdrive", Q::high), 1);
            EffectRegistry::setOversamplingQuality (original);
        }

        beginTest ("name, parameters and accent colour are the inner processor's");
        {
            OversampledEffect wrapped (std::make_unique<DS1StyleDistortionProcessor>(), 1);
            DS1StyleDistortionProcessor reference;
            expectEquals (juce::String (wrapped.getName()), juce::String (reference.getName()));
            expect (wrapped.getParameters() != nullptr);
            expectEquals (wrapped.getParameters()->getParameters (true).size(), reference.getParameters()->getParameters (true).size());
            expect (wrapped.getAccentColour() == reference.getAccentColour());
        }

        beginTest ("a small signal through a linear inner stage comes out at the same level (the half-band filters are flat)");
        {
            for (int order : { 1, 2 })
            {
                auto* unity = new HardClipper();
                OversampledEffect wrapped (std::unique_ptr<EffectProcessor> (unity), order);
                wrapped.prepare (sr, 512, 1);
                const auto out = render (wrapped, 0.2);
                double rmsIn = 0.0, rmsOut = 0.0;
                for (int i = 0; i < N; ++i)
                    rmsOut += (double) out[(size_t) i] * out[(size_t) i];
                rmsIn = 0.2 * 0.2 * 0.5 * N;
                const double db = 10.0 * std::log10 (rmsOut / rmsIn);
                logMessage ("order " + juce::String (order) + ": level " + juce::String (db, 3) + " dB");
                expectWithinAbsoluteError (db, 0.0, 0.2);
            }
        }

        beginTest ("oversampling a hard clipper cuts its aliasing (non-harmonic energy) with every doubling");
        {
            double previous = 1.0e9;
            const int orders[] = { 0, 1, 2 };
            for (int order : orders)
            {
                auto* clipper = new HardClipper();
                clipper->gain = 20.0f;
                clipper->clipLevel = 0.3f;
                std::unique_ptr<EffectProcessor> chain (clipper);
                if (order > 0)
                    chain = std::make_unique<OversampledEffect> (std::move (chain), order);
                chain->prepare (sr, 512, 1);
                const double db = aliasDb (render (*chain, 0.5));
                logMessage ("hard clipper, " + juce::String (1 << order) + "x: non-harmonic/harmonic = " + juce::String (db, 1) + " dB");
                // A hard clip's spectrum falls ~12 dB per octave, so each doubling can buy about that much.
                if (order > 0)
                    expectLessThan (db, previous - 8.0);
                previous = db;
            }
        }

        beginTest ("circuit pedals are steady-state clean: no solver glitches, no noise floor (output repeats exactly with the input)");
        {
            struct Case { const char* name; std::unique_ptr<EffectProcessor> proc; };
            Case cases[] = {
                { "DS-1", std::make_unique<DS1StyleDistortionProcessor>() },
                { "TS808", std::make_unique<TubeScreamerStyleOverdriveProcessor> (TubeScreamerStyleOverdriveProcessor::Model::ts808) },
                { "Centaur", std::make_unique<CentaurStyleOverdriveProcessor>() },
                { "BD-2", std::make_unique<BD2StyleOverdriveProcessor>() },
                { "HM-2", std::make_unique<HM2StyleDistortionProcessor>() },
            };
            for (auto& c : cases)
            {
                setKnobs (*c.proc, 0.7f);
                c.proc->prepare (sr, 96, 1);
                const double db = nonPeriodicDb (*c.proc, 0.3);
                logMessage (juce::String (c.name) + ": non-periodic error " + juce::String (db, 1) + " dB");
                // The HM-2's IC1B stage used to sit at about -5 dB here (a one-sample-predicted node amplified ~840x).
                expectLessThan (db, -80.0);
            }
        }
    }
};

static OversampledEffectTests oversampledEffectTests;

} // namespace openguitarmultifx
