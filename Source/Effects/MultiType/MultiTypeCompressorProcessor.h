#pragma once

#include "MultiTypeCommon.h"

namespace openguitarmultifx
{

/** One dynamics engine with a typical Compressor block structure: feed-forward or feedback detector
    (RMS / peak / both) on a filtered sidechain (low/high cut, emphasis), downward or upward ("sustainer") gain
    computer with knee, compander and dynamics-processor modes, attack/release (auto), optical two-stage release,
    JFET and tube colouration, look-ahead, auto makeup, mix, level. Types are presets. */
class MultiTypeCompressorProcessor : public EffectProcessor
{
public:
    MultiTypeCompressorProcessor();
    explicit MultiTypeCompressorProcessor (int fixedTypeIndex);
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Compressor" : fixedName.c_str(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6d8f3a); }

    enum class Mode { downward, upward, compander, dynamics };
    enum class Colour { clean, optical, jfet, tube };
    struct TypeSpec { const char* name; Mode mode; Colour colour; bool feedback; float threshold, ratio, attack, release, knee; int detector; bool autoAR; };
    static const std::vector<TypeSpec>& types();

private:
    void applyType (int index);
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *threshold, *ratio, *attack, *release, *mix, *level;
    juce::AudioParameterFloat *knee, *detector, *autoAR, *lookAhead, *autoMakeup, *lightType, *dynamicsAmt, *scLowCut, *scHighCut,
        *emphasis, *emphasisFreq, *inputLevel;
    mt::TypeWatcher watcher;
    std::string fixedName;
    double fs = 48000.0;
    mt::OnePole scLc, scHc;
    mt::Biquad scEmph;
    mt::DelayLine look[2];
    double env = 0, rms = 0, gainDb = 0, slowDb = 0, lastOut = 0;
    mt::MixLevel mixLevel;
};

} // namespace openguitarmultifx
