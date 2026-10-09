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

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.size() < 3)
    {
        std::cerr << "usage: RenderFx <in.wav> <out.wav> <EffectKey> [param=value ...] [--quality=eco|balanced|high]\n";
        return 2;
    }

    const juce::File inFile (args[0]), outFile (args[1]);
    const juce::String key = args[2];

    EffectRegistry registry;
    registerBuiltInEffects (registry);

    auto quality = EffectRegistry::OversamplingQuality::high;
    juce::Array<std::pair<juce::String, double>> assignments;
    for (int i = 3; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a.startsWithIgnoreCase ("--quality="))
        {
            const auto v = a.fromLastOccurrenceOf ("=", false, false).toLowerCase();
            quality = v.startsWith ("eco") ? EffectRegistry::OversamplingQuality::eco
                    : v.startsWith ("bal") ? EffectRegistry::OversamplingQuality::balanced
                    : EffectRegistry::OversamplingQuality::high;
        }
        else if (a.containsChar ('='))
        {
            assignments.add ({ a.upToFirstOccurrenceOf ("=", false, false),
                               a.fromLastOccurrenceOf ("=", false, false).getDoubleValue() });
        }
    }

    EffectRegistry::setOversamplingQuality (quality);

    auto fx = registry.create (key);
    if (fx == nullptr)
    {
        std::cerr << "unknown effect key '" << key << "'; registered keys:\n";
        for (auto& k : registry.getRegisteredNames()) std::cerr << "  " << k << "\n";
        return 2;
    }

    for (auto& [name, value] : assignments)
        if (! assignParam (*fx, name, value))
            std::cerr << "warning: no parameter matched '" << name << "'\n";

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr) { std::cerr << "cannot read " << inFile.getFullPathName() << "\n"; return 1; }

    const auto n = (int) reader->lengthInSamples;
    const auto chs = (int) reader->numChannels;
    juce::AudioBuffer<float> buf (chs, n);
    reader->read (&buf, 0, n, 0, true, true);

    const int block = 512;
    fx->prepare (reader->sampleRate, block, chs);
    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), chs, pos, len);
        fx->process (slice);
    }

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
    std::cout << fx->getName() << " | " << inFile.getFileName() << " -> " << outFile.getFileName()
              << " | " << n << " samples @ " << reader->sampleRate << " Hz, " << chs << " ch"
              << " | peak " << juce::String (peak, 3) << "\n";
    return 0;
}
