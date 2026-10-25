#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss ODB-3-style bass overdrive, modelled component by component on NodalCircuit from the full ODB-3 schematic
    (docs/circuits/ODB3StyleOverdrive.md -- read that file to understand this processor).

    The drive path is two DISCRETE op-amps -- the same Boss JFET-pair + PNP stage as the BD-2 -- the first with the
    Gain pot in its feedback, the second with the red-LED pair in its feedback; then a bridged-T network into the
    Balance pot. The other end of Balance is a clean op-amp stage whose gain is the second gang of the Gain pot.
    Balance's wiper feeds the Low / High active tone (a transistor gyrator on the Low band) and the Level.

    Blocks, cut where the next stage cannot load the previous one back (a JFET gate, an op-amp output, a follower):
      A  input JFET follower + effect-path coupling to the stage-1 gate
      B  gain stages 1 and 2 (JFET pairs + PNPs, the LED feedback) + R20 / C26 + bridged-T + R28
      C  the clean op-amp (IC1b) with the Gain pot's second gang
      D  Balance, IC1a tone stage with the gyrator, Level, output follower

    Five controls, as on the pedal: Level, High, Low, Balance, Gain.
*/
class ODB3StyleOverdriveProcessor : public EffectProcessor
{
public:
    ODB3StyleOverdriveProcessor();

    /** The two discrete gain stages as op-amp macros (the BD-2's) instead of transistor by transistor: default true,
        what ships; false builds the transistor-level reference the equivalence test compares against. Read by
        prepare(). */
    static inline bool reducedOrder = true;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "ODB-3"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc23c3c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Q1, the input JFET follower: gate and source volts at the last sample. */
    struct DebugBiasPoint { float vGate, vSource, vSupply; };
    DebugBiasPoint getDebugBiasPoint() const noexcept;
    /** Stage-1 and stage-2 output (the PNP collectors) at the last sample. */
    double debugStage1Out() const noexcept;
    double debugStage2Out() const noexcept;
    double getClipperFailureRate() const noexcept
    {
        return solveCount > 0 ? (double) solveFailures / (double) solveCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit a, b, c, d;
        NodalCircuit::DynamicState aRest, bRest, cRest, dRest;
        int srcIn = 0, srcB = 0, srcCPlus = 0, srcDClean = 0, srcDDrive = 0;
        int rGain1 = 0, rGain2 = 0, rBalTop = 0, rBalBot = 0;
        int rLowPlus = 0, rLowMinus = 0, rHighPlus = 0, rHighMinus = 0, rLevelTop = 0, rLevelBot = 0;
        NodalCircuit::Node nG1 = 0, nS1 = 0, nG3 = 0, nO1 = 0, nO2 = 0, nBn = 0, nClean = 0, nOut = 0;
        int failStreak = 0;
    };
    std::array<Channel, 2> channels;

    void buildChannel (Channel& ch);
    void updatePots (float levelValue, float highValue, float lowValue, float balanceValue, float gainValue);
    bool solve (Channel& ch, double x);

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterFloat* high = nullptr;
    juce::AudioParameterFloat* low = nullptr;
    juce::AudioParameterFloat* balance = nullptr;
    juce::AudioParameterFloat* gain = nullptr;

    juce::SmoothedValue<float> smoothedLevel, smoothedHigh, smoothedLow, smoothedBalance, smoothedGain;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long solveCount = 0, solveFailures = 0;
    bool channelsSynced = true, channel1Stale = false;
    long long identicalRun = 0;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
