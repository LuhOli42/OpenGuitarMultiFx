#pragma once

#include "EffectProcessor.h"

#include <array>

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
      Crossover    -- a 2-band Linkwitz-Riley (24 dB/oct) split: one band goes
                      to any output, the chain carries on with the other band
                      (or full range). Low + high sum back flat (an all-pass),
                      so a sub/top or bass-amp/DI split recombines cleanly.

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

/** The part every "send to a device output" block shares: adds a ramped copy of a stereo signal to one OutputSend
    destination (a pair, or one channel fed the mono fold), marks the channels it wrote, and on a destination change
    fades out, switches once silent, then fades back in. Without a DeviceIo (offline render) it writes nothing. */
class DeviceOutputSend
{
public:
    void prepare (double sampleRate);
    void reset() noexcept { started = false; }

    /** `wanted` is a DeviceIoChoices output choice; `targetGain` 0 fades the send out (bypass, level at -inf). */
    void process (EffectProcessor::DeviceIo* io, const float* left, const float* right, int numSamples, int wanted,
                  float targetGain) noexcept;

    float getCurrentGain() const noexcept { return gain.getCurrentValue(); }

private:
    juce::SmoothedValue<float> gain;
    int activeDestination = 0;
    bool started = false;
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
    bool prefersScrollingSelector (const juce::AudioParameterFloat& p) const override { return &p == destination; }
    const char* getName() const override { return "Output Send"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8d94a5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* destination = nullptr;
    juce::AudioParameterFloat* levelDb = nullptr;
    juce::AudioParameterFloat* muteThru = nullptr;

    DeviceOutputSend send;
    juce::SmoothedValue<float> thruGain;
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
    bool prefersScrollingSelector (const juce::AudioParameterFloat& p) const override { return &p == source; }
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

class CrossoverProcessor : public EffectProcessor
{
public:
    CrossoverProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    bool usesDeviceIo() const override { return true; }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    bool prefersScrollingSelector (const juce::AudioParameterFloat& p) const override { return &p == destination; }
    const char* getName() const override { return "Crossover"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8d94a5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    enum Band { lows = 0, highs = 1 };

private:
    /** One Simper/Cytomic TPT state-variable section (Butterworth Q), both outputs per tick. */
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
        void clear() noexcept { ic1 = ic2 = 0.0f; }
        void tick (float x, float a1, float a2, float a3, float k, float& lp, float& hp) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            lp = v2;
            hp = x - k * v1 - v2;
        }
    };

    /** LR4 = two Butterworth sections per band: a shared first section, then one more on each output. */
    struct Lr4
    {
        Svf first, low, high;
        void clear() noexcept { first.clear(); low.clear(); high.clear(); }
    };

    void updateCoefficients (float frequency) noexcept;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* frequency = nullptr;
    juce::AudioParameterFloat* sendBand = nullptr;
    juce::AudioParameterFloat* destination = nullptr;
    juce::AudioParameterFloat* levelDb = nullptr;
    juce::AudioParameterFloat* chainMode = nullptr;

    double sampleRate = 48000.0;
    std::array<Lr4, 2> filters;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedFrequency;
    juce::SmoothedValue<float> chainSplit;   // 0 = chain full range, 1 = chain keeps the other band
    juce::AudioBuffer<float> sendScratch;
    DeviceOutputSend send;
    int activeBand = lows;   // a band swap fades send + chain split out, swaps, fades back in
    bool started = false;
};

} // namespace openguitarmultifx
