#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "SpringReverbProcessor.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Fender Super-Sonic 22-style guitar amplifier, modelled at component level on NodalCircuit from Fender's factory
    service diagram (0078327000 rev A, 08-JAN-10; docs/circuits/SuperSonic22.md -- read that file to understand this
    processor).

    Two channels on relays. VINTAGE: V1A -> a blackface Treble / Bass stack (fixed 6.8K mid) -> Volume -> V1B -> a 3.3M /
    270K attenuator (Normal) or V2B wrapped in 470K shunt feedback (Fat) -> V2B -> a 100K / 15K divider. BURN: V1A ->
    .0022 -> Gain 1 -> V1B -> Gain 2 -> V2A -> V2B (both cathodes fully bypassed on Burn) -> a Treble / Bass / Middle stack
    -> 220K -> Volume. Both feed the reverb mixer: the dry signal through 3.3M, the spring's return through the Reverb pot
    and 1M, into V4B, which drives a 12AT7 long-tailed-pair phase inverter and two 6V6GTs on fixed bias.

    Solver blocks: A (V1A, both input networks, V1B), B (V2A, V2B, the voicing networks, both stacks), C (the mixer and
    V4B), then the power block. Each upstream block carries a passive copy of the next one's input as its load. The
    spring tank is the project's SpringReverbProcessor, run one block behind (a real tank's delay is longer).

    Controls, page 1 (the real panel): Channel (Vintage / Burn), Fat (Normal / Fat), Vintage Volume / Treble / Bass,
    Burn Gain 1 / Gain 2 / Treble / Bass / Middle / Volume, Reverb. Page 2 (synthetic): Power Drive, Bias, Tube Feel,
    Speaker (4 / 8 / 16 ohm), Output, Cab. The effects loop is not modelled: with nothing plugged in it is bypassed by
    relay K5 and its op-amps are out of the signal path.
*/
class SuperSonicStyleAmplifierProcessor : public EffectProcessor
{
public:
    SuperSonicStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    /** Each channel's own knobs show only while that channel is selected. */
    bool isParameterActive (const juce::AudioParameterFloat& p) const override
    {
        const bool burn = channelParam->get() >= 0.5f;
        if (&p == vVolumeParam || &p == vTrebleParam || &p == vBassParam)
            return ! burn;
        if (&p == gain1Param || &p == gain2Param || &p == bTrebleParam || &p == bBassParam || &p == bMiddleParam || &p == bVolumeParam)
            return burn;
        return true;
    }
    const char* getName() const override { return "Super-Sonic 22"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d6fb4); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;
    bool hasTrails() const override { return true; }

    /** ~22 W into 8 ohm is ~13.3 V rms / 18.8 V peak at the speaker. */
    static constexpr double outputScale = 1.0 / 21.0;

    // ---- reduced-order power stage (the shared behavioural stage; docs/circuits/SuperSonic22.md) ----
    static inline bool reducedOrder = false;
    // Fitted against this amp's own full netlist (AmpFullVsReducedConsistency at rms 0.03 / 0.40): the knee stands for
    // the 12AT7 LTP, two 6V6GTs, the OT and the 820 / 47 feedback, driven from C68 into the PI's 1M.
    static constexpr double bmGain0 = 10.0;
    static constexpr double bmYmax = 0.08;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.65;
    static constexpr double bmAsym = 0.080;
    static constexpr double bmDcHz = 8.0;
    static constexpr double bmLevelTrim = 1.1;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, v2bPlate, channelOut, mixNode, v4bPlate, powerAmpIn, phaseInverterGrid,
                       phaseInverterPlateA, phaseInverterPlateB, phaseInverterTail, powerPlateA, powerPlateB, speaker,
                       feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept { return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0; }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPhaseInverter() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railV2() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV1() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
    double debugIterations (int block) const noexcept;
    int debugRecoveries() const noexcept { return recoveries; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    void debugSetFeedbackResistance (double ohms);
    double plateCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit preA, preB, preC, power, supply;
        NodalCircuit::DynamicState restA, restB, restC, powerRest, supplyRest;
        int failStreak = 0, restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // block A
        int aSrcIn = 0, aSrcW = 0;
        int rVtTop = 0, rVtBot = 0, rVBass = 0, rVvTop = 0, rVvBot = 0, rBrightSw = 0, rFatC3 = 0;
        int rBurnIn = 0, rBurnInGnd = 0, rG1Top = 0, rG1Bot = 0, rSelVintage = 0, rSelBurn = 0, rV1bBypass = 0;
        int aLdVintage = 0, aLdBurn = 0, aLdFat = 0, aLdG2Top = 0, aLdG2Bot = 0;
        NodalCircuit::Node aPlateV1a = 0, aPlateV1b = 0;
        // block B
        int bSrcIn = 0, bSrcW = 0, bSrcX = 0;
        int rK6Vintage = 0, rK6Burn = 0, rK4Normal = 0, rK4Fat = 0, rK4bNormal = 0, rK4bFat = 0, rK7Burn = 0;
        int rG2Top = 0, rG2Bot = 0, rV2bBypass = 0, rK2aVintage = 0, rK2aBurn = 0, rK2bVintage = 0, rK2bBurn = 0;
        int rBtTop = 0, rBtBot = 0, rBBass = 0, rBMid = 0, rBvTop = 0, rBvBot = 0;
        NodalCircuit::Node bPlateV2a = 0, bPlateV2b = 0, bOut = 0;
        // block C
        int cSrcOut = 0, cSrcReverb = 0, cSrcW = 0, rRevTop = 0, rRevBot = 0;
        NodalCircuit::Node cMix = 0, cPlateV4b = 0, cPampIn = 0;
        // power
        int wSrcIn = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rDriveTop = 0, rDriveBot = 0, rFeedback = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wGridA = 0, wPlateA = 0, wPlateB = 0, wTail = 0, wFeedback = 0, wPP1 = 0, wPP2 = 0, wOut = 0;
        double screenDropA = 0.0, screenDropB = 0.0, vScreen = 418.0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0, bmDcState = 0.0;
        tubeamp::PowerStageTail bmTail;
        tubeamp::CouplingCapHighpass piCoupling; // C68 .01 into the PI's 1M, which reducedOrder otherwise skips
        double sendHp = 0.0;                     // C92 470p into R161 1M: the reverb send's high-pass

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, iE = 0, srcVoc = 0, rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0, sE = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0, supplyCounter = 0;
    };

    struct Knobs
    {
        double vVolume, vTreble, vBass, gain1, gain2, bTreble, bBass, bMiddle, bVolume, reverb, powerDrive, bias, tubeFeel;
        int speaker;
    };

    void buildChannel (Channel& ch);
    void buildPreA (Channel& ch) const;
    void buildPreB (Channel& ch) const;
    void buildPreC (Channel& ch) const;
    void applySwitches (Channel& ch, bool burn, bool fat) const;
    void updatePots (const Knobs& k);
    void preparePreamp (Channel& ch, double rate, bool& ok) const;
    bool solvePreamp (Channel& ch, double x, double reverbReturn) const;
    void updateSupply (Channel& ch) const;
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    double behavioralPowerStage (Channel& ch, double drive) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    // The spring tank, shared by both channels (stereo), fed this block's sends and read one block later.
    SpringReverbProcessor spring;
    juce::AudioBuffer<float> reverbSend, reverbReturn;
    int reverbReturnLength = 0;
    double sendCoeff = 0.0;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *channelParam = nullptr, *fatParam = nullptr, *vVolumeParam = nullptr, *vTrebleParam = nullptr,
                              *vBassParam = nullptr, *gain1Param = nullptr, *gain2Param = nullptr, *bTrebleParam = nullptr,
                              *bBassParam = nullptr, *bMiddleParam = nullptr, *bVolumeParam = nullptr, *reverbParam = nullptr,
                              *powerParam = nullptr, *biasParam = nullptr, *tubeFeelParam = nullptr, *speakerParam = nullptr,
                              *outputParam = nullptr, *cabParam = nullptr;
    juce::SmoothedValue<float> smoothed[13];

    Knobs lastKnobs {};
    int appliedSpeaker = -1, appliedChannel = -1, appliedFat = -1;
    double speakerGain = 1.0, driveFraction = 1.0, idleSupplyCurrent = 0.0, feedbackOverride = 0.0;
    mutable int recoveries = 0;
    double worstSaneVolts = 0.0;

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, maxBlock, 2);
    }

    double sampleRate = 0.0;
    int maxBlock = 512;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
