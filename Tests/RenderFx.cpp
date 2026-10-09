// Offline renderer: runs a WAV file through one registered effect and writes the
// result. Meant for listening sessions -- the same processor code that runs live,
// driven from a file instead of the sound card.
//
//   OpenGuitarMultiFx_RenderFx <in.wav> <out.wav> <EffectKey> [param=value ...] [--quality=eco|balanced|high]
//
// Each param=value is matched (case-insensitive) against paramID then display name.
// Values are in the parameter's own units: knob range for AudioParameterFloat,
// choice index for AudioParameterChoice, 0/1 for AudioParameterBool.
//
//   OpenGuitarMultiFx_RenderFx bass-di.wav out.wav SVTStyleAmplifier svt_gain=0.6 svt_speaker=1

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

    if (args.size() < 3)
    {
        std::cerr << "usage: RenderFx <in.wav> <out.wav> <EffectKey> [param=value ...] [--quality=eco|balanced|high] [--repeat=N]\n"
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
            quality = v.startsWith ("eco") ? EffectRegistry::OversamplingQuality::eco
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
