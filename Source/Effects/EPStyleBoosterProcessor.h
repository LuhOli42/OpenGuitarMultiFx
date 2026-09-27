#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Echoplex EP-3 preamp-style booster, modelled at component level on NodalCircuit from the EP-3 schematic
    (serial 12961-28591; docs/circuits/EPStyleBooster.md -- read that file, not this comment). The EP-3's record preamp
    is the circuit boutique "EP boosters" copy: a JFET stage (TIS58, unbypassed 3.3K source), the 500K Record Level pot
    with a 47K and 2 nF after its wiper, and a feedback-biased bipolar stage (2N3053 in the EP-3; 470K collector-base
    feedback, emitter bypassed). One block. The tape-echo half of the EP-3 (echo return, record amplifier and bias
    oscillator, heads) is not part of it.

    Control: Level (the Record Level pot).
*/
class EPStyleBoosterProcessor : public EffectProcessor
{
public:
    EPStyleBoosterProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "EP-Style Booster"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8fb85a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugJfetDrain() const noexcept { return channels[0].c.voltage (channels[0].nD); }
    double debugJfetSource() const noexcept { return channels[0].c.voltage (channels[0].nS); }
    double debugBjtBase() const noexcept { return channels[0].c.voltage (channels[0].nB); }
    double debugBjtCollector() const noexcept { return channels[0].c.voltage (channels[0].nC); }
    double debugBjtEmitter() const noexcept { return channels[0].c.voltage (channels[0].nE); }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].c.averageIterations(); }

private:
    struct Channel
    {
        NodalCircuit c;
        int srcIn = 0;
        NodalCircuit::Node nD = 0, nS = 0, nB = 0, nC = 0, nE = 0, nOut = 0;
        int rLevelTop = 0, rLevelBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double level);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
