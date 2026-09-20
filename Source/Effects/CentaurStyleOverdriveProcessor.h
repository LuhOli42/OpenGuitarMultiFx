#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Klon Centaur-style overdrive, modelled from a redraw of the Centaur's
    schematic (see docs/circuits/CentaurStyleOverdrive.md -- read that file
    to understand this processor, not this comment or the .cpp).

    Unlike the earlier hand-derived processors this one is a netlist run on
    NodalCircuit (modified nodal analysis), because the Centaur's signal
    path is not a chain: the gain stage, two feed-forward networks (one of
    which is referenced through the SECOND gang of the Gain pot) and the
    summing amplifier all interact. Three blocks, cut where an ideal op-amp
    output cannot be loaded back:

      block 0  input buffer (R1, C1, R2 -> ideal follower)
      block 1  gain stage (op-amp + Ge diodes) + both feed-forward networks
               + summing op-amp
      block 2  active treble stage + Volume pot + the clean "bleed" path
               that rides past the whole effect to the output

    Controls, as on the pedal: Gain (dual-gang), Treble, Level.
*/
class CentaurStyleOverdriveProcessor : public EffectProcessor
{
public:
    CentaurStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Centaur-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc9a227); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Channel 0's gain-stage op-amp output (before the clipping diodes), for
        verifying the gain stage against the published 40 dB @ ~1 kHz. */
    double debugGainStageOutput() const noexcept { return channels[0].block1.voltage (channels[0].o1); }

    /** Fraction of samples on which any block's Newton solve failed. */
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit block0, block1, block2;

        int srcIn = 0;                       // block0: input signal
        NodalCircuit::Node nodeA = 0;        // block0: buffer's (+) node == buffer output
        int srcB1 = 0, srcB2 = 0;            // blocks 1/2: buffered signal B
        int srcU = 0;                        // block 2: summing-amp output u
        NodalCircuit::Node o1 = 0, u = 0;    // block1 nodes read out
        NodalCircuit::Node out = 0;          // block2: pedal output
        int rG1 = 0, rH = 0, rB2BG = 0, rBGVB = 0; // Gain pot (both gangs)
        int rToneTop = 0, rToneBot = 0;            // Treble pot segments (with R21/R23 folded in)
        int rVolSeries = 0, rVolBottom = 0;        // Level pot (with R25 folded in)
    };

    void buildChannel (Channel& ch, double sampleRate);
    void updatePots (double gainFrac, double trebleFrac, double levelFrac);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* treble = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool channel1Stale = false;   // dual-mono shortcut has left channel 1's circuit behind channel 0's
    bool channelsSynced = true;   // both circuits known to hold identical state
    long long identicalRun = 0;   // consecutive identical-input samples

    static constexpr int controlInterval = 16; // samples between pot -> resistance updates
};

} // namespace openguitarmultifx
