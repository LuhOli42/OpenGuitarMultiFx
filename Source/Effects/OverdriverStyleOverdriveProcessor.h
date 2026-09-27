#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Colorsound Overdriver-style overdrive (Sola Sound, 1971), modelled at component level on NodalCircuit from the
    maker's own drawing (Issue 1, 14 June 1971, MD1066; docs/circuits/OverdriverStyleOverdrive.md -- read that file, not
    this comment). Three BC109 transistors, one block:
      - TR1 + TR2: a directly coupled pair (TR1's collector IS TR2's base) with DC feedback -- a 150K from TR2's emitter to
        TR1's base -- and AC feedback, a 12K from TR2's output back to TR1's emitter, plus 200 pF collector-base on TR2.
        The Gain control is a 10K rheostat in series with a 25 uF across TR1's 6.8K emitter resistor, so it moves the
        stage's gain rather than its bias;
      - a Baxandall bass/treble network (two 100K pots, 4.7K, 39K, 5.6K, 0.01 and 0.1 uF) around TR3, which is its
        inverting amplifier: the network's input is TR2's output, its far end returns to TR3's COLLECTOR through 25 uF, and
        its wipers' common node drives TR3's base through 0.1 uF. TR3 is a plain common-emitter stage (150K/33K bias,
        470 ohm bypassed emitter, 1.8K collector load);
      - 0.22 uF to the output. There is no volume control on the drawing.
    The drawing marks the DC operating points (TR2 and TR3: collector 5 V, emitter 1 V), which the tests check.

    Controls: Gain, Bass, Treble.
*/
class OverdriverStyleOverdriveProcessor : public EffectProcessor
{
public:
    OverdriverStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Overdriver-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd9a441); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugTr1Collector() const noexcept { return channels[0].c.voltage (channels[0].nX); }
    double debugTr2Collector() const noexcept { return channels[0].c.voltage (channels[0].nC2); }
    double debugTr2Emitter() const noexcept { return channels[0].c.voltage (channels[0].nE2); }
    double debugTr3Collector() const noexcept { return channels[0].c.voltage (channels[0].nC3); }
    double debugTr3Emitter() const noexcept { return channels[0].c.voltage (channels[0].nE3); }
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
        NodalCircuit::Node nX = 0, nC2 = 0, nE2 = 0, nC3 = 0, nE3 = 0, nOut = 0;
        int rGain = 0;
        int rBassLeft = 0, rBassRight = 0, rTrebleLeft = 0, rTrebleRight = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double gain, double bass, double treble);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedBass, smoothedTreble;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
