#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss Metal Zone (MT-2)-style distortion, modelled at component level on NodalCircuit from Roland/Boss's own service
    manual circuit diagram (MT-2, April 1991; docs/circuits/MetalZoneStyleDistortion.md -- read that file, not this comment).

    Five blocks per channel, because every op-amp stage that can clip needs its own (a block holds one saturating op-amp):
      a. a JFET source follower, the electronic-bypass switch, and IC3b: non-inverting, gain 1 -> ~100, its (-) leg a
         bootstrapped-emitter-follower "active inductor" (Q010) that raises the gain only above ~1 kHz;
      b. IC3a: the Dist stage, a non-inverting amplifier whose feedback is the 250K Dist pot (gain 2 -> 252);
      c. the clipper: 2K2 into two 1SS133 back to back to ground, filtered, then IC4b, whose (-) leg has two more
         emitter-follower "inductor" branches (Q008 and Q007) that shape its gain, and IC4a, a unity-gain inverter (linear:
         IC4b's output is rail-limited already, so IC4a cannot clip in turn);
      d. the tone stack's first half: IC1a with the High and Low pots and the bootstrapped IC1b network on Low;
      e. IC2a with the Middle pot and the swept-frequency Mid network (IC2b as a buffer), the Level pot, and the output stage.
    All of it sits on a 9 V supply around a 4.5 V reference.

    Controls: Dist, High, Middle, Mid Freq, Low, Level.
*/
class MetalZoneStyleDistortionProcessor : public EffectProcessor
{
public:
    MetalZoneStyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Metal Zone-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8a8f99); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { ic3b, ic3a, ic4b, ic4a, ic1a, ic2a, output, ic3aPrev, ic3aP, ic3aQ };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept;

private:
    static constexpr int numBlocks = 5;

    struct Channel
    {
        std::array<NodalCircuit, numBlocks> blk;
        int srcIn = 0;
        std::array<int, numBlocks> srcPrev {}; // blocks 1..5: the previous block's output
        std::array<NodalCircuit::Node, numBlocks> out {};
        NodalCircuit::Node nOut = 0, dbgPrev = 0, dbgP = 0, dbgQ = 0, dbgIc4b = 0;
        int rDist = 0, rHighA = 0, rHighB = 0, rLowA = 0, rLowB = 0, rMidA = 0, rMidB = 0, rFreqA = 0, rFreqB = 0, rLevTop = 0, rLevBottom = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs { double dist, high, middle, midFreq, low, level; };
    void updatePots (const Knobs& k);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* distParam = nullptr;
    juce::AudioParameterFloat* highParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* midFreqParam = nullptr;
    juce::AudioParameterFloat* lowParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedDist, smoothedHigh, smoothedMiddle, smoothedMidFreq, smoothedLow, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
