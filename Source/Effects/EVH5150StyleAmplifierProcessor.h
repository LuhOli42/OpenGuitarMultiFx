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
    Peavey EVH 5150 schematic (docs/circuits/EVH5150.md). The 5150 is one of the defining high-gain
    amplifiers, known for its aggressive, tight distortion and mid-forward voicing that became the
    standard for modern metal rhythm tones.

    Both the LEAD (Ultra) and RHYTHM channels are modelled. The Lead channel uses all five preamp gain
    stages (V1A -> V1B -> V2A -> V2B -> V5B); the Rhythm channel bypasses V2A and V2B via switchable
    series resistors, going from V1B directly to V5B for a tighter, less-saturated crunch tone.

    Architecture: up to FIVE cascaded 12AX7 gain stages (V1A -> V1B -> [V2A -> V2B] -> V5B) plus a V5A
    cathode follower, tone stack, a post-tone-stack gain recovery stage (V3B), a 12AX7 long-tailed-pair
    phase inverter (V4A/V4B) with global negative feedback, and four 6L6GC beam tetrodes as two push-pull
    pairs in a fixed-bias output stage.

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
    const char* getName() const override { return "5150-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 9.5;
    static constexpr double bmYmax = 0.198;
    static constexpr double bmKneeN = 6.0;
    // Fitted to THIS amp's own full-order lock-in sweep (toneStackOut -> speaker): LF bump ~+5 dB at ~80-90 Hz
    // falling ~15 dB/oct below (resonant high-pass + one-pole low cut at bmCutHz), flat mids, +2.5 dB ridge at
    // ~4-5 kHz into the top rolloff (-13 dB @ 10.8 kHz). The V3B recovery triode the reduced path removes is
    // absorbed into the knee/filter fit; the reference's presence/resonance pots sit in the dead NFB loop
    // (kg1 collapse) and measure ~0.1 dB at every setting, so there is no presence feed here.
    static constexpr double bmBumpHz = 80.0, bmBumpQp = 2.0;
    static constexpr double bmCutHz = 15.0;
    static constexpr double bmTopZHz = 4000.0, bmTopPHz = 6500.0, bmTopQp = 1.4;
    // the OT/NFB path has a real notch at ~12 kHz in this amp's own sweep -- a third (notch) biquad reproduces it.
    static constexpr double bmNotchHz = 12000.0, bmNotchQ = 1.0;
    // physical ceiling: same rail-clip rationale as the other fitted amps -- keeps the resonant overshoot
    // of a saturated LF signal inside the sanity bound (also the unity-at-noon trim point).
    static constexpr double bmOutMax = 115.0;
    static constexpr double bmLevelTrim = 0.91;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, v2bPlate, v5bPlate, followerOut, toneStackOut,
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
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: five cascaded 12AX7 gain stages + cathode follower
        int pSrcV2 = 0, pSrcV1 = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0, rV2aSeries = 0, rBypassV2ab = 0;
        NodalCircuit::Node pPlateV1a = 0, pPlateV1b = 0, pPlateV2a = 0, pPlateV2b = 0,
                           pPlateV5b = 0, pFollower = 0;
        double followerDc = 0.0;

        // power section (tone stack + gain recovery + PI + power amp)
        int wSrcCf = 0, wSrcRecovery = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0,
            rPresTop = 0, rPresBottom = 0, rBiasTrim = 0, rPostTop = 0, rPostBottom = 0, rResonancePot = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wTone = 0, wRecoveryPlate = 0, wOut = 0, wPlateA = 0,
                           wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0,
                           wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 465.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        double bmAX1 = 0.0, bmAX2 = 0.0, bmAY1 = 0.0, bmAY2 = 0.0;
        double bmBX1 = 0.0, bmBX2 = 0.0, bmBY1 = 0.0, bmBY2 = 0.0;
        double bmCX1 = 0.0, bmCX2 = 0.0, bmCY1 = 0.0, bmCY2 = 0.0;
        double bmCutState = 0.0;
        tubeamp::CouplingCapHighpass piCoupling; // the PI's input cap, which reducedOrder otherwise skips

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
    void designPowerFilters();
    // power-stage biquads (bilinear-transformed in designPowerFilters())
    double bmAB0 = 1.0, bmAB1 = 0.0, bmAB2 = 0.0, bmAA1 = 0.0, bmAA2 = 0.0;
    double bmBB0 = 1.0, bmBB1 = 0.0, bmBB2 = 0.0, bmBA1 = 0.0, bmBA2 = 0.0;
    double bmCB0 = 1.0, bmCB1 = 0.0, bmCB2 = 0.0, bmCA1 = 0.0, bmCA2 = 0.0;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

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
