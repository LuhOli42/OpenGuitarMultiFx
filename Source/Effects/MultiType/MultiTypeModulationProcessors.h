#pragma once

#include "MultiTypeCommon.h"

namespace openguitarmultifx
{

/** Typical Chorus block structure: 1-8 modulated voices per side, LFO1 (+ LFO2 modulating it), minimum delay,
    width (right side scaled toward 1 ms), BBD drive, wet low/high cut, auto depth, dimension mode, phase reverse,
    spread, mix. Types are presets. */
class MultiTypeChorusProcessor : public EffectProcessor
{
public:
    MultiTypeChorusProcessor();
    explicit MultiTypeChorusProcessor (int fixedTypeIndex);
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Chorus" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2fae9b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct TypeSpec { const char* name; int voices; float rate, depth, delayMs, mix, highCut, drive, width, phase, lfo2Rate, lfo2Depth; int lfoType, dimension; bool mono; };
    static const std::vector<TypeSpec>& types();

private:
    void applyType (int index);
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *voices, *rate, *depth, *delayTime, *mix, *level;
    juce::AudioParameterFloat *lowCut, *highCut, *lfoPhase, *lfoType, *autoDepth, *phaseReverse, *drive, *width, *lfo2Rate, *lfo2Depth,
        *spread, *dimension;
    mt::TypeWatcher watcher;
    std::string fixedName;
    double fs = 48000.0;
    mt::DelayLine line[2];
    mt::Lfo lfo, lfo2;
    mt::OnePole lc[2], hc[2];
    mt::MixLevel mixLevel;
};

/** Typical Flanger block structure: modulated short delay with feedback (+/-), manual, min/max time, VCO
    response (linear / exponential with exponent), thru-zero (off / auto / manual) with dry delay, BBD drive, bass
    focus, wet low/high cut, LFO type/phase/hicut/quantize, auto depth, phase reverse, spread, mix. */
class MultiTypeFlangerProcessor : public EffectProcessor
{
public:
    MultiTypeFlangerProcessor();
    explicit MultiTypeFlangerProcessor (int fixedTypeIndex);
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Flanger" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8c5bd6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct TypeSpec { const char* name; float rate, depth, feedback, manual, minMs, maxMs, mix, drive, highCut, phase; int thruZero, lfoType, vco; float exponent, quantize; };
    static const std::vector<TypeSpec>& types();

private:
    void applyType (int index);
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *rate, *depth, *feedback, *manual, *mix, *level;
    juce::AudioParameterFloat *minTime, *maxTime, *thruZero, *dryDelay, *vcoResponse, *exponent, *lfoType, *lfoPhase, *lfoHicut,
        *lfoQuantize, *autoDepth, *drive, *bassFocus, *lowCut, *highCut, *phaseReverse, *spread;
    mt::TypeWatcher watcher;
    std::string fixedName;
    double fs = 48000.0;
    mt::DelayLine line[2], dryLine[2];
    mt::Lfo lfo;
    mt::OnePole lfoSmooth[2], lc[2], hc[2], focus[2];
    double fb[2] { 0, 0 };
    mt::MixLevel mixLevel;
};

/** Typical Phaser block structure: 2-12 all-pass stages per side, feedback (+/-) to a selectable point, min/max
    frequency, manual, LFO type/phase/quantize/hicut, VCR type (linear, exponential, JFET, photocell with
    attack/release), VCR bias, tone, "Vibe" types with unequal stage spacing, spread, mix. */
class MultiTypePhaserProcessor : public EffectProcessor
{
public:
    MultiTypePhaserProcessor();
    explicit MultiTypePhaserProcessor (int fixedTypeIndex);
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Phaser" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc75b9b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct TypeSpec { const char* name; int order; float rate, depth, feedback, manual, minHz, maxHz, mix, phase, tone; int vcr, fbPoint; bool vibe; float bias; };
    static const std::vector<TypeSpec>& types();

private:
    static constexpr int kMaxStages = 12;
    void applyType (int index);
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *rate, *depth, *feedback, *manual, *tone, *mix, *level;
    juce::AudioParameterFloat *order, *feedbackPoint, *minFreq, *maxFreq, *lfoType, *lfoPhase, *lfoQuantize, *lfoHicut, *vcrType, *vcrBias,
        *exponent, *photoAttack, *photoRelease, *spread;
    mt::TypeWatcher watcher;
    std::string fixedName;
    double fs = 48000.0;
    mt::Lfo lfo;
    double ap[2][kMaxStages] {}, apIn[2][kMaxStages] {}, fbSample[2] { 0, 0 }, photo[2] { 0, 0 };
    mt::OnePole lfoSmooth[2], toneLp[2];
    mt::MixLevel mixLevel;
};

} // namespace openguitarmultifx
