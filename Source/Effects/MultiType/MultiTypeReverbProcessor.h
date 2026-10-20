#pragma once

#include "MultiTypeCommon.h"

namespace openguitarmultifx
{

/** One reverb engine with a typical Reverb block structure and parameter list:
      input gain -> pre-delay (feedback, mix) -> input diffuser -> early reflections (size, diffusion, decay, mic
      spacing) and a late tail: 8-line feedback delay network with 3-band decay (LF / mid / HF times, crossover),
      wall diffusion, echo density, modulation, late input mix, integrated 2-voice pitch shifter, stack/hold
      -> EQ (low/high cut, 2 peaking) -> ducker -> spread, mix.
    Spring types switch to a dispersive spring-tank algorithm (number of springs, tone, drive, "boing"). */
class MultiTypeReverbProcessor : public EffectProcessor
{
public:
    MultiTypeReverbProcessor();
    explicit MultiTypeReverbProcessor (int fixedTypeIndex);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool hasTrails() const override { return true; }
    double trailLatencySeconds() const override { return 0.3; }
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return fixedName.empty() ? "Multi-Type Reverb" : fixedName.c_str(); }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff5d6fd6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    struct TypeSpec
    {
        const char* name;
        bool spring;
        float time, size, predelay, early, late, hfMult, lfMult, crossover, density, earlyDiff, inputDiff, wallDiff;
        float modDepth, modRate, lowCut, highCut, pitchMix, shift1, shift2, pitchFb, springs;
    };
    static const std::vector<TypeSpec>& types();

private:
    static constexpr int kLines = 8, kTaps = 12, kSprings = 6, kDispersion = 18;
    void applyType (int index);
    void updateTank();
    float pitchShift (int voice, float x, double ratio);

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *type, *time, *size, *predelay, *mix, *level;
    juce::AudioParameterFloat *earlyLevel, *lateLevel, *hfTime, *lfTime, *crossover, *echoDensity, *earlyDiffusion, *earlyDiffTime,
        *earlyDecay, *lateInputMix, *wallDiffusion, *inputDiffusion, *inputDiffTime, *micSpacing, *stackHold, *predelayFeedback,
        *predelayMix, *spread, *inputGain;
    juce::AudioParameterFloat *modDepth, *modRate, *lfoPhase, *pitchMix, *shift1, *shift2, *voiceBalance, *spliceTime, *pitchFeedback,
        *duckAtten, *duckThreshold, *duckRelease, *numSprings, *springTone, *springDrive, *boing;
    juce::AudioParameterFloat *lowCut, *highCut, *lowMidFreq, *lowMidGain, *highMidFreq, *highMidGain;
    mt::TypeWatcher watcher;
    std::string fixedName;

    double fs = 48000.0;
    mt::DelayLine preLine, lines[kLines], erLine;
    mt::Diffuser inDiff[4], erDiff[2][2], wall[kLines];
    mt::OnePole lowSplit[kLines], highSplit[kLines];
    double gLow[kLines] {}, gMid[kLines] {}, gHigh[kLines] {}, lineLen[kLines] {};
    double tapTime[2][kTaps] {}, tapGain[2][kTaps] {};
    mt::Lfo lfo[kLines];
    mt::Biquad eqLow[2], eqHigh[2], eqP1[2], eqP2[2];
    double duckEnv = 0, lateOut = 0;
    // springs
    mt::DelayLine springLine[kSprings];
    double springState[kSprings][kDispersion] {}, springIn[kSprings][kDispersion] {};
    mt::OnePole springTone_[kSprings];
    double springFb[kSprings] {};
    // pitch shifter
    mt::DelayLine pitchLine[2];
    double pitchPhase[2] { 0, 0 };
    mt::MixLevel mixLevel;
};

} // namespace openguitarmultifx
