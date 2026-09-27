#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A BOSS HM-2 (Heavy Metal)-style distortion, modelled at component level
    on NodalCircuit from the pedal's schematic (see
    docs/circuits/HM2StyleDistortion.md -- read that file to understand this
    processor, not this comment or the .cpp).

    What makes the HM-2 what it is, all reproduced here from the netlist:
      - a JFET input buffer into two very high-gain discrete transistor
        stages (NPN, then PNP, each self-biased through a collector-base
        feedback network);
      - an op-amp whose feedback has ONE diode one way and TWO in series the
        other (asymmetric, like the OD-1) and whose inverting leg is fed
        through the DIST pot -- which is wired as a grounded-wiper divider,
        so turning it both loads the gain stage's collector and sets the
        op-amp's gain leg;
      - a germanium diode pair placed IN SERIES with the signal, then a
        silicon pair shunting it to ground;
      - the "Color" section: three unity-gain followers each turned into a
        simulated inductor (a gyrator) and hung on the LOW/HIGH pots of a
        differential op-amp, giving the resonant bass and mid/high
        boost-and-cut.

    Three blocks: (1) input JFET + the two transistor stages + the DIST
    branch + the clipping op-amp (they must share a block: see the note in
    the .cpp on why the op-amp's (-) node cannot be split off); (2) the diode
    network after the op-amp; (3) the Color section and Level. Controls:
    Dist, Low, High, Level.
*/
class HM2StyleDistortionProcessor : public EffectProcessor
{
public:
    /** Q6 and Q7 as one-port transistors with a smooth collector-current limit instead of two-port Ebers-Moll (see
        NodalCircuit::addBjtSaturating). **Default false**: it measures 18% cheaper and matches level (0.86 dB) and octave-band
        shape (1.15 dB) on chords and a real DI, but the user heard a volume "pop" at high gain, and the only place the two
        differ is hard edges (peaks up to +1 dB on a square wave / gated input), so the full netlist stays the default.
        Kept for the equivalence tests. */
    static inline bool reducedOrder = false;

    HM2StyleDistortionProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "HM-2-Style Distortion"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffe07a1f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests: channel 0's transistor-stage output (Q7 collector) and the clipping op-amp output. */
    double debugStageOut() const noexcept { return channels[0].b1.voltage (channels[0].nC7); }
    double debugClipOut() const noexcept { return channels[0].b1.voltage (channels[0].nO1); }

    bool dcConverged() const noexcept { return dcOk; }
    long long blockFailures (int block) const noexcept { return perBlockFailures[block]; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit b1, b2, b3;

        int srcIn = 0;                              // b1: input signal
        NodalCircuit::Node nC7 = 0, nN6 = 0, nO1 = 0; // b1: stage output, op-amp (-) node, op-amp output
        int srcO1 = 0;                              // b2: driven from the op-amp output
        NodalCircuit::Node nP = 0;                  // b2: buffer input
        int srcFin = 0;                             // b3: driven from the buffer
        NodalCircuit::Node nOut = 0;                // b3: pedal output

        int rDistA = 0, rDistB = 0;                 // DIST pot segments (b1)
        int rLowF = 0, rLowN = 0, rHighF = 0, rHighN = 0; // LOW / HIGH pot segments (b3)
        int rLevelTop = 0, rLevelBottom = 0;        // Level pot (b3)
    };

    void buildChannel (Channel& ch);
    void updatePots (double dist, double low, double high, double level);

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* distParam = nullptr;
    juce::AudioParameterFloat* lowParam = nullptr;
    juce::AudioParameterFloat* highParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedDist, smoothedLow, smoothedHigh, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    bool channel1Stale = false;   // dual-mono shortcut has left channel 1's circuit behind channel 0's
    bool channelsSynced = true;   // both circuits known to hold identical state
    long long identicalRun = 0;   // consecutive identical-input samples
    long long perBlockFailures[3] { 0, 0, 0 };

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
