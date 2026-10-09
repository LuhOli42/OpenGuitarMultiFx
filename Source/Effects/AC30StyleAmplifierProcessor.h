#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Vox AC30-style guitar amplifier (the classic "Top Boost" 4xEL84 head/combo, Top Boost channel only --
    docs/circuits/AC30TopBoost.md -- read that file to understand this processor). Shares the AC15's own family
    traits (cathode-biased EL84 output tubes, no global negative feedback -- confirmed absent on this circuit's own
    factory power-amp drawing) but scales up: FOUR EL84s as two parallel pairs (not two single tubes), a genuine
    long-tailed-pair phase inverter (not a cathodyne -- a single triode couldn't drive four grids symmetrically), and
    a newly-built two-gain-stage-plus-cathode-follower preamp feeding the famous "Top Boost" Treble/Bass tone stack
    (a bridged RC network, not a simple TMB ladder). The Normal channel (no Top Boost tone stack, mixed in on the
    real amp) is not modelled, matching this project's single-channel scoping for the JTM45/JCM800/AC15.

    Controls, page 1: Input (High / Low sensitivity jack), Volume, Treble, Bass (the real amp's own Top Boost
    controls), Cut (treble roll-off, the real amp's own "Cut" control in the NFB loop, modelled as a one-pole LPF on
    the output), Page 2 (synthetic, plus Output -- a plug-in level control; the real amp has no master volume): Power Drive (a synthetic
    master ahead of the phase inverter, matching this project's convention), Bias (a synthetic shift of the shared
    cathode-bias resistor -- the real amp has no adjustable bias trim, being self-biased), Tube Feel, Speaker
    (4 / 8 / 16 ohm, matching the real amp's own transformer taps exactly).
*/
class AC30StyleAmplifierProcessor : public EffectProcessor
{
public:
    AC30StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "AC30-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 45.0;

    // ---- reduced-order power stage, built in from the start ----
    static inline bool reducedOrder = false;
    // Fitted (2026-10-05) to the full reference after its plate resistors were rewired in series with the OT (they
    // were a shunt across each half-primary that killed ~35 dB of output; the old constants were fitted to that).
    static constexpr double bmGain0 = 17.3;
    static constexpr double bmYmax = 0.0583;
    static constexpr double bmKneeN = 3.0;
    static constexpr double bmShelfHz = 120.0;
    static constexpr double bmShelfHfGain = 0.7;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { preampPlate, followerOut, toneOut, piGridA, piPlateA, piPlateB, powerGridA, powerPlateA, powerPlateB,
                       speaker, cathodeBias };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
    void debugSetResistiveLoad (double ohms);
    void debugSetCathodeResistance (double ohms);
    void debugFreezeSupplyCurrent (bool freeze) { supplyCurrentFrozen = freeze; }
    double plateCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: V1a (input) -> Volume -> V2a (gain) -> V2b (cathode follower, direct-coupled from V2a's plate)
        int pSrcIn = 0, pSrcRail = 0;
        int rVolTop = 0, rVolBot = 0;
        NodalCircuit::Node pPlate = 0, pVolOut = 0, pFollower = 0;
        double followerDc = 0.0;

        // power section: Top Boost tone stack -> LTP -> 2x2 EL84 pairs, cathode-biased, no NFB
        int wSrcCf = 0, wSrcRail = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rCathodeBias = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rBassBottom = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wTone = 0, wOut = 0, wPiGridA = 0, wPiPlateA = 0, wPiPlateB = 0,
                           wGridA = 0, wGridB = 0, wPP1 = 0, wPP2 = 0, wCathodeBias = 0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        int iA = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double preRail = 0.0; // the decoupled 290 V preamp tap, after its RC filter
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, treble, bass, cut, powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    double cathodeResistanceOverride = 0.0;
    bool supplyCurrentFrozen = false;
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* cutParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedBass, smoothedCut, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;
    double cutFilterState = 0.0;

        /** JUCE's reset() must return the circuit to its DC operating point -- an
        empty reset() (the bug this fixes) left stale capacitor state forever.
        prepare() deliberately no-ops on a same-rate re-prepare to protect live
        state during the UI's chain-reorder, so reset() re-arms the rate and
        forces the rebuild. Control thread only. */
    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0, failuresPre = 0, failuresPower = 0;
    bool dcOk = false;
    bool lastSampleOk = true;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
