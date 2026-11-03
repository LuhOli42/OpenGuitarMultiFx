#pragma once

#include "ChannelKnobMemory.h"
#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    An ENGL Powerball (E645) style four-channel head, modelled at component level on NodalCircuit from the factory
    drawing 645-1 (docs/circuits/ENGLPowerball.md -- read that file to understand this processor).

    One preamp chain re-routed by the drawing's relays and LDRs: V5A -> the Ch I or Ch II Gain pot (LDRs LD1 / LD2;
    Bright and Bottom on Ch I) -> V5B -> Ch I: a divider straight to V6B / Ch II: a divider into V6A, then V6B ->
    the Ch I or the Ch II tone stack (relay RE2) -> that mode's own Volume -> V7A (the FX-send cathode stage) -> V7B
    (the FX return) -> Master A or B -> a 12AX7 long-tailed-pair PI with the Presence / Depth feedback network -> four
    6L6GC. The FX loop's jacks are not modelled: the dry path the unplugged jacks leave is.

    Controls, page 1 = the real panel: Channel (Clean / Crunch / Lo Lead / Hi Lead -- the Ch I/II and Gain Lo/Hi
    buttons together), Gain, Bass, Middle, Middle Focused, Treble, Volume (each with the real panel's per-channel
    sharing, ChannelKnobMemory.h), Style (Open / Focused), Bright, Bottom, Presence, Depth Punch, Master A, Master B,
    Master A/B. Page 2 (synthetic): Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output, Cab.
*/
class ENGLPowerballStyleAmplifierProcessor : public EffectProcessor
{
public:
    ENGLPowerballStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;
    /** Middle Focused belongs to Ch II, Bright to Ch I (the panel's own grouping). */
    bool isParameterActive (const juce::AudioParameterFloat& p) const override
    {
        const bool chTwo = channelParam != nullptr && channelParam->get() >= 1.5f;
        if (&p == midFocusedParam)
            return chTwo;
        if (&p == brightParam)
            return ! chTwo;
        return true;
    }
    const char* getName() const override { return "Powerball"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff1a1a2e); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 56.0;

    static inline bool reducedOrder = false;
    // Behavioural power stage (reducedOrder), fitted to this amp's full netlist (docs/circuits/ENGLPowerball.md).
    static constexpr double bmGain0 = 20.0;
    static constexpr double bmYmax = 0.17;
    static constexpr double bmKneeN = 4.0;
    static constexpr double bmBumpHz = 80.0, bmBumpQp = 2.0, bmBumpQz = 0.9;
    static constexpr double bmCutHz = 30.0;
    static constexpr double bmTopZHz = 3000.0, bmTopPHz = 6000.0, bmTopQp = 1.0;
    static constexpr double bmOutMax = 64.0;
    static constexpr double bmLevelTrim = 0.40;
    // Per-mode level match against Hi Lead at the default knobs (AmpSwitchLevelSurvey, E3 / DI midpoint,
    // docs/circuits/ChannelLevelMatch.md). Order: Clean, Crunch, Lo Lead, Hi Lead.
    static constexpr std::array<double, 4> modeLevelMatchDb { 4.7, 11.0, 0.0, 0.0 };

    bool dcConverged() const noexcept { return dcOk; }
    // u5a..u7b keep the old probe names: V5A..V7B on the drawing.
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
    double plateCurrentTotal() const noexcept;

private:
    // The drawing's relay contacts and the two gain LDRs, each a resistor in `pre` faded between on and off.
    enum Sw { ld1, ld2, bottomCh1, bright, re3aLo, re3aHi, re3bLo, re3bHi, re1aCh1, re1aCh2, re1bMute,
              re2bCh1, re2bCh2, re6aFocused, re6bOpen, re5aClean, re5aCrunch, re5bClean, re5bCrunch,
              re4aClean, re4aCrunch, re7aFocused, re8aOpen, re8aFocused, re8bOpen, re8bFocused, llb, hlb,
              re4bLo, re4bHi, re2aCh1, re2aCh2, re9aA, re9aB, numSw };

    struct Channel
    {
        NodalCircuit pre, tone, post, power, supply;   // V5A..V6B / stacks + Volumes (linear) / V7A, V7B, Masters / PI + power
        NodalCircuit::DynamicState preRest, toneRest, postRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        double chanTrim = 1.0;
        bool alignOutput = false;

        int pSrcVcc = 0, pSrcIn = 0, tSrcPlate = 0, qSrcVcc = 0, qSrcGrid = 0;
        double plateDcV6b = 0.0;
        NodalCircuit::Node tGrid7a = 0;
        std::array<int, numSw> sw {};
        int rGain1Top = 0, rGain1Bot = 0, rGain2Top = 0, rGain2Bot = 0;
        int rCleanTrebleTop = 0, rCleanTrebleBot = 0, rCrunchTrebleTop = 0, rCrunchTrebleBot = 0, rBass1 = 0, rMid1 = 0;
        int rCleanVolTop = 0, rCleanVolBot = 0, rCrunchVolTop = 0, rCrunchVolBot = 0;
        int rLeadTrebleTop = 0, rLeadTrebleBot = 0, rBass2 = 0, rMidOpen = 0, rMidFocused = 0;
        int rLoVolTop = 0, rLoVolBot = 0, rHiVolTop = 0, rHiVolBot = 0;
        int rMasterATop = 0, rMasterABot = 0, rMasterBTop = 0, rMasterBBot = 0;
        NodalCircuit::Node pPlateV5a = 0, pPlateV5b = 0, pPlateV6a = 0, pPlateV6b = 0, pPlateV7a = 0, pPlateV7b = 0,
                           pVolOut = 0, pMaster = 0;

        // power section (PI + power amp + OT + speaker + NFB / Presence / Depth)
        int wSrcMaster = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rPresTop = 0, rPresBot = 0, rDepth = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 460.0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        tubeamp::PowerStageTail bmTail;
        double bmAX1 = 0.0, bmAX2 = 0.0, bmAY1 = 0.0, bmAY2 = 0.0;
        double bmBX1 = 0.0, bmBX2 = 0.0, bmBY1 = 0.0, bmBY2 = 0.0;
        double bmCutState = 0.0, bmPresState = 0.0;
        tubeamp::CouplingCapHighpass piCoupling; // C19 into the PI grid, which reducedOrder otherwise skips

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
        double gain, treble, mid, midFocused, bass, volume, presence, depth, masterA, masterB, powerDrive, bias, tubeFeel;
        int speaker, channel;
    };
    void updatePots (const Knobs& k);
    void updateSwitches (bool snap);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    mutable int recoveries = 0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double drive) const noexcept;
    void designPowerFilters();
    double bmAB0 = 1.0, bmAB1 = 0.0, bmAB2 = 0.0, bmAA1 = 0.0, bmAA2 = 0.0;
    double bmBB0 = 1.0, bmBB1 = 0.0, bmBB2 = 0.0, bmBA1 = 0.0, bmBA2 = 0.0;
    std::array<double, numSw> swLog {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* midFocusedParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* styleParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* bottomParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* masterAParam = nullptr;
    juce::AudioParameterFloat* masterBParam = nullptr;
    juce::AudioParameterFloat* masterSelParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    // The real panel's sharing: Gain / Bass / Middle / Middle Focused per main channel, Treble Clean / Crunch / Lead,
    // a Volume per mode, Bottom for Ch I / Lo Lead / Hi Lead.
    std::unique_ptr<ChannelKnobMemory> mainMemory, trebleMemory, volumeMemory, bottomMemory;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedMidFocused, smoothedBass, smoothedVolume,
        smoothedPresence, smoothedDepth, smoothedMasterA, smoothedMasterB, smoothedOutput, smoothedPower, smoothedBias,
        smoothedFeel;

    /** JUCE's reset() must return the circuit to its DC operating point; prepare() no-ops on a same-rate re-prepare
        (chain reorder), so reset() re-arms the rate and forces the rebuild. Control thread only. */
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
