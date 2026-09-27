#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A BOSS BD-2 (Blues Driver)-style overdrive, modelled at transistor level
    from the pedal's factory board schematic ("BD-2 MT BOARD") on
    NodalCircuit -- see docs/circuits/BD2StyleOverdrive.md, which is the file
    to read to understand this processor, not this comment or the .cpp.

    The BD-2 is not a chain of simple stages: its two gain stages are
    DISCRETE op-amps (a JFET differential pair driving a PNP second stage,
    with a Miller cap and the Gain pot as a rheostat in the feedback), with a
    fixed passive tone stack and two back-to-back pairs of series diodes
    between them, a passive Tone/Level section, and an op-amp "peak filter"
    whose shunt leg is a transistor gyrator. Much of the pedal's hard
    clipping comes from those discrete stages running into their rails
    (~0 V and the 8 V supply), which a transistor-level model reproduces
    for free where an ideal-op-amp model could not.

    Five blocks, cut where the next stage cannot load the previous one back
    (a JFET gate, an op-amp (+) pin, an ideal op-amp output):

      A  input JFET buffer + effect-path coupling to stage 1's gate
      B  gain stage 1 (JFET pair + PNP) + tone stack + clippers + C27/R35
      C  gain stage 2 (same topology) + low-pass + Tone + Level + C10/R13
      D  peak-filter op-amp (ideal) + gyrator (Q7)
      E  output emitter follower

    Controls, as on the pedal: Gain (dual-gang rheostat, both stages), Tone,
    Level.
*/
class BD2StyleOverdriveProcessor : public EffectProcessor
{
public:
    /** The two gain stages as macro-models of their discrete op-amps (finite gain, one pole, output swing) instead of
        transistor by transistor. Default true; false builds the full transistor-level netlist (the reference the
        equivalence tests compare against). Read by prepare(). See docs/circuits/BD2StyleOverdrive.md. */
    static inline bool reducedOrder = true;

    BD2StyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "BD-2-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f6fd0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests: channel 0's stage-1 output (Q9 collector), stage-2 output (Q12 collector),
        and stage-1 collector's DC point. */
    double debugStage1Out() const noexcept { return channels[0].b.voltage (channels[0].nC9); }
    double debugStage2Out() const noexcept { return channels[0].c.voltage (channels[0].nS2); }

    /** True when every block's DC operating point converged in prepare(). */
    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit a, b, c, d, e;

        int srcIn = 0;                  // a: input signal
        NodalCircuit::Node nG10 = 0;    // a: stage-1 gate node (read out)
        int srcG10 = 0;                 // b: driven gate node
        NodalCircuit::Node nC9 = 0, nG14 = 0; // b: stage-1 output, stage-2 gate node
        int srcG14 = 0;                 // c: driven gate node
        NodalCircuit::Node nS2 = 0, nP = 0;   // c: stage-2 output, peak-filter (+) node
        int srcP = 0;                   // d: driven (+) node
        NodalCircuit::Node nO7 = 0;     // d: peak-filter op-amp output
        int srcO7 = 0;                  // e: driven op-amp output
        NodalCircuit::Node nOut = 0;    // e: pedal output

        int rGain1 = 0, rGain2 = 0;              // Gain rheostats (b, c)
        int rToneTop = 0, rToneBottom = 0;       // Tone pot segments (c)
        int rLevelTop = 0, rLevelBottom = 0;     // Level pot segments (c)
    };

    void buildChannel (Channel& ch);
    void updatePots (double gainKnob, double toneKnob, double levelKnob);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTone, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    bool channel1Stale = false;   // dual-mono shortcut has left channel 1's circuit behind channel 0's
    bool channelsSynced = true;   // both circuits known to hold identical state
    long long identicalRun = 0;   // consecutive identical-input samples

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
