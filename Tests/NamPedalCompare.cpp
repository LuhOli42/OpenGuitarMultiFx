#include "EffectRegistry.h"
#include "Effects/NAMProcessor.h"
#include "Effects/IRLoaderProcessor.h"
#include "Effects/SlewRateLimitEffect.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace openguitarmultifx
{

/** Development bench (NAM_COMPARE=<dir with .nam files>): the modelled pedals against neural captures of real pedals, on the same
    synthetic plucked phrase. Prints, for each unit, the spectral balance (dB relative to 100-1000 Hz) of the output at a few input
    levels, and the harmonics of a 440 Hz sine, so "harsh and hard" can be read as a number: where is the model's top end above the real
    pedal's? Knob positions of the captures are unknown, so compare the SHAPE (slope of bands, harmonic decay), not the loudness. */
class NamPedalCompare : public juce::UnitTest
{
public:
    NamPedalCompare() : juce::UnitTest ("NamPedalCompare", "Bench") {}

    static constexpr double sr = 48000.0;
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;

    static std::vector<float> phrase()
    {
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
                const float next = 0.5f * (line[k] + prev) * 0.9985f;
                prev = line[k];
                line[k] = next;
                y[start + i] += (float) (amp * next * std::exp (-(double) i / (0.9 * sr)) * 2.2);
            }
        };
        std::vector<float> di ((size_t) (9.0 * sr), 0.0f);
        juce::Random rng (7);
        for (double f : { 82.4, 123.5, 164.8, 207.7 })
            pluck (di, (size_t) (0.3 * sr + (f > 100.0 ? 0.03 * sr : 0.0)), f, 3.0, 0.11, rng);
        pluck (di, (size_t) (3.6 * sr), 659.3, 2.0, 0.15, rng);
        pluck (di, (size_t) (5.4 * sr), 987.8, 2.0, 0.15, rng);
        pluck (di, (size_t) (7.0 * sr), 1318.5, 1.8, 0.15, rng);
        return di;
    }

    static void setParamsRaw (EffectProcessor& p, std::initializer_list<float> values)
    {
        auto params = p.getParameters()->getParameters (true);
        size_t i = 0;
        for (float v : values)
            if (i < (size_t) params.size())
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (params[(int) i++]))
                    *f = v;
    }

    static double rms (const std::vector<float>& x)
    {
        double s = 0.0;
        for (float v : x)
            s += (double) v * v;
        return std::sqrt (s / (double) x.size());
    }

    static std::vector<float> run (EffectProcessor& fx, const std::vector<float>& x)
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
    }

    /** Welch-averaged power spectrum. */
    static std::vector<double> welch (const std::vector<float>& x, size_t from)
    {
        juce::dsp::FFT fft (fftOrder);
        std::vector<double> acc ((size_t) fftSize / 2, 0.0);
        std::vector<float> data ((size_t) fftSize * 2);
        int frames = 0;
        for (size_t s = from; s + fftSize <= x.size(); s += fftSize / 2)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                data[(size_t) i] = x[s + (size_t) i] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize));
            fft.performRealOnlyForwardTransform (data.data());
            for (int k = 0; k < fftSize / 2; ++k)
                acc[(size_t) k] += (double) data[2 * (size_t) k] * data[2 * (size_t) k] + (double) data[2 * (size_t) k + 1] * data[2 * (size_t) k + 1];
            ++frames;
        }
        for (auto& a : acc)
            a /= std::max (1, frames);
        return acc;
    }

    static double band (const std::vector<double>& p, double lo, double hi)
    {
        double s = 0.0;
        for (size_t k = 0; k < p.size(); ++k)
        {
            const double f = (double) k * sr / fftSize;
            if (f >= lo && f < hi)
                s += p[k];
        }
        return s;
    }

    static juce::String bands (const std::vector<float>& y)
    {
        const auto p = welch (y, (size_t) (0.3 * sr));
        const double ref = band (p, 100.0, 1000.0);
        juce::String s;
        for (auto [lo, hi] : { std::pair { 1000.0, 2000.0 }, { 2000.0, 4000.0 }, { 4000.0, 8000.0 }, { 8000.0, 16000.0 } })
            s += juce::String (10.0 * std::log10 (band (p, lo, hi) / ref + 1.0e-20), 1).paddedLeft (' ', 7);
        return s;
    }

    static juce::String harmonics (EffectProcessor& fx, double amp)
    {
        std::vector<float> x ((size_t) (2.0 * sr));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / sr));
        const auto y = run (fx, x);
        const auto p = welch (y, (size_t) (0.8 * sr));
        auto bin = [&] (int h)
        {
            const int k = (int) std::round (440.0 * h * fftSize / sr);
            double s = 0.0;
            for (int j = k - 2; j <= k + 2; ++j)
                s += p[(size_t) j];
            return s;
        };
        juce::String s;
        for (int h : { 2, 3, 4, 5, 7, 9, 13, 19 })
            s += juce::String (10.0 * std::log10 (bin (h) / bin (1) + 1.0e-20), 0).paddedLeft (' ', 5);
        return s;
    }

    void runTest() override
    {
        const char* dirEnv = std::getenv ("NAM_COMPARE");
        if (dirEnv == nullptr)
        {
            logMessage ("set NAM_COMPARE=<dir with .nam files> to run");
            return;
        }

        EffectRegistry registry;
        registerBuiltInEffects (registry);
        const auto di = phrase();
        const double diRms = rms (di);

        struct Unit { juce::String name; std::function<std::unique_ptr<EffectProcessor>()> make; };
        std::vector<Unit> units;
        for (const auto& e : std::filesystem::directory_iterator (dirEnv))
            if (e.path().extension() == ".nam")
            {
                const auto path = e.path();
                units.push_back ({ "NAM " + juce::String (path.stem().string()), [path]
                {
                    auto n = std::make_unique<NAMProcessor>();
                    n->prepare (sr, 512, 1);
                    n->loadModel (path);
                    return std::unique_ptr<EffectProcessor> (std::move (n));
                } });
            }
        std::sort (units.begin(), units.end(), [] (const Unit& a, const Unit& b) { return a.name < b.name; });
        const bool ampsOnly = std::getenv ("NAM_COMPARE_AMPS") != nullptr;
        for (const char* key : ampsOnly ? std::initializer_list<const char*> { "BassmanStyleAmplifier", "SuperLeadStyleAmplifier" }
                                        : std::initializer_list<const char*> { "TS808StyleOverdrive", "CentaurStyleOverdrive", "BD2StyleOverdrive", "DS1StyleDistortion", "RatStyleDistortion",
                                 "OCDStyleOverdrive", "ZendriveStyleOverdrive", "BluesBreakerStyleOverdrive", "ToneBenderStyleFuzz", "BigMuffStyleFuzz" })
            units.push_back ({ "model " + juce::String (key), [&registry, key] { return registry.create (key); } });

        if (const char* cabEnv = std::getenv ("NAM_COMPARE_CAB"))
        {
            // the same pedals/amps followed by the app's Cab block with a guitar-speaker IR (a NAM amp+cab capture already has the speaker in it)
            const juce::File ir (cabEnv);
            const size_t n = units.size();
            for (size_t i = 0; i < n; ++i)
                if (units[i].name.startsWith ("model"))
                {
                    auto base = units[i].make;
                    units.push_back ({ units[i].name + " + Cab", [base, ir]
                    {
                        struct Chain : EffectProcessor
                        {
                            std::unique_ptr<EffectProcessor> a, b;
                            void prepare (double sr, int bs, int ch) override { a->prepare (sr, bs, ch); b->prepare (sr, bs, ch); }
                            void process (juce::AudioBuffer<float>& buf) override { a->process (buf); b->process (buf); }
                            void reset() override { a->reset(); b->reset(); }
                            juce::AudioProcessorParameterGroup* getParameters() override { return a->getParameters(); }
                            const char* getName() const override { return "chain"; }
                            juce::Colour getAccentColour() const override { return {}; }
                            void drawIcon (juce::Graphics&, juce::Rectangle<float>) const override {}
                        };
                        auto c = std::make_unique<Chain>();
                        c->a = base();
                        auto cab = std::make_unique<IRLoaderProcessor> ("Cab");
                        cab->prepare (sr, 512, 1);
                        cab->loadModelFile (ir, 0);
                        c->b = std::move (cab);
                        return std::unique_ptr<EffectProcessor> (std::move (c));
                    } });
                }
        }

        if (const char* only = std::getenv ("NAM_COMPARE_FRAMES"))
        {
            // Over time: as each plucked note decays, how do level and top end evolve (real pedals get smoother as the string dies)?
            const juce::StringArray wanted = juce::StringArray::fromTokens (only, ",", "");
            beginTest ("per-frame level and 4-8k / 8-16k relative to <1 kHz over the phrase, input -20 dBFS RMS");
            std::vector<float> x = di;
            const float g = (float) (std::pow (10.0, -20.0 / 20.0) / diRms);
            for (auto& v : x)
                v *= g;
            for (auto& u : units)
            {
                bool take = false;
                for (auto& w : wanted)
                    take = take || u.name.containsIgnoreCase (w);
                if (! take)
                    continue;
                auto fx = u.make();
                const auto y = run (*fx, x);
                logMessage ("== " + u.name);
                for (double t = 0.4; t < 8.6; t += 0.4)
                {
                    std::vector<float> seg (y.begin() + (long) (t * sr), y.begin() + (long) ((t + 0.4) * sr));
                    const auto p = welch (seg, 0);
                    const double lf = band (p, 60.0, 1000.0);
                    logMessage ("t " + juce::String (t, 1) + "  level " + juce::String (20.0 * std::log10 (rms (seg) + 1.0e-12), 1).paddedLeft (' ', 6)
                                + "  4-8k " + juce::String (10.0 * std::log10 (band (p, 4000.0, 8000.0) / lf + 1.0e-20), 1).paddedLeft (' ', 6)
                                + "  8-16k " + juce::String (10.0 * std::log10 (band (p, 8000.0, 16000.0) / lf + 1.0e-20), 1).paddedLeft (' ', 6));
                }
            }
        }

        if (const char* outDir = std::getenv ("NAM_COMPARE_WAVS"))
        {
            // Dev only: WAVs to listen to -- the phrase through the OCD (both clipping modes, three Drives) and, if present, the captures.
            beginTest ("write listening files (NAM_COMPARE_WAVS)");
            const juce::File out (outDir);
            out.createDirectory();
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
            for (double dbfs : { -26.0, -18.0 })
            {
                std::vector<float> x = di;
                const float g = (float) (std::pow (10.0, dbfs / 20.0) / diRms);
                for (auto& v : x)
                    v *= g;
                const juce::String tag = "in" + juce::String ((int) -dbfs) + "dB";
                writeWav ("00_di_" + tag, x);
                for (float drive : { 0.25f, 0.4f, 0.6f })
                    for (int led = 0; led < 2; ++led)
                    {
                        auto fx = registry.create ("OCDStyleOverdrive");
                        setParamsRaw (*fx, { drive, 0.5f, 0.5f, (float) led, 1.0f });
                        writeWav ("OCD_drive" + juce::String (juce::roundToInt (drive * 100)) + (led ? "_LED_" : "_MOSFET_") + tag, run (*fx, x));
                    }
            }
        }

        beginTest ("slew clamp diagnostic: how many output samples actually get slew-limited (dev)");
        {
            for (const char* key : { "RatStyleDistortion", "DS1StyleDistortion", "OCDStyleOverdrive", "BD2StyleOverdrive", "ToneBenderStyleFuzz", "BigMuffStyleFuzz" })
            {
                SlewRateLimitEffect::clampedSamples = 0;
                auto p = registry.create (key);
                p->prepare (sr, 512, 1);
                juce::AudioBuffer<float> b (1, 512);
                for (int block = 0; block < 200; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                        b.setSample (0, i, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * (block * 512 + i) / sr)));
                    p->process (b);
                }
                logMessage (juce::String (key).paddedRight (' ', 24) + juce::String ((long long) SlewRateLimitEffect::clampedSamples) + " / " + juce::String (200 * 512) + " samples clamped");
            }
        }

        beginTest ("bass response check (dev): RMS out/in at 80/150/300/1000/3000 Hz");
        {
            for (const char* key : { "EPStyleBooster", "PositiveGroundBooster", "RangemasterStyleBooster", "TubeDriverStyleOverdrive" })
            {
                juce::String row = juce::String (key).paddedRight (' ', 26);
                bool any = false;
                for (double f : { 80.0, 150.0, 300.0, 1000.0, 3000.0, 6000.0, 10000.0, 15000.0 })
                {
                    std::unique_ptr<EffectProcessor> p;
                    try { p = registry.create (key); } catch (...) {}
                    if (p == nullptr) { row += " (no such key)"; break; }
                    any = true;
                    p->prepare (sr, 512, 1);
                    juce::AudioBuffer<float> b (1, 512);
                    const double amp = 0.05;
                    double e = 0.0; int n = 0;
                    for (int blk = 0; blk < 60; ++blk)
                    {
                        for (int i = 0; i < 512; ++i)
                            b.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * (blk * 512 + i) / sr)));
                        p->process (b);
                        if (blk >= 40)
                            for (int i = 0; i < 512; ++i) { e += (double) b.getSample (0, i) * b.getSample (0, i); ++n; }
                    }
                    row += juce::String (10.0 * std::log10 (e / n / (amp * amp / 2.0) + 1.0e-12), 1).paddedLeft (' ', 8);
                }
                if (any) logMessage (row);
            }
        }

        beginTest ("spectral balance of the phrase (dB re 100-1000 Hz: 1-2k, 2-4k, 4-8k, 8-16k) at input RMS -30 / -20 / -10 dBFS");
        for (double dbfs : { -30.0, -20.0, -10.0 })
        {
            logMessage ("--- input " + juce::String (dbfs, 0) + " dBFS RMS");
            std::vector<float> x = di;
            const float g = (float) (std::pow (10.0, dbfs / 20.0) / diRms);
            for (auto& v : x)
                v *= g;
            for (auto& u : units)
            {
                auto fx = u.make();
                const auto y = run (*fx, x);
                logMessage (u.name.paddedRight (' ', 46) + bands (y) + "   out " + juce::String (20.0 * std::log10 (rms (y) + 1.0e-12), 1) + " dBFS");
            }
        }

        beginTest ("harmonics 2,3,4,5,7,9,13,19 of a 440 Hz sine (dB re fundamental), sine peak 0.03 / 0.1 / 0.3");
        for (double amp : { 0.03, 0.1, 0.3 })
        {
            logMessage ("--- sine peak " + juce::String (amp, 2));
            for (auto& u : units)
            {
                auto fx = u.make();
                logMessage (u.name.paddedRight (' ', 46) + harmonics (*fx, amp));
            }
        }
    }
};

static NamPedalCompare namPedalCompare;

} // namespace openguitarmultifx
