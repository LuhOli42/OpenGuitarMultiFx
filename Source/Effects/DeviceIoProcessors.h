#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    The two Utility blocks that reach past their point in the chain (EffectProcessor::DeviceIo, wired by
    SignalGraph -- no effect type is special-cased there):

      Output Send  -- copies the signal at its point in the chain to any output pair or single channel (a DI to
                      Out 3/4 before the amp, a tuner feed), or into a LATER line's input ("Line 3"). Its Crossover
                      (Lows / Highs, Linkwitz-Riley 24 dB/oct) sends one band only; Thru says what the chain carries
                      on with: Full, Mute, or the Other band (so lows go to Line 3 and this line keeps the highs --
                      the two bands sum back flat).
      Input Source -- brings any physical input (one channel or a pair), or an EARLIER line's output, into the chain
                      at its point, optionally one band of it. Mix: 0% = the chain alone, 50% = both at full level
                      (a sum), 100% = the input alone.

    Lines are forward-only, like row links: a send to its own or an earlier line, or a read of its own or a later
    line, stays silent. Everything ramps over ~10 ms -- knob moves, bypass (process() still runs during the bypass
    crossfade), and a destination/source/band change (fade out, switch, fade back in), so none of them clicks.
*/

/** The destinations / sources the selectors offer, in knob order. */
struct DeviceIoChoices
{
    static constexpr int numOutputPairs = 4;   // Out 1/2 .. Out 7/8
    static constexpr int numOutputChoices = numOutputPairs * 3;   // per pair: the pair, then its L and R alone
    static constexpr int numInputChannels = 8; // In 1 .. In 8, then the pairs In 1/2 .. In 7/8
    static constexpr int numInputChoices = numInputChannels + numInputChannels / 2;
    static constexpr int numLines = EffectProcessor::DeviceIo::maxLines;   // then Line 1 .. Line 4, on both selectors
    static constexpr int numOutputDestinations = numOutputChoices + numLines;
    static constexpr int numInputSources = numInputChoices + numLines;

    static int outputPairStart (int choice) noexcept { return 2 * (choice / 3); }
    /** 0 = stereo, 1 = left only, 2 = right only (OutputRoute::Channels order). */
    static int outputChannels (int choice) noexcept { return choice % 3; }
    /** -1 for a device output, else the line (lane index) the choice names. */
    static int outputLine (int choice) noexcept { return choice >= numOutputChoices ? choice - numOutputChoices : -1; }
    static juce::String outputName (int choice);
    /** Whether the open interface has this output (lines always exist). Unknown interface = all of them. */
    static bool isOutputAvailable (int choice) noexcept;

    /** First device input channel of a choice, and whether it is a stereo pair. */
    static int inputFirstChannel (int choice) noexcept { return choice < numInputChannels ? choice : 2 * (choice - numInputChannels); }
    static bool inputIsPair (int choice) noexcept { return choice >= numInputChannels && choice < numInputChoices; }
    static int inputLine (int choice) noexcept { return choice >= numInputChoices ? choice - numInputChoices : -1; }
    static juce::String inputName (int choice);
    static bool isInputAvailable (int choice) noexcept;
};

/** A 4th-order Linkwitz-Riley split (two Butterworth TPT state-variable sections per band, the first one shared), up
    to 2 channels, with a smoothed frequency. Low + high = an all-pass: flat magnitude. */
class LinkwitzRileySplit
{
public:
    void prepare (double sampleRate, float frequencyHz);
    void reset() noexcept;
    void setTargetFrequency (float hz) noexcept { frequency.setTargetValue (hz); }

    /** Call once per sample before split()/select(): advances the frequency ramp. */
    void tick() noexcept;
    void split (int channel, float x, float& low, float& high) noexcept;

    enum Band { full = 0, lows = 1, highs = 2 };
    /** The `band` part of x (full = x untouched) and, in `other`, the complementary band (full: x). */
    void select (int channel, float x, int band, float& picked, float& other) noexcept;

private:
    struct Svf
    {
        float ic1 = 0.0f, ic2 = 0.0f;
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
    struct Channel { Svf first, low, high; };

    void updateCoefficients (float hz) noexcept;

    double sampleRate = 48000.0;
    std::array<Channel, 2> channels {};
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> frequency;
    int sinceUpdate = 0;
};

/** The part every "send somewhere" block shares: adds a ramped copy of a stereo signal to one destination (a device
    pair, one device channel fed the mono fold, or a later line), marks what it wrote, and on a change of `key` fades
    out, switches once silent (and once the caller allows it), then fades back in. Without a DeviceIo (offline render)
    it writes nothing. */
class DeviceOutputSend
{
public:
    void prepare (double sampleRate);
    void reset() noexcept { started = false; }

    /** `key` = destination + numOutputDestinations * (anything else whose change must not click, e.g. the band). */
    void process (EffectProcessor::DeviceIo* io, const float* left, const float* right, int numSamples, int key,
                  float targetGain, bool maySwitch = true) noexcept;

    /** The key now being played -- set up the signal for the NEXT process() from this, not from the knobs. */
    int getActiveKey (int fallback) const noexcept { return started ? activeKey : fallback; }
    float getCurrentGain() const noexcept { return gain.getCurrentValue(); }

private:
    juce::SmoothedValue<float> gain;
    int activeKey = 0;
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
    bool isSelectorOptionAvailable (const juce::AudioParameterFloat& p, int step) const override;
    bool isParameterActive (const juce::AudioParameterFloat& p) const override;
    const char* getName() const override { return "Output Send"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8d94a5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    enum Thru { thruFull = 0, thruMute = 1, thruOtherBand = 2 };   // 1 = the old "Mute Thru: On", presets keep it

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* destination = nullptr;
    juce::AudioParameterFloat* levelDb = nullptr;
    juce::AudioParameterFloat* thru = nullptr;
    juce::AudioParameterFloat* band = nullptr;
    juce::AudioParameterFloat* frequency = nullptr;

    DeviceOutputSend send;
    LinkwitzRileySplit crossover;
    juce::AudioBuffer<float> sendScratch;
    juce::SmoothedValue<float> thruGain, otherBandMix;   // otherBandMix: 0 = chain full range, 1 = the other band
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
    bool isSelectorOptionAvailable (const juce::AudioParameterFloat& p, int step) const override;
    bool isParameterActive (const juce::AudioParameterFloat& p) const override;
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
    juce::AudioParameterFloat* band = nullptr;
    juce::AudioParameterFloat* frequency = nullptr;

    LinkwitzRileySplit crossover;
    juce::SmoothedValue<float> inputGain, chainGain;
    int activeKey = 0;   // source + numInputSources * band
    bool started = false;
};

} // namespace openguitarmultifx
