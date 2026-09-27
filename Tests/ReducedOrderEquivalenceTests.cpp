#include "Effects/BD2StyleOverdriveProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    A reduced-order pedal model must sound like the transistor-level one it replaces: same level (the registry re-trims
    it to unity, so a small offset is fine), same timbre (octave-band spectral shape, level removed), same amount of
    distortion, and no solver trouble. Every reduced model in the project is checked here against its full netlist, on a
    chord with a decaying envelope at three input levels and several knob settings.
*/
class ReducedOrderEquivalenceTests : public juce::UnitTest
{
public:
    ReducedOrderEquivalenceTests() : juce::UnitTest ("ReducedOrderEquivalence", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setParams (EffectProcessor& p, const std::vector<std::pair<const char*, float>>& values)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                for (auto& v : values)
                    if (f->paramID == v.first)
                        *f = v.second;
    }

    /** A decaying three-note chord (E2, B2, E3 with harmonics), `rms` before the pedal. */
    static std::vector<float> render (EffectProcessor& p, double rms, double seconds, double* failureRate = nullptr)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int block = 256;
        const int total = (int) (seconds * sr);
        std::vector<float> out;
        out.reserve ((size_t) total);
        juce::AudioBuffer<float> buf (1, block);
        double norm = 0.0;
        for (int k = 1; k <= 8; ++k)
            norm += 0.5 / (double) (k * k);
        norm *= 3.0;
        const double scale = rms / std::sqrt (norm);
        for (int n0 = 0; n0 < total; n0 += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const double t = (double) (n0 + i) / sr;
                double v = 0.0;
                for (double f0 : { 82.41, 123.47, 164.81 })
                    for (int k = 1; k <= 8; ++k)
                        v += std::sin (twoPi * f0 * k * t + 0.3 * k) / (double) k * std::exp (-t * (0.6 + 0.15 * k));
                buf.setSample (0, i, (float) (scale * v * 1.6));
            }
            p.process (buf);
            for (int i = 0; i < block; ++i)
                out.push_back (buf.getSample (0, i));
        }
        (void) failureRate;
        return out;
    }

    struct Bands { double db[8]; double totalDb; };

    static Bands analyse (const std::vector<float>& x)
    {
        constexpr int order = 15, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) n * 2, 0.0f);
        const size_t start = 12000; // skip the attack's first 250 ms
        for (int i = 0; i < n && start + (size_t) i < x.size(); ++i)
            data[(size_t) i] = x[start + (size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) n));
        fft.performFrequencyOnlyForwardTransform (data.data());
        Bands b {};
        double total = 0.0;
        const double edges[9] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800, 20000 };
        for (int band = 0; band < 8; ++band)
        {
            double e = 0.0;
            for (int k = (int) (edges[band] / sr * n); k < (int) (edges[band + 1] / sr * n); ++k)
                e += (double) data[(size_t) k] * data[(size_t) k];
            b.db[band] = 10.0 * std::log10 (e + 1.0e-30);
            total += e;
        }
        b.totalDb = 10.0 * std::log10 (total + 1.0e-30);
        return b;
    }


    /** Small-signal gain (dB) of the pedal at `freq` for a sine of `amp`. */
    template <typename Pedal>
    static double sineGainDb (const std::vector<std::pair<const char*, float>>& knobs, double freq, double amp, bool reduced)
    {
        const bool defaultReduced = Pedal::reducedOrder;
        Pedal::reducedOrder = reduced;
        Pedal pedal;
        setParams (pedal, knobs);
        pedal.prepare (sr, 256, 1);
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> buf (1, 256);
        double s = 0.0, c = 0.0;
        long long n = 0, count = 0;
        const long long total = (long long) (0.8 * sr), len = (long long) std::llround (std::floor (0.3 * freq) * sr / freq);
        while (n < total)
        {
            for (int i = 0; i < 256; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (twoPi * freq * (double) (n + i) / sr)));
            pedal.process (buf);
            for (int i = 0; i < 256; ++i)
                if (n + i >= total - len)
                {
                    const double ph = twoPi * freq * (double) (n + i) / sr;
                    s += buf.getSample (0, i) * std::sin (ph);
                    c += buf.getSample (0, i) * std::cos (ph);
                    ++count;
                }
            n += 256;
        }
        Pedal::reducedOrder = defaultReduced;
        return 20.0 * std::log10 (2.0 * std::sqrt (s * s + c * c) / (double) count / amp);
    }

    struct Comparison { double levelDb, worstBandDb; bool sane; };

    template <typename Pedal>
    Comparison compare (const std::vector<std::pair<const char*, float>>& knobs, double rms)
    {
        std::vector<float> a, b;
        const bool defaultReduced = Pedal::reducedOrder;
        for (int variant = 0; variant < 2; ++variant)
        {
            Pedal::reducedOrder = variant == 1;
            Pedal pedal;
            setParams (pedal, knobs);
            pedal.prepare (sr, 256, 1);
            (variant == 0 ? a : b) = render (pedal, rms, 3.0);
            if (pedal.getSolveFailureRate() > 0.0005)
            {
                Pedal::reducedOrder = defaultReduced;
                return { 0.0, 99.0, false };
            }
        }
        Pedal::reducedOrder = defaultReduced;
        const auto full = analyse (a), reduced = analyse (b);
        if (juce::SystemStats::getEnvironmentVariable ("BANDS", {}).isNotEmpty())
        {
            juce::String line = "   bands re total, full/reduced:";
            for (int i = 0; i < 8; ++i)
                line += " " + juce::String (full.db[i] - full.totalDb, 1) + "/" + juce::String (reduced.db[i] - reduced.totalDb, 1);
            logMessage (line);
        }
        Comparison c { reduced.totalDb - full.totalDb, 0.0, true };
        for (int i = 0; i < 8; ++i)
            if (full.db[i] > full.totalDb - 30.0) // audible bands only: those holding more than -30 dB of the signal
                c.worstBandDb = std::max (c.worstBandDb, std::abs ((reduced.db[i] - reduced.totalDb) - (full.db[i] - full.totalDb)));
        return c;
    }

    void runTest() override
    {


        beginTest ("HM-2: the one-port gain transistors sound like the two-port ones");
        {
            double worstLevel = 0.0, worstBand = 0.0;
            for (float dist : { 0.1f, 0.5f, 0.9f })
                for (float high : { 0.2f, 0.8f })
                    for (double rms : { 0.02, 0.1, 0.3 })
                    {
                        const auto c = compare<HM2StyleDistortionProcessor> ({ { "hm2_dist", dist }, { "hm2_low", 0.5f }, { "hm2_high", high }, { "hm2_level", 0.5f } }, rms);
                        expect (c.sane, "solver trouble at dist " + juce::String (dist));
                        logMessage ("HM-2 dist " + juce::String (dist) + " high " + juce::String (high) + " in " + juce::String (rms) + ": level " + juce::String (c.levelDb, 2) + " dB, worst audible band " + juce::String (c.worstBandDb, 2) + " dB");
                        worstLevel = std::max (worstLevel, std::abs (c.levelDb));
                        worstBand = std::max (worstBand, c.worstBandDb);
                    }
            logMessage ("HM-2 worst level " + juce::String (worstLevel, 2) + " dB, worst band " + juce::String (worstBand, 2) + " dB");
            expect (worstLevel < 2.0, "level within 2 dB");
            expect (worstBand < 1.2, "timbre within 1.2 dB per audible octave band");
        }

        beginTest ("BD-2: small-signal response of the macro-model matches the transistor-level stages (every Gain, 60 Hz - 8 kHz)");
        {
            double worst = 0.0;
            for (float gain : { 0.0f, 0.15f, 0.5f, 0.9f })
                for (double f : { 60.0, 130.0, 400.0, 1000.0, 3000.0, 8000.0 })
                {
                    const std::vector<std::pair<const char*, float>> knobs { { "bd2_gain", gain }, { "bd2_tone", 0.8f }, { "bd2_level", 0.5f } };
                    const double a = sineGainDb<BD2StyleOverdriveProcessor> (knobs, f, 0.002, false);
                    const double b = sineGainDb<BD2StyleOverdriveProcessor> (knobs, f, 0.002, true);
                    worst = std::max (worst, std::abs (a - b));
                }
            logMessage ("BD-2 worst small-signal difference " + juce::String (worst, 2) + " dB");
            expect (worst < 0.8, "small-signal response within 0.8 dB");
        }

        beginTest ("BD-2: the macro-model gain stages sound like the transistor-level ones");
        {
            double worstLevel = 0.0, worstBand = 0.0;
            for (float gain : { 0.15f, 0.5f, 0.9f })
                for (float tone : { 0.2f, 0.8f })
                    for (double rms : { 0.02, 0.1, 0.3 })
                    {
                        const auto c = compare<BD2StyleOverdriveProcessor> ({ { "bd2_gain", gain }, { "bd2_tone", tone }, { "bd2_level", 0.5f } }, rms);
                        expect (c.sane, "solver trouble at gain " + juce::String (gain));
                        logMessage ("BD-2 gain " + juce::String (gain) + " tone " + juce::String (tone) + " in " + juce::String (rms) + ": level " + juce::String (c.levelDb, 2) + " dB, worst octave band " + juce::String (c.worstBandDb, 2) + " dB");
                        worstLevel = std::max (worstLevel, std::abs (c.levelDb));
                        worstBand = std::max (worstBand, c.worstBandDb);
                    }
            logMessage ("BD-2 worst level " + juce::String (worstLevel, 2) + " dB, worst band " + juce::String (worstBand, 2) + " dB");
            expect (worstLevel < 2.0, "level within 2 dB");
            expect (worstBand < 1.2, "timbre within 1.2 dB per audible octave band");
        }
    }
};

static ReducedOrderEquivalenceTests reducedOrderEquivalenceTests;

} // namespace openguitarmultifx
