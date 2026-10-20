#pragma once

#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"

#include <array>

namespace openguitarmultifx
{

enum class AlgorithmicVibrato
{
    tapeWarble, // a worn transport: slow random wow plus flutter, darkening with Age
    stereo,     // left and right swept in opposition -- motion that collapses cleanly to mono
    random      // a smoothed random pitch drift instead of a periodic LFO
};

/** Vibratos that are algorithms (docs/circuits/AlgorithmicVibratos.md): a Hermite-interpolated delay read at a moving
    position, wet only. */
class AlgorithmicVibratoProcessor : public EffectProcessor
{
public:
    explicit AlgorithmicVibratoProcessor (AlgorithmicVibrato kind);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff5fb09f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Channel
    {
        EnsembleDelay line;
        WetLowpass tone;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    AlgorithmicVibrato kind;
    std::array<Channel, 2> channels;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *extra = nullptr, *shape = nullptr;
    juce::Random random { 0x71b2 };
    double phase = 0.0, flutterPhase = 0.0, wowTarget = 0.0, wow = 0.0, randTarget = 0.0, rand = 0.0, sampleRate = 0.0, lastExtra = -1.0;
};

} // namespace openguitarmultifx
