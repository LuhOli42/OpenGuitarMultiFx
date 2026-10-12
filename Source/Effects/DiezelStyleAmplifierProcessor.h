#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"
#include "ChannelKnobMemory.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Diezel VH4-style guitar amplifier (the 1994 100 W four-channel head whose channel settings are MIDI-stored),
    modelled at component level on NodalCircuit -- docs/circuits/Diezel.md, read that file to understand this
    processor.

    Three parts, the same structure as the Rivera Knucklehead-style amplifier:
      1. the preamp -- FOUR separate channel netlists (only the selected channel is solved per sample, like the
         real amp's relays): CH1 Clean (2 triodes, high headroom), CH2 Crunch (2 triodes + cold clipper,
         plexi-leaning), CH3 Mega (3 triodes, tighter couplings, mid-forward TMB) and CH4 Lead (CH3's path plus
         a fourth cold stage -- the VH4's channels get progressively hotter rather than sharing one gain knob);
      2. the long-tailed-pair phase inverter (82k5 / 100k plates, 680R + 8k8 tail) driving four KT77s (the EL34
         family the Diezels run) as two push-pull pairs on 150k grid leaks into FIXED bias, the ~1k8
         plate-to-plate output transformer -- WITH the global negative-feedback loop carrying the Presence and
         Deep controls (Deep is Diezel's LF-NFB trim, the same mechanism as Rivera's Focus);
      3. the solid-state bridge supply (~430 / 420 / 310 / 270 V nodes).

    The VH4's front panel carries a full Gain / TMB / Volume set per channel (that's what the MIDI memory holds):
    the model exposes one set of knobs whose per-channel values are kept by ChannelKnobMemory, exactly like the
    real front panel's recall.

    Controls, page 1: Channel (Clean / Crunch / Mega / Lead), Gain, Treble, Middle, Bass, Volume (all per-channel),
    Master (the global master ahead of the phase inverter), Presence, Deep.
    Page 2 (synthetic): Power Drive, Bias (the fixed-bias trimmer), Tube Feel (supply sag), Speaker
    (4 / 8 / 16 ohm on the 16 ohm tap), Output. The MIDI/footswitch system itself is not modelled.
*/
class DiezelStyleAmplifierProcessor : public EffectProcessor
{
public:
    DiezelStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Diezel"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a3a63); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;

    /** ~100 W into the 16 ohm tap is ~57 V peak at the speaker terminals. */
    static constexpr double outputScale = 1.0 / 57.0;
    static inline bool reducedOrder = false;
    static constexpr double fullOutputScale = outputScale;

    // Behavioural power-stage constants: borrowed from the Knucklehead fit (same 4-tube ~100 W class; rails
    // differ -- 430 V vs 460 V -- so the sag table is scaled in behavioralPowerStage). DZL_POWERCAL in the
    // test file is the gate for a real calibration.
    static constexpr double bmGain0 = 3.4569;
    static constexpr double bmYmax = 0.115;
    static constexpr double bmKneeN = 5.0;
    static constexpr double bmShelfHz = 80.0;
    static constexpr double bmShelfHfGain = 1.0;
    static constexpr double bmDeepHz = 100.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { chanPlate, chanOut,
                       piPlateA, piPlateB, piCathode, powerGridA, powerGridB, powerPlateA, powerPlateB, speaker,
                       biasNode, nfb };
    /** Probe on the debugChannel'th preamp netlist; stage = 1..4. */
    double debugVoltage (Probe p, int stage = 1) const noexcept;
    void debugSetChannel (int c) noexcept { debugChannel = juce::jlimit (0, 3, c); }
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
        NodalCircuit pre[4], power, supply;     // one netlist per VH4 channel
        NodalCircuit::DynamicState preRest[4], powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: four channel paths, each its own netlist (only the selected one is solved per sample)
        int pSrcIn[4] = {}, pSrcRail[4] = {};
        int rGainTop[4] = {}, rGainBot[4] = {};
        int rVolTop[4] = {}, rVolBot[4] = {};
        int rMasterTop[4] = {}, rMasterBot[4] = {}; // the VH4's one global master is inside every channel path
        int rTrebleTop[4] = {}, rTrebleBot[4] = {}, rBass[4] = {}, rMid[4] = {};
        NodalCircuit::Node pPlate[4][4] = {}, pK[4][4] = {}, pOut[4] = {};
        double outDc[4] = {};

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rPresence = 0, rDeep = 0;
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
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0, bmLowState = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)
        int appliedChannel = -1;
        double chanFade = 1.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double gain, treble, middle, bass, volume, master, presence, deep, powerDrive, bias, tubeFeel;
        int speaker;  // 0 = 4 ohm, 1 = 8, 2 = 16
        int channel;  // 0 = Clean, 1 = Crunch, 2 = Mega, 3 = Lead
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch, int channel) const noexcept;
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* deepParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    std::unique_ptr<ChannelKnobMemory> knobMemory;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMiddle, smoothedBass, smoothedVolume,
        smoothedMaster, smoothedPresence, smoothedDeep, smoothedPower, smoothedBias, smoothedFeel, smoothedOutput;

    static constexpr int controlInterval = 32;
    static constexpr int supplyInterval = 32;
    static constexpr int restRefreshInterval = 512;
    int controlCounter = 0;

    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }

    double sampleRate = 0.0;
    long long sampleCount = 0, failureCount = 0, failuresPre = 0, failuresPower = 0;
    bool lastSampleOk = false;
    bool dcOk = false;
    int debugChannel = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiezelStyleAmplifierProcessor)
};
}
