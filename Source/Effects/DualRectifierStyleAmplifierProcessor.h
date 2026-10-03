#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Mesa/Boogie Dual Rectifier-style guitar amplifier, modelled at component level on NodalCircuit from
    the factory Mesa Boogie schematic (2-channel, dated 6-93 GEO. M.; docs/circuits/DualRectifier.md -- read
    that file to understand this processor). The Dual Rectifier is one of the defining high-gain amplifiers of
    the 1990s--2000s metal and hard rock era, known for its massive low-end, saturated gain and chunky rhythm tone.

    The RED channel only is modelled (the high-gain channel), as the amp's defining voice. The ORANGE channel,
    LDR switching circuitry, tube/diode rectifier select, and FX loop are not implemented. Solid-state
    rectification only is modelled.

    Architecture: FIVE cascaded 12AX7 gain stages (V1A -> V2A -> V2B -> V3A -> V3D follower), a Fender-derived
    TMB tone stack, a 12AX7 long-tailed-pair phase inverter with global negative feedback, and four 6L6GC
    beam tetrodes as two push-pull pairs in a fixed-bias output stage.

    Controls, page 1: Gain (RED channel drive, 1MA), Treble, Bass, Mid, Master, Presence, Output. Page 2:
    Power Drive (PI drive), Bias, Tube Feel, Speaker (4 / 8 / 16 ohm).
*/
class DualRectifierStyleAmplifierProcessor : public EffectProcessor
{
public:
    DualRectifierStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Dual Rectifier-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff1a1a2e); } // deep navy (Mesa chrome/dark)
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** 100 W into 16 ohm is ~57 V peak. */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    // Twin Reverb's fitted 6L6GC constants (same tube type and push-pull topology).
    static constexpr double bmGain0 = 9.5;
    static constexpr double bmYmax = 0.198;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.55;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v2aPlate, v2bPlate, v3aPlate, followerOut, toneStackOut,
                       phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB, phaseInverterTail,
                       powerPlateA, powerPlateB, powerGridA, speaker, biasNode, feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPi() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railV3() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV2() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
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

        // preamp: four cascaded 12AX7 gain stages + cathode follower
        int pSrcV2 = 0, pSrcV3 = 0, pSrcE = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node pPlateV1a = 0, pPlateV2a = 0, pPlateV2b = 0, pPlateV3a = 0, pFollower = 0;
        double followerDc = 0.0;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0,
            rPresTop = 0, rPresBottom = 0, rBiasTrim = 0, rMaster = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0,
                           wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 460.0;

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
        double gain, treble, mid, bass, presence, master, powerDrive, bias, tubeFeel;
        int speaker;
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
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedMaster, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
