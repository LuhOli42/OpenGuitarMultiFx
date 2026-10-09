#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

class ENGLPowerballStyleAmplifierProcessor : public EffectProcessor
{
public:
    ENGLPowerballStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Powerball-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff1a1a2e); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 56.0;

    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 10.5;
    static constexpr double bmYmax = 0.195;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 85.0;
    static constexpr double bmShelfHfGain = 0.55;

    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { u5aPlate, u5bPlate, u6aPlate, u6bPlate, u7aPlate, u7bPlate,
                       toneStackOut, phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB,
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
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    int debugRecoveries() const noexcept { return recoveries; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }

private:
    struct Channel
    {
        NodalCircuit pre, tone, power, supply;
        NodalCircuit::DynamicState preRest, toneRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: three cascaded 12AX7 gain stages (U5A -> U5B -> U6A)
        int pSrcV1 = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node pPlateU5a = 0, pPlateU5b = 0, pPlateU6a = 0;
        double plateDcU6a = 0.0;

        // tone block: tone stack + 3 post-tonestack gain stages (U6B, U7A, U7B) + master
        int tSrcPre = 0, tSrcVcc = 0;
        int rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0, rMaster = 0;
        NodalCircuit::Node tToneIn = 0, tTone = 0, tPlateU6b = 0, tPlateU7a = 0, tPlateU7b = 0,
                           tMasterWiper = 0;
        double plateDcU7b = 0.0;

        // power section (PI + power amp + OT + speaker + NFB/Presence/Depth)
        int wSrcMaster = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rPresTop = 0, rPresBottom = 0, rDepthPot = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wOut = 0, wPlateA = 0, wPlateB = 0,
                           wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 460.0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
        double mwDcPrev = 0.0, mwDcOut = 0.0;
        double u7bDcPrev = 0.0, u7bDcOut = 0.0;
        double mwTarget = 0.0, mwServo = 0.0;

        int iA = 0, iB = 0, iC = 0, iD = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double gain, treble, mid, bass, presence, depth, master, powerDrive, bias, tubeFeel;
        int speaker;
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    mutable int recoveries = 0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
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
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedDepth, smoothedMaster, smoothedOutput, smoothedPower,
        smoothedBias, smoothedFeel;

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
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
