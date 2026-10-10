#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    A single-footswitch-style looper (Boss RC-1 pattern): one `Trigger`
    parameter advances a state machine each time it crosses from 0 to 1
    (same edge-detected float-as-button convention HoldProcessor uses,
    for the same reason -- getState()'s float-only serialization), and a
    separate `Clear` trigger resets everything back to idle from any state.

        Idle --trigger--> Recording --trigger--> Playing
                                                     |  ^
                                                 trigger|  |trigger
                                                     v  |
                                                  Overdubbing

    The dry signal always passes through unchanged (a looper adds a layer,
    it doesn't replace your live playing) -- only whether input ALSO gets
    written into the loop buffer changes between states. Recording fixes
    the loop length at whatever time elapsed before the second trigger;
    Overdubbing writes new content at `overdubDecay` times the existing
    content plus the new input, so old layers fade rather than piling up
    to infinity over many overdub passes.
*/
class LooperProcessor : public EffectProcessor
{
public:
    LooperProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Looper"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d72b8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Free-form status for generic UI display -- which state the looper is in. */
    juce::String getStatusText() const override;

private:
    enum class State
    {
        idle,
        recording,
        playing,
        overdubbing
    };

    static constexpr float maxLoopSeconds = 20.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* trigger = nullptr;
    juce::AudioParameterFloat* clear = nullptr;
    juce::AudioParameterFloat* overdubDecay = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::AudioBuffer<float> loopBuffer;
    State state = State::idle;
    bool triggerWasHigh = false;
    bool clearWasHigh = false;
    int writePos = 0;
    int loopLengthSamples = 0;
    double currentSampleRate = 0.0;

    juce::SmoothedValue<float> smoothedLevel;
    juce::SmoothedValue<float> smoothedOverdubDecay;
};

} // namespace openguitarmultifx
