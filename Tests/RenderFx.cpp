// Offline renderer: runs a WAV file through one registered effect and writes the
// result. Meant for listening sessions -- the same processor code that runs live,
// driven from a file instead of the sound card.
//
//   OpenGuitarMultiFx_RenderFx <in.wav> <out.wav> <EffectKey> [param=value ...] [--quality=eco|balanced|high|reduced]
//
// Each param=value is matched (case-insensitive) against paramID then display name.
// Values are in the parameter's own units: knob range for AudioParameterFloat,
// choice index for AudioParameterChoice, 0/1 for AudioParameterBool.
//
//   OpenGuitarMultiFx_RenderFx bass-di.wav out.wav SVTStyleAmplifier svt_gain=0.6 svt_speaker=1
//
// CPU benchmark mode (--bench): renders every effect (or the listed keys) at the
// A7S-realistic setup -- 48 kHz, mono, 256-sample blocks (5.33 ms budget) -- once
// per quality tier, and writes a machine-readable CSV instead of a WAV:
//
//   OpenGuitarMultiFx_RenderFx --bench <in.wav> <out.csv|-> [EffectKey ...] [param=value ...]
//                                   [--sr=48000] [--block=256] [--repeat=3] [--model=..] [--model2=..]
//                                   [--nam=<file>] [--ir=<file>]
//
// --nam feeds every Neural* key, --ir feeds the convolution keys (Cab,
// DynamicCab, Reverb -- all IRLoaderProcessor roles; without an IR Reverb
// would measure passthrough, not convolution) -- so a single invocation
// produces the whole table on the device (see tools/fx-audit/
// run_cpu_bench.py for the parallel per-key variant used for the x86 table).
//
// Tiers compared: "high" (OversamplingQuality::high, the desktop reference) and
// "reduced" (OversamplingQuality::eco -- the app's embedded default tier, minimal
// oversampling; amps already run their shipped reduced-order power stage in all
// tiers, so "reduced" is exactly the code path the A7S executes). CPU% is
// processing_time/audio_time; peak block is compared against block/sr.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <chrono>

#include "../Source/EffectRegistry.h"

using namespace openguitarmultifx;

static bool assignParam (EffectProcessor& fx, const juce::String& name, double value)
{
    for (auto* p : fx.getParameters()->getParameters (true))
    {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
            if (f->paramID.equalsIgnoreCase (name) || f->getName (128).equalsIgnoreCase (name))
            { *f = (float) value; return true; }
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
            if (c->paramID.equalsIgnoreCase (name) || c->getName (128).equalsIgnoreCase (name))
            { *c = (int) value; return true; }
        if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
            if (b->paramID.equalsIgnoreCase (name) || b->getName (128).equalsIgnoreCase (name))
            { *b = value != 0.0; return true; }
    }
    return false;
}

// Dumps every registered key with each parameter's id/name/range/default in a
// parseable form: key|paramID|kind|min|max|default|displayName
static void listEffectsAndParams (EffectRegistry& registry)
{
    for (auto& key : registry.getRegisteredNames())
    {
        auto fx = registry.create (key);
        if (fx == nullptr) continue;
        std::cout << "FX " << key << " | " << fx->getName() << "\n";
        for (auto* p : fx->getParameters()->getParameters (true))
        {
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                std::cout << "  P " << f->paramID << "|float|" << f->range.start << "|" << f->range.end
                          << "|" << f->get() << "|" << f->getName (128) << "\n";
            else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
                std::cout << "  P " << c->paramID << "|choice|0|" << (c->choices.size() - 1) << "|" << c->getIndex()
                          << "|" << c->getName (128) << "|choices=" << c->choices.joinIntoString (",") << "\n";
            else if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
                std::cout << "  P " << b->paramID << "|bool|0|1|" << (b->get() ? 1 : 0)
                          << "|" << b->getName (128) << "\n";
        }
    }
}

// Catmull-Rom resample to `outSr`: the bench must run the real A7S rate (48 kHz)
// even though the DI fixtures are 44.1 kHz. Good enough for a CPU metric -- this
// is not the audible-quality path.
static std::vector<float> resampleMono (const float* in, juce::int64 inN, double inSr, double outSr)
{
    if (inSr == outSr)
        return { in, in + inN };
    if (inN < 4) // shorter than the interpolation window -- reject instead of overflowing outN
        return {};
    const double step = inSr / outSr;
    const auto outN = juce::jmax ((juce::int64) 0, (juce::int64) ((inN - 2) / step)); // keep the interpolation window in range
    std::vector<float> out ((size_t) outN);
    for (juce::int64 i = 0; i < outN; ++i)
    {
        const double t = i * step;
        const auto j = (juce::int64) t;
        const double f = t - j;
        const auto at = [&] (juce::int64 k) { return (double) in[juce::jlimit ((juce::int64) 0, inN - 1, k)]; };
        const double p0 = at (j - 1), p1 = at (j), p2 = at (j + 1), p3 = at (j + 2);
        out[(size_t) i] = (float) (p1 + 0.5 * f * (p2 - p0
            + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3
            + f * (3.0 * (p1 - p2) + p3 - p0))));
    }
    return out;
}

static juce::AudioBuffer<float> loadBenchInput (const juce::File& inFile, double targetSr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr)
        return {};
    const auto n = (int) reader->lengthInSamples;
    const auto chs = (int) reader->numChannels;
    juce::AudioBuffer<float> buf (chs, n);
    reader->read (&buf, 0, n, 0, true, true);
    // Downmix to mono -- the A7S chain is mono.
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < chs; ++c)
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += buf.getSample (c, i) / (float) chs;
    auto resampled = resampleMono (mono.data(), n, reader->sampleRate, targetSr);
    juce::AudioBuffer<float> out (1, (int) resampled.size());
    out.copyFrom (0, 0, resampled.data(), (int) resampled.size());
    return out;
}

struct BenchResult
{
    juce::String key, quality;
    double procMs = 0.0, audioMs = 0.0, cpuPct = 0.0, rtf = 0.0;
    double peakBlockMs = 0.0, budgetMs = 0.0;
    int blocksOver = 0;
    juce::String status = "ok";
};

static int runBench (EffectRegistry& registry, const juce::StringArray& args)
{
    // args[0] == "--bench"; args[1] = in.wav; args[2] = out.csv ("-" = stdout)
    if (args.size() < 3)
    {
        std::cerr << "usage: RenderFx --bench <in.wav> <out.csv|-> [EffectKey ...] [param=value ...]\n"
                     "              [--sr=48000] [--block=256] [--repeat=3] [--model=..] [--model2=..]\n"
                     "              [--nam=<file> (Neural* keys)] [--ir=<file> (Cab/DynamicCab/Reverb)]\n";
        return 2;
    }

    const juce::File inFile (args[1]);
    const juce::String outName = args[2]; // "-" = stdout
    const juce::File outFile (outName);
    double sampleRate = 48000.0;
    int block = 256, repeats = 3;
    juce::StringArray keys;
    juce::Array<std::pair<juce::String, double>> assignments;
    juce::String modelPath, model2Path, namPath, irPath;
    for (int i = 3; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a.startsWithIgnoreCase ("--model2="))      model2Path = a.fromLastOccurrenceOf ("=", false, false);
        else if (a.startsWithIgnoreCase ("--model="))  modelPath  = a.fromLastOccurrenceOf ("=", false, false);
        else if (a.startsWithIgnoreCase ("--nam="))    namPath    = a.fromLastOccurrenceOf ("=", false, false);
        else if (a.startsWithIgnoreCase ("--ir="))     irPath     = a.fromLastOccurrenceOf ("=", false, false);
        else if (a.startsWithIgnoreCase ("--sr="))     sampleRate = a.fromLastOccurrenceOf ("=", false, false).getDoubleValue();
        else if (a.startsWithIgnoreCase ("--block="))  block      = juce::jmax (1, a.fromLastOccurrenceOf ("=", false, false).getIntValue());
        else if (a.startsWithIgnoreCase ("--repeat=")) repeats    = juce::jmax (1, a.fromLastOccurrenceOf ("=", false, false).getIntValue());
        else if (a.containsChar ('='))
            assignments.add ({ a.upToFirstOccurrenceOf ("=", false, false),
                               a.fromLastOccurrenceOf ("=", false, false).getDoubleValue() });
        else
            keys.add (a);
    }
    if (keys.isEmpty())
    {
        keys = registry.getRegisteredNames();
        keys.sort (true);
    }

    auto input = loadBenchInput (inFile, sampleRate);
    if (input.getNumSamples() == 0)
    {
        std::cerr << "cannot read " << inFile.getFullPathName() << "\n";
        return 1;
    }
    const int n = input.getNumSamples();
    const double audioSeconds = (double) n / sampleRate;
    const double budgetMs = 1000.0 * block / sampleRate;

    // "reduced" = the tier the app ships on the A7S: OversamplingQuality::eco.
    // Amps run their reduced-order power stage in every tier (set in
    // registerBuiltInEffects), so the reduced column IS the device code path.
    const std::pair<const char*, EffectRegistry::OversamplingQuality> tiers[] = {
        { "high",    EffectRegistry::OversamplingQuality::high },
        { "reduced", EffectRegistry::OversamplingQuality::eco  },
    };

    juce::String csv = "effect,quality,sr,block,channels,reps,audio_ms,proc_ms,cpu_pct,rtf,peak_block_ms,budget_ms,peak_block_pct,blocks_over_budget,status\n";
    juce::AudioBuffer<float> dry = input;
    bool anyFailed = false;

    for (auto& key : keys)
        for (auto& [label, quality] : tiers)
        {
            BenchResult r;
            r.key = key; r.quality = label; r.budgetMs = budgetMs;
            r.audioMs = audioSeconds * 1000.0;
            try
            {
                EffectRegistry::setOversamplingQuality (quality);
                auto fx = registry.create (key);
                if (fx == nullptr) { r.status = "create_failed"; }
                else
                {
                    for (auto& [name, value] : assignments)
                        if (! assignParam (*fx, name, value))
                            std::cerr << "warning: " << key << " has no parameter '" << name << "'\n";
                    if (modelPath.isNotEmpty() && fx->wantsModelFile())
                        fx->loadModelFile (juce::File (modelPath), 0);
                    if (model2Path.isNotEmpty() && fx->wantsModelFile())
                        fx->loadModelFile (juce::File (model2Path), 1);
                    if (namPath.isNotEmpty() && key.startsWith ("Neural") && fx->wantsModelFile())
                        fx->loadModelFile (juce::File (namPath), 0);
                    if (irPath.isNotEmpty() && (key == "Cab" || key == "DynamicCab" || key == "Reverb")
                        && fx->wantsModelFile())
                        fx->loadModelFile (juce::File (irPath), 0);
                    fx->prepare (sampleRate, block, 1);

                    double best = 1e30;
                    for (int rep = 0; rep < repeats; ++rep)
                    {
                        input.copyFrom (0, 0, dry, 0, 0, n); // restore the dry take
                        double repProc = 0.0, repPeak = 0.0;
                        int repOver = 0;
                        for (int pos = 0; pos < n; pos += block)
                        {
                            const int len = juce::jmin (block, n - pos);
                            juce::AudioBuffer<float> slice (input.getArrayOfWritePointers(), 1, pos, len);
                            const auto t0 = std::chrono::steady_clock::now();
                            fx->process (slice);
                            const auto t1 = std::chrono::steady_clock::now();
                            const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
                            repProc += ms;
                            repPeak = juce::jmax (repPeak, ms);
                            if (ms > budgetMs) ++repOver;
                        }
                        if (repProc < best) { best = repProc; r.peakBlockMs = repPeak; r.blocksOver = repOver; }
                    }
                    r.procMs = best;
                    r.rtf = (audioSeconds * 1000.0) / best;
                    r.cpuPct = 100.0 * best / (audioSeconds * 1000.0);
                }
            }
            catch (...)
            {
                r.status = "exception";
            }
            anyFailed = anyFailed || r.status != "ok";
            csv << r.key << ',' << r.quality << ',' << (int) sampleRate << ',' << block << ",1,"
                << repeats << ',' << juce::String (r.audioMs, 1) << ',' << juce::String (r.procMs, 2)
                << ',' << juce::String (r.cpuPct, 3) << ',' << juce::String (r.rtf, 1)
                << ',' << juce::String (r.peakBlockMs, 3) << ',' << juce::String (r.budgetMs, 3)
                << ',' << juce::String (100.0 * r.peakBlockMs / r.budgetMs, 1)
                << ',' << r.blocksOver << ',' << r.status << '\n';
            std::cerr << r.key << " [" << r.quality << "] cpu " << juce::String (r.cpuPct, 2)
                      << "% peak-block " << juce::String (r.peakBlockMs, 2) << " ms"
                      << (r.status != "ok" ? "  (" + r.status + ")" : "") << "\n";
        }

    if (outName == "-")
        std::cout << csv;
    else if (! outFile.replaceWithText (csv))
    {
        std::cerr << "cannot write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    // Nonzero if any row failed -- the CSV still got written, but a green exit
    // must mean every measured row is real.
    return anyFailed ? 1 : 0;
}

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    EffectRegistry registry;
    registerBuiltInEffects (registry);

    if (args.size() == 1 && args[0] == "--list")
    {
        listEffectsAndParams (registry);
        return 0;
    }

    if (args.size() >= 1 && args[0] == "--bench")
        return runBench (registry, args);

    if (args.size() < 3)
    {
        std::cerr << "usage: RenderFx <in.wav> <out.wav> <EffectKey> [param=value ...] [--quality=eco|balanced|high|reduced] [--repeat=N]\n"
                     "       RenderFx --bench <in.wav> <out.csv|-> [EffectKey ...]   (CPU benchmark, see file header)\n"
                     "       RenderFx --list   (dump every key + parameters)\n";
        return 2;
    }

    const juce::File inFile (args[0]), outFile (args[1]);
    // '+' chains several registered effects (e.g. "JCM800StyleAmplifier+Cab").
    juce::StringArray keys;
    keys.addTokens (args[2], "+", "");

    auto quality = EffectRegistry::OversamplingQuality::high;
    int repeats = 1;
    juce::Array<std::pair<juce::String, double>> assignments;
    juce::String modelPath, model2Path;
    for (int i = 3; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a.startsWithIgnoreCase ("--model2="))
        {
            model2Path = a.fromLastOccurrenceOf ("=", false, false);
        }
        else if (a.startsWithIgnoreCase ("--model="))
        {
            modelPath = a.fromLastOccurrenceOf ("=", false, false);
        }
        else if (a.startsWithIgnoreCase ("--quality="))
        {
            const auto v = a.fromLastOccurrenceOf ("=", false, false).toLowerCase();
            quality = v.startsWith ("eco") || v.startsWith ("red") ? EffectRegistry::OversamplingQuality::eco // "reduced" = the A7S tier
                    : v.startsWith ("bal") ? EffectRegistry::OversamplingQuality::balanced
                    : EffectRegistry::OversamplingQuality::high;
        }
        else if (a.startsWithIgnoreCase ("--repeat="))
        {
            repeats = juce::jmax (1, a.fromLastOccurrenceOf ("=", false, false).getIntValue());
        }
        else if (a.containsChar ('='))
        {
            assignments.add ({ a.upToFirstOccurrenceOf ("=", false, false),
                               a.fromLastOccurrenceOf ("=", false, false).getDoubleValue() });
        }
    }

    EffectRegistry::setOversamplingQuality (quality);

    std::vector<std::unique_ptr<EffectProcessor>> chain;
    for (auto& k : keys)
    {
        auto fx = registry.create (k);
        if (fx == nullptr)
        {
            std::cerr << "unknown effect key '" << k << "'; registered keys:\n";
            for (auto& kk : registry.getRegisteredNames()) std::cerr << "  " << kk << "\n";
            return 2;
        }
        chain.push_back (std::move (fx));
    }

    for (auto& [name, value] : assignments)
    {
        bool matched = false;
        for (auto& fx : chain)
            matched = assignParam (*fx, name, value) || matched;
        if (! matched)
            std::cerr << "warning: no parameter matched '" << name << "'\n";
    }

    if (modelPath.isNotEmpty())
        for (auto& fx : chain)
            if (fx->wantsModelFile())
                fx->loadModelFile (juce::File (modelPath), 0);
    if (model2Path.isNotEmpty())
        for (auto& fx : chain)
            if (fx->wantsModelFile())
                fx->loadModelFile (juce::File (model2Path), 1);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr) { std::cerr << "cannot read " << inFile.getFullPathName() << "\n"; return 1; }

    const auto n = (int) reader->lengthInSamples;
    const auto chs = (int) reader->numChannels;
    juce::AudioBuffer<float> buf (chs, n);
    reader->read (&buf, 0, n, 0, true, true);

    const int block = 512;
    for (auto& fx : chain)
        fx->prepare (reader->sampleRate, block, chs);

    double bestSeconds = 1e30;
    for (int rep = 0; rep < repeats; ++rep)
    {
        if (rep > 0)
            reader->read (&buf, 0, n, 0, true, true); // reload the dry take
        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = juce::jmin (block, n - pos);
            juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), chs, pos, len);
            for (auto& fx : chain)
                fx->process (slice);
        }
        const auto t1 = std::chrono::steady_clock::now();
        bestSeconds = juce::jmin (bestSeconds, std::chrono::duration<double> (t1 - t0).count());
    }
    const double audioSeconds = (double) n / reader->sampleRate;
    const double rtf = audioSeconds / bestSeconds;
    const double cpuPct = 100.0 / rtf;

    outFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (outFile.createOutputStream());
    if (os == nullptr) { std::cerr << "cannot write " << outFile.getFullPathName() << "\n"; return 1; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (
        wav.createWriterFor (os.get(), reader->sampleRate, (unsigned) chs, 24, {}, 0));
    if (w == nullptr) { std::cerr << "cannot create wav writer\n"; return 1; }
    os.release();
    w->writeFromAudioSampleBuffer (buf, 0, n);

    float peak = buf.getMagnitude (0, 0, n);
    std::cout << args[2] << " | " << inFile.getFileName() << " -> " << outFile.getFileName()
              << " | " << n << " samples @ " << reader->sampleRate << " Hz, " << chs << " ch"
              << " | peak " << juce::String (peak, 3)
              << " | proc " << juce::String (bestSeconds * 1000.0, 1) << " ms"
              << " | rtf " << juce::String (rtf, 1) << "x"
              << " | cpu " << juce::String (cpuPct, 2) << "%\n";
    return 0;
}
