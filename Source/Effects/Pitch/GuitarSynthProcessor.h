#pragma once

#include "Effects/AnalogFilterDesign.h"
#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "PitchParts.h"

#include <array>

namespace openguitarmultifx
{

enum class GuitarSynthModel
{
    microSynth,     // EHX Micro Synthesizer -- guitar, octave up, sub octave, square; VCF swept Start -> Stop per note; Attack Delay
    bassMicroSynth, // EHX Bass Micro Synthesizer -- the same architecture voiced for bass
    guitarSynth,    // EHX Guitar Synthesizer -- the voices plus a second sub and an attack/decay filter envelope (behavioural)
    brassmaster     // Maestro Brassmaster BB-1 -- fuzz into a phase splitter, transformer and diode bridge (octave up), Brass filters
};

/**
    Analog guitar synthesizers (docs/circuits/GuitarSynths.md). No oscillator follows the pitch: every voice is made
    from the string itself -- the full-wave rectifier's octave up, a flip-flop chopper's octave down, a comparator's
    square shaped by the note's envelope -- mixed and sent through a resonant voltage-controlled low-pass whose cutoff
    is swept from Start to Stop each time the attack detector sees a new note.
*/
class GuitarSynthProcessor : public EffectProcessor
{
public:
    explicit GuitarSynthProcessor (GuitarSynthModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd86fa8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Channel
    {
        pitchparts::Analyser analyser;
        pitchparts::Dividers ff;
        pitchparts::Vcf vcf;
        analog::Filter inputHp, octDc, brassA, brassB;
        double env = 0.0, lastLevel = 0.0, sweep = 1.0, vca = 1.0, holdOff = 0.0;
        bool gate = false;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    GuitarSynthModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *guitar = nullptr, *octUp = nullptr, *sub = nullptr, *square = nullptr, *resonance = nullptr,
                              *start = nullptr, *stop = nullptr, *rate = nullptr, *attack = nullptr, *trigger = nullptr,
                              *sub2 = nullptr, *decay = nullptr, *sensitivity = nullptr, *harmonic = nullptr, *brass = nullptr;
    double sampleRate = 0.0, envA = 0.0;
};

} // namespace openguitarmultifx
