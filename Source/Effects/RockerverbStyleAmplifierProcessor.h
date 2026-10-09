#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Orange Rockerverb 50 MK1-style guitar amplifier (the early 4x6V6 version), modelled at component level on
    NodalCircuit from the factory schematics ORA-CD204 / ORA-CD206 (Feb/Mar 2004) -- docs/circuits/Rockerverb.md.

    BOTH channels, as drawn: Dirty = V9-A -> (R53/R60, C42, bright C36) -> Gain RV4-B -> V9-B -> (R54/R61) -> Gain RV4-A
    (the Gain pot is a dual gang) -> V8-A -> (R51/R52) -> V8-B -> its own Treble/Middle/Bass stack -> Volume RV8.
    Clean = V10-A -> (R56/R55, bright C34) -> Volume RV1 -> V10-B -> its own Treble/Bass stack (no Middle pot: a fixed
    6k8). Relay RL1 picks one; after it the path is shared: R4 -> cathode follower V7-A (12AT7) -> loop -> V7-B
    (12AT7 recovery) -> the reverb mixer (reverb not modelled, its pot at minimum as a load) -> C3 -> phase inverter.

    Controls, page 1 -- the real front panel: Channel, Clean Volume / Treble / Bass, Dirty Gain / Treble / Middle /
    Bass / Volume. (No Presence and no master on the real amp.) Page 2 (synthetic): Power Drive (a master ahead of
    the power stage; 1.0 = the real amp), Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output.
*/
class RockerverbStyleAmplifierProcessor : public EffectProcessor
{
public:
    RockerverbStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Rockerverb-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffcc5500); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 8.5;
    static constexpr double bmYmax = 0.20;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 80.0;
    static constexpr double bmShelfHfGain = 0.55;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v9aPlate, v9bPlate, v8aPlate, v8bPlate, v10aPlate, v10bPlate, v7aCathode, v7bPlate, toneStackOut,
                       phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB,
                       phaseInverterTail, powerPlateA, powerPlateB, powerGridA, speaker, biasNode,
                       feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    // Schematic rails: C (PI) = sB, D (V7) = sC, E (V8) = sD, F (V9/V10) = sE.
    double railPi() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railV7() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railV8() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV9() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
    double debugIterations (int block) const noexcept;
    int debugLastPowerIterations() const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    long long debugSanityRejects() const noexcept { return sanityRejects; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    int debugRecoveries() const noexcept { return recoveries; }
    bool debugLastSampleOk() const noexcept { return lastSampleOk; }
    void debugSetResistiveLoad (double ohms);
    void debugSetFeedbackResistance (double ohms);
    void debugFreezeSupplyCurrent (bool freeze) { supplyCurrentFrozen = freeze; }
    double plateCurrentTotal() const noexcept;
    double screenCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp block: both channels, both tone stacks (rails F = V9/V10, E = V8)
        int pSrcF = 0, pSrcE = 0, pSrcIn = 0;
        int rGainBTop = 0, rGainBBot = 0, rGainATop = 0, rGainABot = 0;               // RV4-B / RV4-A
        int rDTrebleTop = 0, rDTrebleBot = 0, rDBass = 0, rDMidTop = 0, rDMidBot = 0, rDVolTop = 0, rDVolBot = 0;
        int rCVolTop = 0, rCVolBot = 0, rCTrebleTop = 0, rCTrebleBot = 0, rCBassTop = 0, rCBassBot = 0;
        int rLoadDirty = 0, rLoadClean = 0;                                             // what RL1 connects to
        NodalCircuit::Node pPlateV9a = 0, pPlateV9b = 0, pPlateV8a = 0, pPlateV8b = 0, pPlateV10a = 0, pPlateV10b = 0;
        NodalCircuit::Node pDirtyOut = 0, pCleanOut = 0;

        // post block (always): R4 -> V7-A follower -> loop -> V7-B -> reverb mixer -> C3; then PI + power (full only)
        int wSrcPost = 0, wSrcD = 0, wSrcV7Bias = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wPlateV7a = 0, wCathodeV7a = 0, wPlateV7b = 0, wMix = 0, wTone = 0, wOut = 0, wPlateA = 0,
                           wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 410.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, iE = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0, sE = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double cVolume, cTreble, cBass, gain, treble, mid, bass, post, powerDrive, bias, tubeFeel;
        int speaker, channel;
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    long long sanityRejects = 0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    double feedbackOverride = 0.0;
    bool supplyCurrentFrozen = false;
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* cVolumeParam = nullptr;
    juce::AudioParameterFloat* cTrebleParam = nullptr;
    juce::AudioParameterFloat* cBassParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* postParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedCVolume, smoothedCTreble, smoothedCBass, smoothedGain, smoothedTreble, smoothedMid,
        smoothedBass, smoothedPost, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
