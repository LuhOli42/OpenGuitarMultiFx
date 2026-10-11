#pragma once

#include "EffectProcessor.h"
#include "DelayLine.h"

namespace openguitarmultifx
{

/**
    A freeze/infinite-sustain effect: while `Hold` is engaged, whatever was
    playing at the moment it was pressed loops forever from a captured
    buffer window instead of continuing to follow live input -- the
    classic "freeze pedal" behaviour (Boss RE-20/Line 6-style), and a
    completely different interaction model from every other Delay
    processor here (all continuous knobs; this one reacts to a bool-like
    parameter crossing a threshold, same as `EffectProcessor::isBypassed()`
    conceptually but user-controlled per-instance rather than global).

    Continuously records into a circular buffer while NOT held (so
    whatever is ringing right when Hold engages is exactly what gets
    captured, not silence); while held, recording pauses and a fixed
    `captureMs`-long window loops from wherever it was when Hold engaged.
    `hold` is an `AudioParameterFloat` (0/1), not `AudioParameterBool` --
    `EffectProcessor::getState()`'s default float-only serialization would
    silently drop a bool parameter from presets, so every user-facing
    control in this codebase is a float even where the value is
    conceptually a toggle.
*/
class HoldProcessor : public EffectProcessor
{
public:
    HoldProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Hold"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f5f9e); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxCaptureMs = 750.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* hold = nullptr;
    juce::AudioParameterFloat* captureMs = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    FractionalDelayLine line;
    bool wasHeld = false;
    int loopStartPos = 0;
    int loopOffset = 0;
    int frozenLoopLengthSamples = 0;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedMix;
};

} // namespace openguitarmultifx
