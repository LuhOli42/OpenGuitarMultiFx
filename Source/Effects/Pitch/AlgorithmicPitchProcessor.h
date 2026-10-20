#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"
#include "PitchParts.h"

#include <array>

namespace openguitarmultifx
{

enum class AlgorithmicPitch
{
    pog,      // polyphonic octaves: -2, -1, +1, +2 octaves and dry, each with its own level (EHX POG-style)
    whammy,   // a pitch bend between unison and an interval, by a Position knob (map it to an expression pedal)
    organ,    // guitar into organ: sub, octave and 19th "drawbars", key click, rotating-speaker shimmer (EHX B9-style)
    monoSynth // a tracking monophonic synth: the played pitch drives an oscillator through a resonant filter
};

/**
    Pitch effects that are algorithms (docs/circuits/AlgorithmicPitch.md). The shifting voices are time-domain: two
    taps sweeping a short window of a delay line at (1 - ratio) times real time, sin^2-crossfaded -- polyphonic (chords
    survive, with the usual granular smear), latency ~one window. The Mono Synth tracks the pitch the way analog guitar
    synths did: low-pass, level-tracking comparator, period measurement.
*/
class AlgorithmicPitchProcessor : public EffectProcessor
{
public:
    explicit AlgorithmicPitchProcessor (AlgorithmicPitch kind);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xffa06fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The Mono Synth's tracked frequency (Hz, 0 = none) -- for tests. */
    double debugTrackedHz() const noexcept { return trackedHz; }

private:
    struct Shifter
    {
        double phase = 0.0;
        double process (const EnsembleDelay& line, double ratio, double windowSamples) noexcept;
    };

    struct Channel
    {
        EnsembleDelay line;
        std::array<Shifter, 4> shifters;
        pitchparts::Analyser analyser;
        pitchparts::Vcf vcf;
        WetLowpass tone;
        double env = 0.0, oscPhase = 0.0, freq = 0.0, click = 0.0, lastLevel = 0.0, modPhase = 0.0;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    AlgorithmicPitch kind;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::array<juce::AudioParameterFloat*, 8> knobs {};
    juce::SmoothedValue<float> smoothedPosition;
    double sampleRate = 0.0, trackedHz = 0.0;
};

} // namespace openguitarmultifx
