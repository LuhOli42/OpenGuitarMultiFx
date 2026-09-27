#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A Boss NS-2-style noise suppressor. What is public about the NS-2 (its owner's manual and Boss's own articles): "a high-quality VCA
    and high-speed envelope-detecting circuits", an EXPANDER that "starts its function when the volume of the instrument becomes lower
    than the threshold level" (so the noise is turned down as the note dies away, not chopped off), a THRESHOLD knob, a DECAY knob (how
    slowly the suppression closes after the playing stops), and a REDUCTION / MUTE selector that decides what the foot switch does
    (Normal <-> Reduction, or Reduction <-> Mute) -- it is not a depth control. No ratio, depth or timing is published, so those are
    estimates, listed in docs/circuits/NS2StyleNoiseSuppressor.md: a downward expander with ratio 1:3 (10 dB under the threshold is
    turned down by 20 dB more), at most -60 dB, opening in about 1 ms and closing in 40 ms .. 2 s.

    Controls: Threshold, Decay, Mute (the switch's Mute position engaged).
*/
class NS2StyleNoiseSuppressorProcessor : public EffectProcessor
{
public:
    NS2StyleNoiseSuppressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "NS-2-Style Noise Suppressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4b7f52); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugGainDb() const noexcept { return gainDb; }

    static constexpr double expanderRatio = 3.0;
    static constexpr double maxReductionDb = 60.0;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* threshold = nullptr;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* mute = nullptr;

    double sampleRate = 0.0;
    double envelope = 0.0; // linear peak, instant attack
    double gainDb = 0.0;
    double muteGain = 1.0;
};

} // namespace openguitarmultifx
