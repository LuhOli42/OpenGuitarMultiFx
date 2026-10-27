#include "EffectRegistry.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstdlib>

namespace openguitarmultifx
{

/** Dev-only (AMP_VOICING=1): do the amps actually sound different? Every amp in AMP_KEYS (comma-separated registry keys) gets the
    same input at its default knobs (AMP_SET="id=value,..." overrides by parameter id, applied where the id exists): a repeated
    decaying "pluck" (a low-E-ish note with a guitar-like partial roll-off) for the spectrum, and a steady 110 Hz sine for the
    harmonic profile. Prints the octave-band balance (dB relative to the amp's own total), the harmonics h2..h7 relative to the
    fundamental, and the RMS distance in dB between every pair's band balance -- a pair under ~1.5 dB reads as "the same amp". */
class AmpVoicingBench : public juce::UnitTest
{
public:
    AmpVoicingBench() : juce::UnitTest ("AmpVoicingBench", "Probe") {}

    static constexpr double sr = 48000.0;
    static constexpr int block = 128;
    static constexpr int fftOrder = 14, fftSize = 1 << fftOrder;
    static constexpr int numBands = 9;
    static constexpr double bandCentres[numBands] { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

    static void applySettings (EffectProcessor& fx)
    {
        if (const char* set = std::getenv ("AMP_SET"))
            for (auto& pair : juce::StringArray::fromTokens (set, ",", ""))
                for (auto* p : fx.getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == pair.upToFirstOccurrenceOf ("=", false, false))
                            *f = pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    }

    static double pluck (long long n, double level)
    {
        const double period = 0.5 * sr; // a new pick every 500 ms
        const double t = std::fmod ((double) n, period) / sr;
        double v = 0.0;
        for (int k = 1; k <= 24; ++k)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * 82.4 * k * t) / (double) k * std::exp (-t * (3.0 + 0.6 * k));
        return level * v;
    }

    void runTest() override
    {
        if (std::getenv ("AMP_VOICING") == nullptr)
            return;

        beginTest ("voicing of each amp at the same settings");
        juce::ScopedNoDenormals noDenormals;
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto keys = juce::StringArray::fromTokens (std::getenv ("AMP_KEYS") != nullptr ? std::getenv ("AMP_KEYS") : "", ",", "");
        const double level = std::getenv ("AMP_LEVEL") != nullptr ? std::atof (std::getenv ("AMP_LEVEL")) : 0.15;

        std::vector<std::array<double, numBands>> balances;
        juce::String header = "  amp                              ";
        for (double c : bandCentres)
            header << juce::String (c < 1000 ? juce::String ((int) c) : juce::String (c / 1000.0, 0) + "k").paddedLeft (' ', 6);
        header << "  |    h2    h3    h4    h5    h6    h7";
        logMessage (header);

        for (auto& key : keys)
        {
            // Spectrum of the plucks.
            auto fx = registry.create (key);
            if (fx == nullptr)
            {
                logMessage ("  " + key + ": not registered");
                continue;
            }
            applySettings (*fx);
            fx->prepare (sr, block, 1);
            juce::dsp::FFT fft (fftOrder);
            juce::dsp::WindowingFunction<float> window (fftSize, juce::dsp::WindowingFunction<float>::hann);
            std::vector<double> power (fftSize / 2, 0.0);
            std::vector<float> seg (2 * fftSize, 0.0f), collected;
            juce::AudioBuffer<float> buf (1, block);
            long long n = 0;
            const long long warm = (long long) (1.0 * sr), total = warm + (long long) (6.0 * sr);
            while (n < total)
            {
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, (float) pluck (n + i, level));
                fx->process (buf);
                if (n >= warm)
                    for (int i = 0; i < block; ++i)
                        collected.push_back (buf.getSample (0, i));
                n += block;
            }
            for (size_t start = 0; start + fftSize <= collected.size(); start += fftSize / 2)
            {
                std::fill (seg.begin(), seg.end(), 0.0f);
                std::copy (collected.begin() + (long) start, collected.begin() + (long) start + fftSize, seg.begin());
                window.multiplyWithWindowingTable (seg.data(), fftSize);
                fft.performFrequencyOnlyForwardTransform (seg.data());
                for (int b = 0; b < fftSize / 2; ++b)
                    power[(size_t) b] += (double) seg[(size_t) b] * seg[(size_t) b];
            }
            std::array<double, numBands> band {};
            double sum = 0.0;
            for (int k = 0; k < numBands; ++k)
            {
                const double lo = bandCentres[k] / std::sqrt (2.0), hi = bandCentres[k] * std::sqrt (2.0);
                for (int b = 1; b < fftSize / 2; ++b)
                {
                    const double f = b * sr / fftSize;
                    if (f >= lo && f < hi)
                        band[(size_t) k] += power[(size_t) b];
                }
                sum += band[(size_t) k];
            }
            for (auto& v : band)
                v = 10.0 * std::log10 (juce::jmax (1.0e-30, v / juce::jmax (1.0e-30, sum)));
            balances.push_back (band);

            // Harmonic profile on a steady sine.
            auto fx2 = registry.create (key);
            applySettings (*fx2);
            fx2->prepare (sr, block, 1);
            const double f0 = 110.0;
            std::array<double, 8> hs {}, hc {};
            n = 0;
            const long long warm2 = (long long) (0.5 * sr), meas = (long long) (sr); // 110 whole cycles
            while (n < warm2 + meas)
            {
                for (int i = 0; i < block; ++i)
                    buf.setSample (0, i, (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (double) (n + i) / sr)));
                fx2->process (buf);
                for (int i = 0; i < block; ++i)
                    if (n + i >= warm2)
                        for (int h = 1; h <= 7; ++h)
                        {
                            const double ph = 2.0 * juce::MathConstants<double>::pi * f0 * h * (double) (n + i) / sr;
                            hs[(size_t) h] += buf.getSample (0, i) * std::sin (ph);
                            hc[(size_t) h] += buf.getSample (0, i) * std::cos (ph);
                        }
                n += block;
            }
            const double fund = std::hypot (hs[1], hc[1]);
            juce::String line = "  " + key.substring (0, 32).paddedRight (' ', 33);
            for (auto v : band)
                line << juce::String (v, 1).paddedLeft (' ', 6);
            line << "  |";
            for (int h = 2; h <= 7; ++h)
                line << juce::String (20.0 * std::log10 (juce::jmax (1.0e-12, std::hypot (hs[(size_t) h], hc[(size_t) h]) / juce::jmax (1.0e-12, fund))), 1).paddedLeft (' ', 6);
            logMessage (line);
        }

        logMessage ("  pairwise RMS difference of band balance (dB), 63 Hz..8 kHz:");
        for (size_t a = 0; a < balances.size(); ++a)
        {
            juce::String line = "  " + keys[(int) a].substring (0, 20).paddedRight (' ', 21);
            for (size_t b = 0; b < balances.size(); ++b)
            {
                double d = 0.0;
                for (int k = 0; k < numBands - 1; ++k)
                    d += juce::square (balances[a][(size_t) k] - balances[b][(size_t) k]);
                line << juce::String (std::sqrt (d / (numBands - 1)), 1).paddedLeft (' ', 6);
            }
            logMessage (line);
        }
    }
};

static AmpVoicingBench ampVoicingBench;

} // namespace openguitarmultifx
