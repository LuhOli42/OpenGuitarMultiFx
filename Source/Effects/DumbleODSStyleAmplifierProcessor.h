#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Dumble Overdrive Special-style guitar amplifier, modelled at component level on NodalCircuit
    (docs/circuits/DumbleODS.md -- read that file to understand this processor). The circuit follows the
    public 1970s ODS trace: V1A into the distributed tone stack, Volume, V1B with its 22M plate-to-grid feedback,
    then the Normal / Overdrive relay. Overdrive routes the signal through two more 12AX7 stages (the input trim,
    the Overdrive pot between them, the Ratio pot after them). After the relay come the Master, a 12AX7
    long-tailed pair with the NFB at the bottom of its tail, two 6L6GC in fixed bias and the output transformer.

    The switches are the ones the later ODS (#124) carries: Bright, Deep, Mid Boost (the treble cap is 2.2nF in
    series with 390pF; the switch shorts the 390pF), and Jazz / PAB / Rock (Rock ties the treble pot's bottom to the
    bass wiper; Jazz puts it on .005uF and merges the bass wiper through 100K; PAB lifts the bass wiper through 44M).

    Controls, page 1 (the front panel): Channel (Normal / Overdrive), Volume, Bright, Deep, Mid Boost, Jazz / PAB /
    Rock, Treble, Middle, Bass, Overdrive, Ratio, Master, Accent. Page 2: OD Trim (the internal input trim), Power
    Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output, Cab.
*/
class DumbleODSStyleAmplifierProcessor : public EffectProcessor
{
public:
    DumbleODSStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    /** Overdrive, Ratio and the OD trim only act on the Overdrive channel: hidden on Normal. */
    bool isParameterActive (const juce::AudioParameterFloat& p) const override
    {
        return (&p != smoothedParams[sOverdrive] && &p != smoothedParams[sRatio] && &p != smoothedParams[sOdTrim])
            || channelParam->get() >= 0.5f;
    }
    const char* getName() const override { return "Dumble ODS"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2b2b2b); } // black tolex
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Two 6L6GC at ~460 V make ~50 W into 8 ohm, ~28 V peak. */
    static constexpr double outputScale = 1.0 / 28.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 9.5;
    static constexpr double bmYmax = 0.09;
    static constexpr double bmKneeN = 4.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.55;
    static constexpr double bmLevelTrim = 1.06;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, v2bPlate, preampOut, phaseInverterGrid, phaseInverterPlateA,
                       phaseInverterPlateB, phaseInverterTail, powerPlateA, powerPlateB, powerGridA, speaker, biasNode,
                       feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPi() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railOverdrive() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV1() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
    int debugRecoveries() const noexcept { return recoveries; }
    void debugSetResistiveLoad (double ohms);
    void debugSetFeedbackResistance (double ohms);
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

        // preamp: V1A, stack, V1B, the relay, V2A / V2B, Master
        int pSrcE = 0, pSrcD = 0, pSrcIn = 0;
        int rVolTop = 0, rVolBot = 0, rTrebleTop = 0, rTrebleBottom = 0, rBassTop = 0, rBassBottom = 0, rMid = 0;
        int capBright = 0, rMidBoost = 0, rRock = 0, rJazzCap = 0, rJazzMerge = 0, rDeepA = 0, rDeepB = 0;
        int rNormal = 0, rOdIn = 0, rOdOut = 0;
        int rTrimTop = 0, rTrimBot = 0, rOdTop = 0, rOdBot = 0, rRatioTop = 0, rRatioBot = 0, rMasterTop = 0, rMasterBot = 0;
        NodalCircuit::Node pPlateV1a = 0, pPlateV1b = 0, pPlateV2a = 0, pPlateV2b = 0, pRelay = 0, pMasterWiper = 0, pPiGrid = 0;

        // power section (full reference only)
        int wSrcIn = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rBiasTrim = 0, capAccent = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 454.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
        tubeamp::PowerStageTail bmTail;

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, iE = 0, srcVoc = 0, rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0, sE = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    struct Knobs
    {
        double volume = 0.5, treble = 0.5, mid = 0.5, bass = 0.5, overdrive = 0.5, ratio = 0.5, master = 0.5,
               odTrim = 0.5, powerDrive = 1.0, bias = 0.5, tubeFeel = 1.0;
        int speaker = 1;
        bool overdriveOn = false, bright = false, deep = false, midBoost = false, accent = false;
        int voicing = 2; // 0 Jazz, 1 PAB, 2 Rock
    };

    void buildChannel (Channel& ch);
    void updatePots (const Knobs& k);
    Knobs knobsFromSmoothed (const float* v, int speaker) const;
    Knobs knobsFromParams() const;
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double piGrid) const noexcept;

    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    double feedbackOverride = 0.0;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    // smoothed continuous controls, in this order
    enum Smoothed { sVolume, sTreble, sMid, sBass, sOverdrive, sRatio, sMaster, sOdTrim, sPower, sBias, sFeel, sOutput, numSmoothed };
    std::array<juce::AudioParameterFloat*, numSmoothed> smoothedParams {};
    std::array<juce::SmoothedValue<float>, numSmoothed> smoothed;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* deepParam = nullptr;
    juce::AudioParameterFloat* midBoostParam = nullptr;
    juce::AudioParameterFloat* voicingParam = nullptr;
    juce::AudioParameterFloat* accentParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* cabParam = nullptr;

    /** reset() must return the circuit to its DC operating point; prepare() no-ops on a same-rate re-prepare to
        protect live state during a chain reorder, so reset() re-arms the rate and forces the rebuild. */
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
