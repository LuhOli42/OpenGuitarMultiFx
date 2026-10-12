#pragma once

#include "Effects/EffectProcessor.h"
#include "BbdChorusParts.h"

namespace openguitarmultifx
{

/**
    A Boss DC-2 Dimension C-style stereo chorus, from the DC-2 schematic (docs/circuits/DC2Dimension.md -- read that,
    not this comment).

    The Dimension recipe: ONE triangle LFO drives TWO BBDs (MN3207 + MN3102 each) in antiphase -- IC5a/IC6b invert the
    LFO for one clock -- so as one delay grows the other shrinks and the pitch wobble of each cancels in the mix, leaving
    width without the obvious chorus "seasick" swirl. Around them: IC1 pre-emphasis (R7 47K, R8 10K + C5 4.7n, R10 33K
    || C8 100p), an NE570 compressor in front of the BBDs and an NE570 expander after each, the BBDs' 3rd-order filters
    (10K / 470p, 1.8n to the follower, 220p -- wider than the CE-2's, for the MN3207's higher clock), and two IC12
    mixers with the matching de-emphasis: each output = dry + its own BBD, less a little of the other one's.

    Four mode buttons, no knobs: two LFO speeds (modes 1-2 slow, 3-4 fast) and two depths. The mode network's resistor
    values are on the drawing but its exact wiring is not legible; speeds and depths are calibrated (see the doc).

    One input; a stereo chain gets OUTPUT A left and OUTPUT B right (a mono chain gets A).
*/
class DC2StyleChorusProcessor : public EffectProcessor
{
public:
    DC2StyleChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "DC-2"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8f6fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugClockHz (int which) const noexcept { return which == 0 ? clockA : clockB; }
    double debugDelaySeconds (int which) const noexcept { return bbdA.delaySeconds (which == 0 ? clockA : clockB); }
    double debugLfoHz() const noexcept { return lfo.frequencyHz(); }

private:
    void updateMode (int mode) noexcept;

    analog::Filter inputCoupling, preEmphasis, mixDry, mixOwnA, mixOwnB, mixCross, mixCrossB;
    BucketBrigadeDelay bbdA, bbdB;
    bbdchorus::Ne570 compressor, expanderA, expanderB;
    bbdchorus::TriangleLfo lfo;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* modeParam = nullptr;

    int currentMode = -1;
    double swing = 0.05;
    double sampleRate = 0.0, dt = 0.0, clockA = 120000.0, clockB = 120000.0;
};

} // namespace openguitarmultifx
