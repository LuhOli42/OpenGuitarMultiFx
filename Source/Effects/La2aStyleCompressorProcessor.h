#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Teletronix LA-2A-style optical leveling amplifier, from Universal Audio's LA-2A manual (specifications, block diagram and
    schematic figures 5-7). What is modelled:

    * the T4 electro-luminescent attenuator: a divider of a fixed series resistor (R6 68K / R7 2.7K in the schematic) and a CdS
      photocell in shunt, lit by an EL panel that the side-chain drives. The cell is the reason the LA-2A sounds the way it does and
      it is what the model spends its effort on: it releases in TWO stages (about half of the reduction in ~60 ms, the rest over 0.5 s
      to 5 s, the longer the longer and deeper it has been compressing -- the "memory" of the cell), it attacks in ~10 ms, the
      light-to-resistance law is a power law (so it compresses gently at first and harder as the level rises), and it can shave up to
      40 dB;
    * a FEEDBACK side-chain (the side-chain amplifier listens to the output amplifier, after the attenuator): the loop makes the
      static curve; COMPRESS is a ~3:1 law and LIMIT a ~10:1 one (the switch changes the side-chain amplifier's feedback);
    * the output stage: the 12AX7 / 12BH7A amplifier, modelled as the static transfer curve of a Koren 12AX7 common-cathode stage
      (Rp 100K, B+ 250 V) whose drive is set so it distorts 0.35% (mostly second harmonic) at +10 dBm, the manual's spec;
    * PEAK REDUCTION sets the gain of the side-chain amplifier, GAIN is the output make-up.

    The side-chain is flat here (the manual describes no emphasis); the values that the manual does not give are estimates and are
    listed in docs/circuits/La2aStyleCompressor.md. Level convention: 0 dBu = -18 dBFS.
    Controls: Peak Reduction, Gain, Compress / Limit.
*/
class La2aStyleCompressorProcessor : public EffectProcessor
{
public:
    La2aStyleCompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "LA-2A-Style Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd9d4c7); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double getGainReductionDb() const noexcept { return grDb; }
    double getMemory() const noexcept { return memory; }

    /** The output stage's static transfer (unit small-signal gain) for a sample x in full-scale units: what the triode does to it. */
    double stage (double x) const noexcept;

    // The T4 cell (estimates fitted to the manual: 50% in ~60 ms, 0.5-5 s to finish, attack ~10 ms, 40 dB)
    static constexpr double attackFastSeconds = 0.010, attackSlowSeconds = 0.030;
    static constexpr double releaseFastSeconds = 0.050, fastFraction = 0.75;
    static constexpr double releaseSlowMin = 0.3, releaseSlowSpan = 2.0;
    static constexpr double maxReductionDb = 40.0;
    static constexpr double referencePeak = 0.05; // side-chain level (full-scale peak) at which the cell reaches Rs = Rc when the amplifier gain is 1

private:
    void buildStage();

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* peakReduction = nullptr;
    juce::AudioParameterFloat* gain = nullptr;
    juce::AudioParameterFloat* mode = nullptr;

    static constexpr int stageSize = 4096;
    static constexpr double stageRange = 4.0; // table covers x in [-4, 4]
    std::array<float, stageSize + 1> stageTable {};

    double sampleRate = 0.0;
    double envelope = 0.0;          // fast peak follower of the output (the rectifier in front of the EL panel)
    double grFast = 0.0, grSlow = 0.0, grDb = 0.0;
    double memory = 0.0;            // 0..1: how long / how deep the cell has been lit
};

} // namespace openguitarmultifx
