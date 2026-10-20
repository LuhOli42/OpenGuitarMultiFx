#pragma once

#include "Effects/AnalogFilterDesign.h"
#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "PitchParts.h"

#include <array>

namespace openguitarmultifx
{

enum class DividerOctaverModel
{
    octaveMultiplexer, // EHX Deluxe Octave Multiplexer -- 4013 driving 2N4302 choppers on the filtered signal, Filter, Blend
    fx35,              // DOD FX35 Octoplus -- 3rd-order analyser LPF, 4013 + J113 chopper, Direct / Octave Level, Tone
    oct1               // Korg OCT-1 -- 4013s, the divided square through an NJM13600 VCA following the note: synth-like
};

/**
    Analog octave-down pedals (docs/circuits/DividerOctavers.md): the shared analyser (low-pass, level-tracking
    hysteresis comparator) clocks toggle flip-flops at the played fundamental, and the divided square either flips the
    signal's polarity every other cycle (a chopper -- the note's own waveform an octave down, the OC-2 / Multiplexer /
    Octoplus way) or is the sound itself, shaped by an envelope-controlled VCA (the OCT-1). Monophonic like the
    originals: chords and noisy decays mis-track the same way.
*/
class DividerOctaverProcessor : public EffectProcessor
{
public:
    explicit DividerOctaverProcessor (DividerOctaverModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8a6fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Channel
    {
        pitchparts::Analyser analyser;
        pitchparts::Dividers ff;
        analog::Filter inputHp, subLp1, subLp2, bassLp;
        double env = 0.0;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    DividerOctaverModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *direct = nullptr, *octave = nullptr, *toneOrFilter = nullptr, *toggle = nullptr;
    juce::SmoothedValue<float> smoothedDirect, smoothedOctave, smoothedTone;
    double sampleRate = 0.0, envA = 0.0;
};

} // namespace openguitarmultifx
