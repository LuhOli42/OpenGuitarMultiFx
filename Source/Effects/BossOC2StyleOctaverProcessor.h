#pragma once

#include "Biquad.h"
#include "DualMono.h"
#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss OC-2 style ANALOG octave-down, modelled block-for-block on the real pedal's divide-down
    architecture (see docs/circuits/BossOC2StyleOctaver.md) -- NOT a digital pitch shifter.

    Per channel: input buffer/HPF -> the analyser path (LP filter + dual peak detectors driving a
    hysteresis comparator, i.e. the input-amplitude tracking that makes the square at the fundamental)
    -> toggle flip-flops dividing by 2 and 4 (the 4013 + BA634 chain) -> each octave creator lifts the
    input about a diode drop above the virtual ground and multiplies it by +/-0.5 at the divided-square
    rate (the 2SK30 JFET chopper) -> low-pass filtering -> the Direct/Oct1/Oct2 mixer.

    Like the real unit it is monophonic: chords and noisy decays mis-track the same way.
*/
class BossOC2StyleOctaverProcessor : public EffectProcessor
{
public:
    BossOC2StyleOctaverProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Boss OC-2-Style Octaver"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5fb4); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct Channel
    {
        Biquad lpAnalyser;
        Biquad lpOct1[2], lpOct2[2];
        double hpZ = 0.0, hpPrev = 0.0;   // 1st-order input HPF state
        double envP = 0.0, envN = 0.0;    // the analyser's dual peak detectors
        bool comp = false, prevComp = false;
        bool ff1 = false, ff2 = false;
    };

    void updateLevels();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* directParam = nullptr;
    juce::AudioParameterFloat* oct1Param = nullptr;
    juce::AudioParameterFloat* oct2Param = nullptr;

    double sampleRate = 0.0;
    double hpCoeff = 0.0, envDecay = 0.0;
    double directGain = 0.0, oct1Gain = 0.0, oct2Gain = 0.0;
    int controlCounter = 0;
    DualMonoShortcut shortcut;

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
