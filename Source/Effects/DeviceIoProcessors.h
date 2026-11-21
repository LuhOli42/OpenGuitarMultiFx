#pragma once

#include "EffectProcessor.h"

namespace openguitarmultifx
{

/**
    The two Utility blocks that reach past their row to the physical device
    (EffectProcessor::DeviceIo, wired by SignalGraph -- no effect type is
    special-cased there):

      Output Send  -- copies the signal at its point in the chain to any
                      output pair or single channel (a DI to Out 3/4 before
                      the amp, a tuner feed, a click track). The chain goes on
                      unchanged unless Mute Thru is on.
      Input Source -- brings any physical input (one channel or a pair) into
                      the chain at its point. Mix: 0% = the chain alone,
                      50% = both at full level (a sum), 100% = the input alone.

    Both ramp their level over ~10 ms -- on knob moves, on bypass (process()
    still runs during the bypass crossfade) and on a destination/source change
    (fade out, switch, fade back in), so none of them clicks.
*/

/** The destinations Output Send offers, in knob order: per pair, the pair, then its L and R channels alone. */
struct DeviceIoChoices
{
    static constexpr int numOutputPairs = 4;   // Out 1/2 .. Out 7/8
    static constexpr int numOutputChoices = numOutputPairs * 3;
    static constexpr int numInputChannels = 8; // In 1 .. In 8, then the pairs In 1/2 .. In 7/8
    static constexpr int numInputChoices = numInputChannels + numInputChannels / 2;

    static int outputPairStart (int choice) noexcept { return 2 * (choice / 3); }
    /** 0 = stereo, 1 = left only, 2 = right only (OutputRoute::Channels order). */
    static int outputChannels (int choice) noexcept { return choice % 3; }
    static juce::String outputName (int choice);

    /** First device input channel of a choice, and whether it is a stereo pair. */
    static int inputFirstChannel (int choice) noexcept { return choice < numInputChannels ? choice : 2 * (choice - numInputChannels); }
    static bool inputIsPair (int choice) noexcept { return choice >= numInputChannels; }
    static juce::String inputName (int choice);
};

class OutputSendProcessor : public EffectProcessor
{
public:
    OutputSendProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool usesDeviceIo() const override { return true; }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    bool prefersSteppedKnob (const juce::AudioParameterFloat& p) const override { return &p == destination; }
    const char* getName() const override { return "Output Send"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8d94a5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* destination = nullptr;
    juce::AudioParameterFloat* levelDb = nullptr;
    juce::AudioParameterFloat* muteThru = nullptr;

    juce::SmoothedValue<float> sendGain, thruGain;
    int activeDestination = 0;   // what the send writes now; a knob change fades out, then switches
    bool started = false;
};

class InputSourceProcessor : public EffectProcessor
{
public:
    InputSourceProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool usesDeviceIo() const override { return true; }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    bool prefersSteppedKnob (const juce::AudioParameterFloat& p) const override { return &p == source; }
    const char* getName() const override { return "Input Source"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8d94a5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The Mix law: chain and input gains for a mix in 0..1 (both 1 at the middle). */
    static float chainGainForMix (float mix) noexcept { return juce::jmin (1.0f, 2.0f * (1.0f - mix)); }
    static float inputGainForMix (float mix) noexcept { return juce::jmin (1.0f, 2.0f * mix); }

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* source = nullptr;
    juce::AudioParameterFloat* levelDb = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    juce::SmoothedValue<float> inputGain, chainGain;
    int activeSource = 0;
    bool started = false;
};

} // namespace openguitarmultifx
