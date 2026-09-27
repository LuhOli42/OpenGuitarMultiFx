#pragma once

#include "EffectProcessor.h"
#include "WetLevelMatcher.h"

#include <array>

#include <juce_dsp/juce_dsp.h>

namespace openguitarmultifx
{

/**
    Algorithmic reverb wrapping juce::dsp::Reverb (Freeverb-derived, built
    into JUCE). Distinct from IRLoaderProcessor's "Reverb" chain role: that
    one convolves with a real captured space loaded from a TONE3000 IR
    file; this one is a synthesized, continuously-controllable space with
    no file to load at all -- the icon set's "Ambient" glyph, not "Hall".
    getName() must stay "Ambient" (not "Reverb") so the two don't collide
    in EffectRegistry's display-name lookup, which presets rely on.
*/
class ReverbProcessor : public EffectProcessor
{
public:
    ReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Ambient"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff1f96a0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    void updateReverbParameters();

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* size = nullptr;
    juce::AudioParameterFloat* damping = nullptr;
    juce::AudioParameterFloat* mix = nullptr;
    juce::AudioParameterFloat* width = nullptr;

    juce::dsp::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;
    std::array<WetLevelMatcher, 2> wetMatch; // keeps Mix = 1 as loud as the dry signal (docs/circuits/MixLaw.md)
};

} // namespace openguitarmultifx
