#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Orange Rockerverb 50 MK1-style guitar amplifier (Dirty channel only), modelled at component level
    on NodalCircuit from the factory schematic ORA-CD204 (27 Feb 2004). The Rockerverb is Orange's flagship
    high-gain amplifier, known for its thick, harmonically-rich distortion and distinctive mid-range character.

    Architecture: FOUR cascaded 12AX7 dirty-channel gain stages (V9-A -> V9-B -> V8-A -> V8-B), FMV tone stack,
    a 12AX7 long-tailed-pair phase inverter (V5-A/V5-B) with global negative feedback, and four 6V6 beam tetrodes
    as two push-pull pairs in a fixed-bias output stage.

    Controls, page 1: Gain (1MA), Treble, Mid, Bass, Presence, Master, Output.
    Page 2: Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm).
*/
class RockerverbStyleAmplifierProcessor : public EffectProcessor
{
public:
    RockerverbStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

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
    enum class Probe { v9aPlate, v9bPlate, v8aPlate, v8bPlate, toneStackOut,
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
    double railPi() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
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

        // preamp: four cascaded 12AX7 gain stages
        int pSrcE = 0, pSrcD = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node pPlateV9a = 0, pPlateV9b = 0, pPlateV8a = 0, pPlateV8b = 0;
        double plateDcV8b = 0.0;

        // power section (tone stack + PI + power amp)
        int wSrcTs = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0,
            rPresTop = 0, rPresBottom = 0, rBiasTrim = 0, rPost = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wTone = 0, wOut = 0, wPlateA = 0,
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
        double gain, treble, mid, bass, presence, post, powerDrive, bias, tubeFeel;
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
    juce::AudioParameterFloat* postParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedPost, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
