#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Nobels DT-1-style distortion, modelled at component level on NodalCircuit from Nobels Electronics' own circuit
    diagram (DT-1, rev 4, Feb 2000; docs/circuits/DT1StyleDistortion.md -- read that file, not this comment).

    Two 4558 stages driven by ONE pot:
      - stage 1 (non-inverting) has two red LEDs in its feedback, so it clips softly at ~1.7 V;
      - stage 2 (inverting) has ONE 4148 one way and TWO in series the other in its feedback, so it clips asymmetrically;
      - the 250K Distortion pot is wired with its wiper on stage 1's output, one end into stage 1's feedback and the other
        end into stage 2's input. Turning it up therefore raises stage 1's gain (10K + R -> up to ~80x) AND lowers stage
        2's input resistance (250K -> 0, so ~1.5x -> ~118x) at the same time: ~9400x (79 dB) in total at the top.
    After that: a passive Tone (a 4.7 nF treble path against a 15K/68 nF bass path, blended by a 50K pot), a JFET buffer,
    Level (50KA), and a transistor output buffer. Three blocks' worth of JFET switching (the electronic bypass) is not
    modelled; the effect path is the one that is always on here.

    Controls: Distortion, Tone, Level.
*/
class DT1StyleDistortionProcessor : public EffectProcessor
{
public:
    DT1StyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "DT-1-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd1452f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugStage2Out() const noexcept { return channels[0].b.voltage (channels[0].nOp2); }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].a.averageIterations() + channels[0].b.averageIterations(); }

private:
    struct Channel
    {
        NodalCircuit a, b;
        int srcIn = 0, srcOp1 = 0;
        NodalCircuit::Node nOp1 = 0, nOp2 = 0, nOut = 0;
        int rDistFeedback = 0, rDistSeries = 0;       // the Distortion pot's two segments
        int rToneTreble = 0, rToneBass = 0;
        int rLevelTop = 0, rLevelBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double distortion, double tone, double level);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* distortionParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedDistortion, smoothedTone, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
