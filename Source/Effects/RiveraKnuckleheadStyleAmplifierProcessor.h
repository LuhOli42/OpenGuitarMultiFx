#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Rivera Knucklehead Reverb-style guitar amplifier (the 1993 100 W K100, Rivera's "two amps in one" 6L6 design),
    modelled at component level on NodalCircuit -- docs/circuits/RiveraKnucklehead.md, read that file to understand
    this processor.

    Three parts, the same structure as the Trainwreck Express-style amplifier:
      1. the preamp -- BOTH channels, in one netlist: CH1 clean (V1A -> Volume -> its own TMB stack -> V4A recovery ->
         Master), CH2 high-gain (V1B -> Gain -> its own TMB stack -> V2A -> V2B -> Master); the Channel control
         selects which path feeds the power section (the real amp's relays);
      2. the V5 long-tailed-pair phase inverter (82k5 / 100k plates, 680R + 8k8 tail) driving four 6L6GCs as two
         push-pull pairs on 150k grid leaks into FIXED bias, the ~1k8 plate-to-plate output transformer -- WITH the
         global negative-feedback loop the Knucklehead runs, carrying the Presence and Focus controls;
      3. the solid-state bridge supply (~460 / 445 / 318 / ~250 V nodes; the real amp's TP12/TP4/TP5 marks).

    Controls, page 1: Channel (Clean / Lead), Volume (CH1), Gain (CH2), Treble, Middle, Bass, Master, Presence, Focus.
    Page 2 (synthetic): Power Drive (a master ahead of the phase inverter), Bias (the fixed-bias trimmer), Tube Feel
    (supply sag), Speaker (4 / 8 / 16 ohm on the 16 ohm tap), Output. The spring reverb, Ninja boost and CH3 voicing
    are documented simplifications.
*/
class RiveraKnuckleheadStyleAmplifierProcessor : public EffectProcessor
{
public:
    RiveraKnuckleheadStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Rivera Knucklehead-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff274a63); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** ~100 W into the 16 ohm tap is ~57 V peak at the speaker terminals. */
    static constexpr double outputScale = 1.0 / 57.0;
    static inline bool reducedOrder = false;
    static constexpr double fullOutputScale = outputScale;

    // drive -> speaker small-signal gain at the nominal rail, output knee height as a fraction of the sagged
    // rail, knee sharpness, and the fitted first-order shelf standing in for the transformer + speaker response
    // (KNK_POWERCAL in the test file).
    static constexpr double bmGain0 = 3.4569;
    static constexpr double bmYmax = 0.115;
    static constexpr double bmKneeN = 5.0;
    static constexpr double bmShelfHz = 80.0;
    static constexpr double bmShelfHfGain = 1.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { cleanPlate1, cleanCath1, leadPlate1, leadCath1, leadPlate2, leadPlate3, cleanOut, leadOut,
                       piPlateA, piPlateB, piCathode, powerGridA, powerGridB, powerPlateA, powerPlateB, speaker,
                       biasNode, nfb };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
    void debugSetResistiveLoad (double ohms);
    double plateCurrentA() const noexcept;
    double plateCurrentB() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, preL, power, supply;      // pre = clean path, preL = lead path
        NodalCircuit::DynamicState preRest, preLRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: clean path + lead path in one netlist
        int pSrcIn = 0, pSrcRail = 0, pSrcInL = 0, pSrcRailL = 0;
        int rVolTop = 0, rVolBot = 0, rGainTop = 0, rGainBot = 0;
        int rMasterCleanTop = 0, rMasterCleanBot = 0, rMasterLeadTop = 0, rMasterLeadBot = 0;
        int rTrebleTopC = 0, rTrebleBotC = 0, rBassC = 0, rMidC = 0;
        int rTrebleTopL = 0, rTrebleBotL = 0, rBassL = 0, rMidL = 0;
        NodalCircuit::Node pPlateC1 = 0, pKC1 = 0, pPlateC4 = 0, pKC4 = 0, pOutClean = 0;
        NodalCircuit::Node pPlateL1 = 0, pKL1 = 0, pPlateL2 = 0, pKL2 = 0, pPlateL3 = 0, pKL3 = 0, pOutLead = 0;
        double outDcClean = 0.0, outDcLead = 0.0;

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rPresence = 0, rFocus = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wPiGrid = 0, wPiPlateA = 0, wPiPlateB = 0, wPiK = 0, wGridA = 0, wGridB = 0, wPP1 = 0, wPP2 = 0,
                           wBias = 0, wOut = 0, wNfb = 0;

        // supply
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;

        // reducedOrder only: behavioural power stage state (see behavioralPowerStage())
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, gain, treble, middle, bass, master, presence, focus, powerDrive, bias, tubeFeel;
        int speaker;  // 0 = 4 ohm, 1 = 8, 2 = 16
        int channel;  // 0 = clean, 1 = lead
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch, int channel) const noexcept;
    /** reducedOrder only: behavioural replacement for the LTP / 6L6 pairs / OT / speaker netlist; returns
        speaker volts. Constants fitted against the full netlist (KNK_POWERCAL in the test file). */
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* focusParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedGain, smoothedTreble, smoothedMiddle, smoothedBass, smoothedMaster,
        smoothedPresence, smoothedFocus, smoothedPower, smoothedBias, smoothedFeel, smoothedOutput;

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
