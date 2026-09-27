#include "EffectRegistry.h"
#include "Effects/OCDStyleOverdriveProcessor.h"
#include "Effects/FuzzFaceStyleFuzzProcessor.h"
#include "Effects/ToneBenderStyleFuzzProcessor.h"
#include "Effects/OversampledEffect.h"
#include "Effects/OutputTrimEffect.h"
#include "Effects/DS1StyleDistortionProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"
#include "Effects/RatStyleDistortionProcessor.h"
#include "Effects/BD2StyleOverdriveProcessor.h"
#include "Effects/MetalZoneStyleDistortionProcessor.h"

#include "SineProbe.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** Development diagnostics (run by name with PEDAL_DIAG=1): where do the pedals' highs come from? For every drive pedal at its registry
    default: how much of the output is above 4 / 8 / 16 kHz, how much non-harmonic ("alias") energy a pure sine leaves at the eco tier
    against the high tier, and the harmonic profile of a sine -- to compare pedals that "sound alike" with numbers. */
class PedalDiagnostics : public juce::UnitTest
{
public:
    PedalDiagnostics() : juce::UnitTest ("PedalDiagnostics", "Bench") {}

    static constexpr double sr = 48000.0;
    static constexpr int fftOrder = 15, fftSize = 1 << fftOrder;

    static std::vector<float> render (EffectProcessor& p, double freq, double amp, double seconds)
    {
        p.prepare (sr, 512, 1);
        const int total = (int) (seconds * sr);
        std::vector<float> out ((size_t) total);
        juce::AudioBuffer<float> buf (1, 512);
        for (int base = 0; base < total; base += 512)
        {
            const int len = std::min (512, total - base);
            buf.setSize (1, len, false, false, true);
            for (int i = 0; i < len; ++i)
                buf.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) (base + i) / sr)));
            p.process (buf);
            for (int i = 0; i < len; ++i)
                out[(size_t) (base + i)] = buf.getSample (0, i);
        }
        return out;
    }

    /** Power spectrum (linear, per bin) of the last fftSize samples, Hann window. */
    static std::vector<double> spectrum (const std::vector<float>& x)
    {
        juce::dsp::FFT fft (fftOrder);
        std::vector<float> data ((size_t) fftSize * 2, 0.0f);
        const size_t start = x.size() - (size_t) fftSize;
        for (int i = 0; i < fftSize; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (fftSize - 1));
            data[(size_t) i] = (float) (x[start + (size_t) i] * w);
        }
        fft.performRealOnlyForwardTransform (data.data());
        std::vector<double> p ((size_t) fftSize / 2);
        for (size_t k = 0; k < p.size(); ++k)
            p[k] = (double) data[2 * k] * data[2 * k] + (double) data[2 * k + 1] * data[2 * k + 1];
        return p;
    }

    static double bandDb (const std::vector<double>& p, double lo, double hi)
    {
        double e = 0.0, total = 0.0;
        for (size_t k = 1; k < p.size(); ++k)
        {
            const double f = (double) k * sr / fftSize;
            total += p[k];
            if (f >= lo && f < hi)
                e += p[k];
        }
        return 10.0 * std::log10 (juce::jmax (1.0e-30, e) / juce::jmax (1.0e-30, total));
    }

    /** Non-harmonic / harmonic energy for a sine at exactly `bin` bins (so the harmonics fall on bins). */
    static double aliasDb (const std::vector<double>& p, int fundamentalBin)
    {
        double harmonic = 0.0, other = 0.0;
        for (size_t k = 42; k < (size_t) (16000.0 * fftSize / sr); ++k) // 60 Hz .. 16 kHz: sub-audio drift and the near-Nyquist leakage of the decimator are not audible aliasing
        {
            const int r = (int) k % fundamentalBin;
            const bool onHarmonic = r <= 3 || r >= fundamentalBin - 3;
            (onHarmonic ? harmonic : other) += p[k];
        }
        return 10.0 * std::log10 (juce::jmax (1.0e-30, other) / juce::jmax (1.0e-30, harmonic));
    }

    static double harmonicDb (const std::vector<double>& p, int fundamentalBin, int n)
    {
        auto peak = [&] (int centre)
        {
            double e = 0.0;
            for (int k = centre - 3; k <= centre + 3; ++k)
                if (k > 0 && k < (int) p.size())
                    e += p[(size_t) k];
            return e;
        };
        return 10.0 * std::log10 (juce::jmax (1.0e-30, peak (fundamentalBin * n)) / juce::jmax (1.0e-30, peak (fundamentalBin)));
    }

    void runTest() override
    {
        if (std::getenv ("PEDAL_DIAG") == nullptr)
            return;

        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const std::vector<const char*> keys { "TS808StyleOverdrive", "TS9StyleOverdrive", "TS10StyleOverdrive", "CentaurStyleOverdrive", "BD2StyleOverdrive",
                                              "OD1StyleOverdrive", "DOD250StyleOverdrive", "BluesBreakerStyleOverdrive", "ODR1StyleOverdrive",
                                              "OverdriverStyleOverdrive", "OCDStyleOverdrive", "ZendriveStyleOverdrive", "DS1StyleDistortion",
                                              "HM2StyleDistortion", "DistortionPlusStyleDistortion", "GuvnorStyleDistortion", "RatStyleDistortion",
                                              "RAT2StyleDistortion", "TurboRatStyleDistortion", "DT1StyleDistortion", "CrunchBoxStyleDistortion",
                                              "MetalZoneStyleDistortion" };

        const double f0 = 22.0 * sr / fftSize; // 32.2 Hz * 22 = 707 Hz: exactly on a bin
        const int bin0 = 22 * 1; (void) bin0;
        const double sineHz = 32 * sr / fftSize;    // bin 32 = 46.9 Hz? (kept small; harmonics stay in band)
        (void) sineHz;
        const double toneHz = 200.0 * sr / fftSize; // 200 bins = 293 Hz

        beginTest ("drive pedals at their defaults: highs, aliasing at eco vs high, harmonic profile of a 293 Hz sine at 0.1 V");
        for (const char* key : keys)
        {
            std::vector<double> specEco, specHigh;
            for (auto quality : { EffectRegistry::OversamplingQuality::eco, EffectRegistry::OversamplingQuality::high })
            {
                EffectRegistry::setOversamplingQuality (quality);
                auto fx = registry.create (key);
                const auto y = render (*fx, toneHz, 0.1, 3.0);
                (quality == EffectRegistry::OversamplingQuality::eco ? specEco : specHigh) = spectrum (y);
            }
            EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::eco);

            juce::String h;
            for (int n = 2; n <= 9; ++n)
                h += juce::String (harmonicDb (specEco, 200, n), 0).paddedLeft (' ', 5);
            logMessage (juce::String (key).paddedRight (' ', 30) + " alias eco/high: " + juce::String (aliasDb (specEco, 200), 1).paddedLeft (' ', 6)
                        + " / " + juce::String (aliasDb (specHigh, 200), 1).paddedLeft (' ', 6) + " dB | >4k " + juce::String (bandDb (specEco, 4000, 24000), 1)
                        + " (high " + juce::String (bandDb (specHigh, 4000, 24000), 1) + ") >12k " + juce::String (bandDb (specEco, 12000, 24000), 1)
                        + " (high " + juce::String (bandDb (specHigh, 12000, 24000), 1) + ") | H2..H9 (eco):" + h);
        }
        (void) f0;

        beginTest ("sensitivity: THD (H2..H12 against the fundamental, %) of a 293 Hz sine at 8 input levels, each pedal at its default");
        {
            juce::String head = juce::String ("pedal").paddedRight (' ', 30);
            for (double amp : { 0.005, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64 })
                head += (juce::String (amp * 1000.0, 0) + " mV").paddedLeft (' ', 9);
            logMessage (head);
            for (const char* key : keys)
            {
                juce::String row = juce::String (key).paddedRight (' ', 30);
                for (double amp : { 0.005, 0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64 })
                {
                    auto fx = registry.create (key);
                    const auto sp = spectrum (render (*fx, toneHz, amp, 3.0));
                    auto peak = [&] (int centre)
                    {
                        double e = 0.0;
                        for (int k = centre - 3; k <= centre + 3; ++k)
                            if (k > 0 && k < (int) sp.size())
                                e += sp[(size_t) k];
                        return e;
                    };
                    double harm = 0.0;
                    for (int n = 2; n <= 12; ++n)
                        harm += peak (200 * n);
                    row += (juce::String (100.0 * std::sqrt (harm / juce::jmax (1.0e-30, peak (200))), 1) + "%").paddedLeft (' ', 9);
                }
                logMessage (row);
            }
        }

        beginTest ("aliasing of a 1.1 kHz sine (a note high on the neck, or a chord's upper notes) at eco / normal / high, non-harmonic energy vs harmonic energy (dB)");
        for (const char* key : keys)
        {
            juce::String row = juce::String (key).paddedRight (' ', 30);
            for (auto quality : { EffectRegistry::OversamplingQuality::eco, EffectRegistry::OversamplingQuality::balanced, EffectRegistry::OversamplingQuality::high })
            {
                EffectRegistry::setOversamplingQuality (quality);
                auto fx = registry.create (key);
                const auto sp = spectrum (render (*fx, 750.0 * sr / fftSize, 0.1, 3.0));
                row += juce::String (aliasDb (sp, 750), 1).paddedLeft (' ', 9);
            }
            EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::eco);
            logMessage (row);
        }

        if (std::getenv ("PEDAL_DIAG_PEAKS") != nullptr)
        {
            beginTest ("where is the non-harmonic energy of the DS-1 / HM-2 / BD-2 at 4x? (top peaks, dB re fundamental)");
            for (const char* key : { "DS1StyleDistortion", "HM2StyleDistortion", "BD2StyleOverdrive" })
            {
                EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::high);
                auto fx = registry.create (key);
                const auto sp = spectrum (render (*fx, 750.0 * sr / fftSize, 0.1, 3.0));
                EffectRegistry::setOversamplingQuality (EffectRegistry::OversamplingQuality::eco);
                std::vector<std::pair<double, size_t>> peaks;
                for (size_t k = 42; k + 1 < sp.size(); ++k)
                {
                    const int r = (int) k % 750;
                    if (r <= 3 || r >= 750 - 3)
                        continue;
                    if (sp[k] > sp[k - 1] && sp[k] >= sp[k + 1])
                        peaks.push_back ({ sp[k], k });
                }
                std::sort (peaks.rbegin(), peaks.rend());
                double fund = 0.0;
                for (int k = 747; k <= 753; ++k)
                    fund += sp[(size_t) k];
                juce::String row = juce::String (key) + ": ";
                for (size_t i = 0; i < 8 && i < peaks.size(); ++i)
                    row += juce::String ((double) peaks[i].second * sr / fftSize, 0) + " Hz " + juce::String (10.0 * std::log10 (peaks[i].first / fund), 0) + " dB | ";
                logMessage (row);
            }
        }

        if (const char* dir = std::getenv ("PEDAL_AB_DIR"))
        {
            // Dev only: WAV files to listen to. A synthetic plucked-string phrase (a power chord, then two high notes: the worst case for aliasing)
            // through a few hard-clipping pedals at Eco (1x), Normal (2x) and High (4x-8x): if the highs sound harsh at 1x and smooth at High, it is aliasing.
            beginTest ("write A/B files (PEDAL_AB_DIR)");
            const juce::File out (dir);
            out.createDirectory();

            auto pluck = [] (std::vector<float>& y, size_t start, double freq, double seconds, double amp, juce::Random& rng)
            {
                const int period = juce::jmax (2, (int) std::round (sr / freq));
                std::vector<float> line ((size_t) period);
                for (auto& v : line)
                    v = rng.nextFloat() * 2.0f - 1.0f;
                float prev = 0.0f;
                const size_t n = (size_t) (seconds * sr);
                for (size_t i = 0; i < n && start + i < y.size(); ++i)
                {
                    const size_t k = i % (size_t) period;
                    const float next = 0.5f * (line[k] + prev) * 0.9985f; // the string's damping
                    prev = line[k];
                    line[k] = next;
                    y[start + i] += (float) (amp * next * std::exp (-(double) i / (0.9 * sr)) * 2.2);
                }
            };
            std::vector<float> di ((size_t) (9.0 * sr), 0.0f);
            juce::Random rng (7);
            for (double f : { 82.4, 123.5, 164.8, 207.7 })      // an E5 power-ish chord, strummed
                pluck (di, (size_t) (0.3 * sr + (f > 100.0 ? 0.03 * sr : 0.0)), f, 3.0, 0.11, rng);
            pluck (di, (size_t) (3.6 * sr), 659.3, 2.0, 0.15, rng); // E5
            pluck (di, (size_t) (5.4 * sr), 987.8, 2.0, 0.15, rng); // B5
            pluck (di, (size_t) (7.0 * sr), 1318.5, 1.8, 0.15, rng); // E6

            auto writeWav = [&] (const juce::String& name, const std::vector<float>& y)
            {
                juce::WavAudioFormat wav;
                const auto file = out.getChildFile (name + ".wav");
                file.deleteFile();
                std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
                if (stream == nullptr)
                    return;
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0));
                if (writer == nullptr)
                    return;
                stream.release();
                juce::AudioBuffer<float> b (1, (int) y.size());
                for (size_t i = 0; i < y.size(); ++i)
                    b.setSample (0, (int) i, y[i]);
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            };
            writeWav ("00_di_guitar", di);

            auto process = [&] (EffectProcessor& fx, const std::vector<float>& x)
            {
                fx.prepare (sr, 512, 1);
                std::vector<float> y (x.size());
                juce::AudioBuffer<float> buf (1, 512);
                for (size_t base = 0; base < x.size(); base += 512)
                {
                    const int len = (int) std::min<size_t> (512, x.size() - base);
                    buf.setSize (1, len, false, false, true);
                    for (int i = 0; i < len; ++i)
                        buf.setSample (0, i, x[base + (size_t) i]);
                    fx.process (buf);
                    for (int i = 0; i < len; ++i)
                        y[base + (size_t) i] = buf.getSample (0, i);
                }
                return y;
            };

            struct Spec { const char* name; std::function<std::unique_ptr<EffectProcessor>()> make; float trim; };
            const Spec specs[] = {
                { "DS1", [] { return std::make_unique<DS1StyleDistortionProcessor>(); }, 2.87f },
                { "HM2", [] { return std::make_unique<HM2StyleDistortionProcessor>(); }, 6.3f },
                { "RAT", [] { return std::make_unique<RatStyleDistortionProcessor>(); }, 1.17f },
                { "BD2", [] { return std::make_unique<BD2StyleOverdriveProcessor>(); }, -4.42f },
                { "MetalZone", [] { return std::make_unique<MetalZoneStyleDistortionProcessor>(); }, 2.23f },
            };
            int index = 1;
            for (const auto& spec : specs)
            {
                for (int order : { 0, 1, 2 })
                {
                    std::unique_ptr<EffectProcessor> fx = spec.make();
                    if (order > 0)
                        fx = std::make_unique<OversampledEffect> (std::move (fx), order);
                    fx = std::make_unique<OutputTrimEffect> (std::move (fx), spec.trim);
                    const auto y = process (*fx, di);
                    writeWav (juce::String::formatted ("%02d_", index) + spec.name + "_" + juce::String (1 << order) + "x" + (order == 0 ? "_the_old_Eco" : ""), y);
                }
                ++index;
            }
        }

        beginTest ("small-signal gain (dB, 2 mV sine) of the fuzzes by frequency: the transistors' own bandwidth");
        {
            auto row = [&] (const juce::String& name, auto make)
            {
                juce::String line = name.paddedRight (' ', 22);
                for (double f : { 200.0, 1000.0, 3000.0, 6000.0, 12000.0 })
                {
                    auto p = make();
                    p->prepare (sr, 512, 1);
                    const auto y = render (*p, f, 0.002, 1.0);
                    double e = 0.0;
                    for (size_t i = y.size() - 4800; i < y.size(); ++i)
                        e += (double) y[i] * y[i];
                    line += juce::String (10.0 * std::log10 (e / 4800.0 / (0.002 * 0.002 * 0.5)), 1).paddedLeft (' ', 8);
                }
                logMessage (line);
            };
            row ("FuzzFace Ge", [] { return std::make_unique<FuzzFaceStyleFuzzProcessor> (FuzzFaceStyleFuzzProcessor::Model::germanium); });
            row ("FuzzFace Si", [] { return std::make_unique<FuzzFaceStyleFuzzProcessor> (FuzzFaceStyleFuzzProcessor::Model::silicon); });
            row ("ToneBender Ge", [] { return std::make_unique<ToneBenderStyleFuzzProcessor> (ToneBenderStyleFuzzProcessor::Model::germanium); });
            row ("ToneBender Si", [] { return std::make_unique<ToneBenderStyleFuzzProcessor> (ToneBenderStyleFuzzProcessor::Model::silicon); });
        }

        beginTest ("OCD internal peaks (V re bias): stage 1 out, clipper node, stage 2 out, by Drive / input / clipping mode");
        for (float drive : { 0.1f, 0.2f, 0.3f, 0.5f })
            for (double amp : { 0.02, 0.05, 0.1, 0.3 })
            {
                juce::String row = "Drive " + juce::String (drive, 1) + ", " + juce::String (amp, 2) + " V: ";
                for (int led = 0; led < 2; ++led)
                {
                    OCDStyleOverdriveProcessor p;
                    p.prepare (sr, 512, 1);
                    setParams (p, { drive, 0.5f, 0.5f, (float) led, 1.0f });
                    juce::AudioBuffer<float> b (1, 1);
                    double m1 = 0.0, mc = 0.0, m2 = 0.0;
                    for (int i = 0; i < (int) (1.5 * sr); ++i)
                    {
                        b.setSample (0, 0, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / sr)));
                        p.process (b);
                        if (i > (int) (1.0 * sr))
                        {
                            m1 = std::max (m1, std::abs (p.debugStage1Out() - 4.5));
                            mc = std::max (mc, std::abs (p.debugClipNode() - 4.5));
                            m2 = std::max (m2, std::abs (p.debugStage2Out() - 4.5));
                        }
                    }
                    row += juce::String (led ? "LED " : "MOS ") + juce::String (m1, 2) + " / " + juce::String (mc, 2) + " / " + juce::String (m2, 2) + "   ";
                }
                logMessage (row);
            }

        beginTest ("OCD: does the Clipping switch (MOSFET / LED) change the sound? level and harmonics, by Drive and input level");
        for (float drive : { 0.3f, 0.6f, 0.9f })
            for (double amp : { 0.03, 0.1, 0.3 })
            {
                double level[2], h3[2], hf[2];
                for (int led = 0; led < 2; ++led)
                {
                    OCDStyleOverdriveProcessor p;
                    p.prepare (sr, 512, 1);
                    setParams (p, { drive, 0.5f, 0.5f, (float) led, 1.0f });
                    const auto y = render (p, toneHz, amp, 3.0);
                    const auto s = spectrum (y);
                    double e = 0.0;
                    for (size_t i = y.size() - fftSize; i < y.size(); ++i)
                        e += (double) y[i] * y[i];
                    level[led] = 10.0 * std::log10 (e / fftSize);
                    h3[led] = harmonicDb (s, 200, 3);
                    hf[led] = bandDb (s, 4000, 24000);
                }
                logMessage ("Drive " + juce::String (drive, 1) + ", " + juce::String (amp, 2) + " V in: MOSFET level " + juce::String (level[0], 1) + " dB, H3 " + juce::String (h3[0], 1)
                            + ", >4k " + juce::String (hf[0], 1) + " | LED level " + juce::String (level[1], 1) + " dB, H3 " + juce::String (h3[1], 1) + ", >4k " + juce::String (hf[1], 1));
            }
    }
};

static PedalDiagnostics pedalDiagnostics;

} // namespace openguitarmultifx
