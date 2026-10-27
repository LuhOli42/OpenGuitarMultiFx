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
    A Peavey/EVH 5150-style guitar amplifier, modelled at component level on NodalCircuit from the factory
    Peavey EVH 5150 schematic (docs/circuits/EVH5150.md -- read that file to understand this processor).

    The signal path is the schematic's: V1A -> the ULTRA (Lead) or CLEAN (Rhythm) pre network -> V1B (470K grid
    feed) -> R6/R9 divider -> V2A (39K unbypassed cathode, the cold clipper) -> V2B -> R32 1M / R101 100K divider ->
    V5B (2.2K unbypassed) -> R87 1M -> V5A (a gain stage with 1M plate-to-grid feedback, NOT a cathode follower) ->
    R89 470K into the 33K / .01+47K loaded tone-stack input -> TMB stack -> Ultra/Clean Post -> V3A recovery ->
    long-tailed-pair PI (470 + 10K + 4.7K tail) with the Presence/Resonance NFB network -> 4x 6L6GC.

    The preamp is four NodalCircuit blocks cut at the big series resistors (R82 470K is inside block A; the cuts are
    at V1B's plate, V2B's plate and V5B's plate, each followed by >= 470K): each upstream block carries a passive copy
    of the next block's input network as its load, the downstream block is driven by the upstream plate.

    Controls, page 1 (the real panel's labels): Channel (Rhythm / Lead), Gain, Low, Mid, High, Volume -- each channel
    keeps its own set (ChannelKnobMemory.h), like the real amp's separate pots -- plus the shared Presence and
    Resonance. Page 2 (synthetic): Power Drive (PI drive), Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output.
*/
class EVH5150StyleAmplifierProcessor : public EffectProcessor
{
public:
    EVH5150StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return "5150"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 15.0;
    static constexpr double bmYmax = 0.198;
    // Softer than the shared fitted 6.0: at hot gain the knee's high-order harmonics flooded the
    // 8-20 kHz band past this amp's own full-netlist level (Oct-11 audit fizz regression).
    static constexpr double bmKneeN = 4.0;
    // Fitted against this amp's own full netlist (AmpFullVsReducedConsistency, rms 0.03 and 0.40): the knee and
    // filters stand for C49 -> PI -> 4x 6L6 -> OT with the Presence/Resonance NFB. Resonant high-pass LF bump at
    // ~80 Hz, a zero/pole top section (2 kHz / 6.5 kHz), a 12 kHz notch, a one-pole low cut at bmCutHz.
    static constexpr double bmBumpHz = 80.0, bmBumpQp = 1.6;
    static constexpr double bmCutHz = 15.0;
    static constexpr double bmTopZHz = 2000.0, bmTopPHz = 6500.0, bmTopQp = 1.4;
    static constexpr double bmNotchHz = 12000.0, bmNotchQ = 1.0;
    // physical ceiling: same rail-clip rationale as the other fitted amps -- keeps the resonant overshoot
    // of a saturated LF signal inside the sanity bound.
    static constexpr double bmOutMax = 115.0;
    static constexpr double bmLevelTrim = 0.53;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, v2bPlate, v5bPlate, v5aPlate, toneStackOut,
                       recoveryPlate, phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB,
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
    double railV2() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV1() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
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
        // preamp blocks A (V1A, pre network, V1B), B (V2A, V2B), C (V5B), `driver` (V5A), `stack` (the linear tone
        // stack, posts and loop); `power` holds V3A and -- full reference only -- the PI and power stage.
        NodalCircuit preA, preB, preC, driver, stack, power, supply;
        NodalCircuit::DynamicState restA, restB, restC, restD, restS, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        int aSrcIn = 0, aSrcV1 = 0;
        int rUltraTop = 0, rUltraBot = 0, rCleanTop = 0, rCleanBot = 0, rSelUltra = 0, rSelClean = 0;
        NodalCircuit::Node aPlateV1a = 0, aPlateV1b = 0;
        int bSrcIn = 0, bSrcV1 = 0, bSrcV2 = 0, rCrunchShunt = 0;
        NodalCircuit::Node bPlateV2a = 0, bPlateV2b = 0;
        int cSrcIn = 0, cSrcV2 = 0;
        NodalCircuit::Node cPlateV5b = 0;
        int dSrcIn = 0, dSrcV2 = 0, sSrcIn = 0;
        NodalCircuit::Node dPlateV5a = 0, dOut = 0, sOut = 0;

        int wSrcIn = 0, wSrcV2 = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0,
            rUltraPostTop = 0, rUltraPostBot = 0, rCleanPostTop = 0, rCleanPostBot = 0, rSelPostUltra = 0, rSelPostClean = 0,
            rDriveTop = 0, rDriveBot = 0, rPresence = 0, rResonance = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wStackIn = 0, wPost = 0, wPlateV3a = 0, wOut = 0, wPlateA = 0,
                           wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 465.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)
        double bmAX1 = 0.0, bmAX2 = 0.0, bmAY1 = 0.0, bmAY2 = 0.0;
        double bmBX1 = 0.0, bmBX2 = 0.0, bmBY1 = 0.0, bmBY2 = 0.0;
        double bmCX1 = 0.0, bmCX2 = 0.0, bmCY1 = 0.0, bmCY2 = 0.0;
        double bmCutState = 0.0;
        double bmPresState = 0.0;   // presence HF-shelf lowpass state (reducedOrder only)
        tubeamp::CouplingCapHighpass piCoupling; // C49 into the PI's 1M, which reducedOrder otherwise skips

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
        double gain, treble, mid, bass, presence, resonance, post, powerDrive, bias, tubeFeel;
        int speaker;
    };
    void buildPreA (Channel& ch) const;
    void buildPreB (Channel& ch) const;
    void buildPreC (Channel& ch) const;
    void applyChannel (Channel& ch, int channelChoice) const;
    void preparePreamp (Channel& ch, double rate, bool& ok) const;
    bool solvePreamp (Channel& ch, double x) const;
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    double driveFraction = 1.0; // Power Drive: the fraction of V3A's swing the PI sees (reducedOrder reads it directly)
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
    void designPowerFilters();
    // power-stage biquads (bilinear-transformed in designPowerFilters())
    double bmAB0 = 1.0, bmAB1 = 0.0, bmAB2 = 0.0, bmAA1 = 0.0, bmAA2 = 0.0;
    double bmBB0 = 1.0, bmBB1 = 0.0, bmBB2 = 0.0, bmBA1 = 0.0, bmBA2 = 0.0;
    double bmCB0 = 1.0, bmCB1 = 0.0, bmCB2 = 0.0, bmCA1 = 0.0, bmCA2 = 0.0;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* resonanceParam = nullptr;
    juce::AudioParameterFloat* postParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    std::unique_ptr<ChannelKnobMemory> channelMemory;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedResonance, smoothedPost, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
