#pragma once

#include "MultiTypeCommon.h"

namespace openguitarmultifx
{

/** One delay engine with a typical Delay block structure; the types are presets.
      input gain -> [algorithm: mono / stereo / ping-pong / dual (cross feedback) / reverse / 2-head tape]
      feedback path: EQ (low/high cut, 2 peaking) -> drive -> bit reduction -> diffuser -> compander
      delay-time modulation from LFO 1 and LFO 2 (depth range LOW/HIGH), ducker, sweep filter (Sweep type),
      echo pan / spread, phase reverse, mix (the Delay block's own mix law: dry stays full up to 50 %). */
class MultiTypeDelayProcessor : public EffectProcessor
{
public:
    MultiTypeDelayProcessor();
    explicit MultiTypeDelayProcessor (int fixedTypeIndex);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool hasTrails() const override { return true; }
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Delay" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3f8fd1); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    enum class Algo { mono, stereo, pingPong, dual, reverse, tape };
    struct TypeSpec
    {
        const char* name;
        Algo algo;
        float time, ratio, feedback, feedbackR, cross, mix;
        float lowCut, highCut, drive, bits;
        float lfo1Rate, lfo1Depth, lfo2Rate, lfo2Depth;
        float diffusion, diffTime, compander;
        float duckAtten, sweep;  // sweep: 1 = LFO band-pass on the output
        float motor, head2;      // tape
    };
    static const std::vector<TypeSpec>& types();

private:
    void applyType (int index);
    float readTape (double delaySamples);

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *time, *feedback, *mix, *level, *killDry = nullptr;
    juce::AudioParameterFloat *ratio, *spread, *feedbackR, *crossFeedback, *masterFeedback, *drive, *bitReduction, *phaseReverse,
        *inputGain, *motorSpeed, *head2Ratio, *head2Level, *echoPan;
    juce::AudioParameterFloat *lowCut, *highCut, *eq1Freq, *eq1Gain, *eq2Freq, *eq2Gain;
    juce::AudioParameterFloat *lfo1Type, *lfo1Rate, *lfo1Depth, *lfo2Type, *lfo2Rate, *lfo2Depth, *lfoPhase, *depthRange,
        *diffusion, *diffusionTime, *compander, *duckAtten, *duckThreshold, *duckRelease, *sweepStart, *sweepStop, *sweepRate, *sweepQ;
    mt::TypeWatcher watcher;
    std::string fixedName;

    double fs = 48000.0;
    mt::DelayLine line[2];
    mt::OnePole fbLow[2], fbHigh[2];
    mt::Biquad fbEq1[2], fbEq2[2], sweepFilter[2];
    mt::Diffuser diff[2][2];
    mt::Lfo lfo1, lfo2, sweepLfo;
    double duckEnv = 0, compEnv[2] { 1e-3, 1e-3 }, fbState[2] { 0, 0 };
    // reverse
    std::vector<float> revBuf[2];
    int revWrite = 0;
    double revPhase = 0;
    juce::SmoothedValue<float> smoothTime;
    mt::MixLevel mixLevel;
};

} // namespace openguitarmultifx
