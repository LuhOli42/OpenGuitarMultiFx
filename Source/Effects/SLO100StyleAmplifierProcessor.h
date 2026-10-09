#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Soldano SLO-100-style guitar amplifier (the "Super Lead Overdrive 100"), modelled at component level on
    NodalCircuit from Soldano's own factory drawing ("March 15, 1996", schematicheaven.net; docs/circuits/SLO100.md --
    read that file to understand this processor). This is the flagship high-gain amplifier that helped define the modern
    "hot-rodded" American high-gain sound, heard on countless rock and metal recordings from the early 1990s onward.

    Both channels are modelled: the CLEAN channel bypasses V2a/V2b/V3b (taps from the OD Volume pot wiper,
    giving a single-stage clean tone), and the OD (Overdrive) channel uses all five cascaded gain stages for the
    amp's defining high-gain voice. Channel switching is via a switchable series resistor on V2a's grid in the
    netlist. The FX Send/Return loop is not modelled (standing project rule: amp-no-effects-loop.md).

    Architecture: up to FIVE cascaded 12AX7 gain stages (V1b -> [V2a -> V2b -> V3b] -> V3a as a cathode follower),
    a Fender-style TMB tone stack (Treble 250K / Bass 1M / Mid 25K), a 12AX7 long-tailed-pair phase inverter with
    global negative feedback from the 4 ohm tap through a Presence pot, and four 6L6GC beam tetrodes as two
    push-pull pairs in a fixed-bias output stage -- structurally very similar to the Twin Reverb's own power section.

    Both channels as drawn (Rob Robinette's annotated factory schematic, 2026-10-08 rebuild): Normal = V1B -> Crunch/
    Clean attenuator (470K + 470K over 39K; Crunch drops one 470K and the 39K) with the Bright switch's 470 pF -> Normal
    Preamp 500KL -> V1A -> 2.2M || 120 pF -> V3B; Overdrive = V1B -> 470K || 2 nF -> OD Preamp 500KL (fixed 1 nF bright)
    -> V2A -> 470K / 1M -> V2B cold clipper -> LDR2 -> V3B. LDR1/LDR2 switch the channels. One shared tone stack, then
    a Normal and an Overdrive master.

    Controls, page 1 -- the real panel: Channel (Normal / Overdrive), Bright, Crunch, Normal Preamp, Overdrive Preamp,
    Treble, Middle, Bass, Normal Master, Overdrive Master, Presence. Page 2 (synthetic): Bias, Tube Feel, Speaker
    (4 / 8 / 16 ohm), Output.
*/
class SLO100StyleAmplifierProcessor : public EffectProcessor
{
public:
    SLO100StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "SLO-100-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8b0000); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** 100 W into 16 ohm is ~57 V peak (same as the JCM800). */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    // Placeholders -- calibrated during prepare() from a POWERCAL sweep if the full model is stable enough.
    // If not, fall back to the Twin Reverb's own fitted constants (same 6L6GC power section topology).
    static constexpr double bmGain0 = 9.5;      // placeholder, to be calibrated
    static constexpr double bmYmax = 0.198;      // placeholder, to be calibrated
    static constexpr double bmKneeN = 6.0;
    // The removed phase inverter / power tubes / output transformer / speaker / feedback loop have their OWN
    // frequency response beyond the tone stack. The earlier one-pole shelf (bmShelfHz/bmShelfHfGain) was
    // calibrated only up to 1.65 kHz and wrongly kept falling: this amp's own full reference netlist measures
    // +2.8 dB @80 Hz (speaker cone resonance past the strong NFB), ~flat mids, a +3 dB presence-region ridge
    // at ~4 kHz and a steep roll above ~6 kHz. Refitted 2026-10-09 as the analog section the measurement
    // shows (resonant LF shelf + one zero + resonant HF pole pair), discretized in prepare().
    static constexpr double bmBumpHz = 80.0, bmBumpQp = 4.0, bmBumpQz = 2.85;
    static constexpr double bmTopZHz = 4695.0, bmTopPHz = 4576.0, bmTopQp = 1.04;
    // Presence: the 25K pot + .1 uF leg in the feedback path opens the loop progressively at HF. The
    // reference netlist's presence wiring reads dead (same compromise class as the JCM800's), so the fitted
    // law is shared with the SuperLead's identical 25K/.1uF network: a resonant high-pass contribution into
    // the saturator drive, mix m(p) = K*p/(1-R*p).
    static constexpr double bmPresHz = 4684.0, bmPresQ = 0.67, bmPresZeroHz = 3205.0;
    static constexpr double bmPresMixK = 0.727, bmPresMixR = 0.909;
    // Level re-trim: the refit is ~5 dB hotter at noon than the shelf it replaced, and PedalUnityLevel needs
    // noon at unity while the registry's -18.92 dB trim stays put.
    static constexpr double bmLevelTrim = 0.62;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1bPlate, v1aPlate, v2aPlate, v2bPlate, v3bPlate, followerOut, toneStackOut,
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
    double railV1() const noexcept { return channels[0].supply.voltage (channels[0].sF); }
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
    /** One channel's preamp netlist (V1B -> its branch -> V3B -> V3A follower). */
    struct PreSide
    {
        NodalCircuit net;
        NodalCircuit::DynamicState rest;
        int srcV1 = 0, srcV2 = 0, srcV3 = 0, srcIn = 0;
        int rGainTop = 0, rGainBot = 0, rNGainTop = 0, rNGainBot = 0;  // OD / Normal preamp pots (500KL)
        int rCrunchSeries = 0, rCrunchShunt = 0, rBrightSeries = 0;    // the Crunch and Bright switches
        NodalCircuit::Node plateV1b = 0, plateV1a = 0, plateV2a = 0, plateV2b = 0, plateV3b = 0, follower = 0;
        double followerDc = 0.0;
    };

    struct Channel
    {
        std::array<PreSide, 2> pre;          // [0] Normal, [1] Overdrive -- only the selected one is solved
        int activePre = 1;
        double followerDc = 0.0;             // the active side's follower DC (what the tone stack sees at rest)
        NodalCircuit power, supply;
        NodalCircuit::DynamicState powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0,
            rPresTop = 0, rPresBottom = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0,
                           wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 470.0;

        // reducedOrder behavioural power stage state (see behavioralPowerStage()); bmRail is set to the real
        // nominal rail in prepare(). The three quads are the fitted sections' direct-form-I histories.
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        double bmAX1 = 0.0, bmAX2 = 0.0, bmAY1 = 0.0, bmAY2 = 0.0;
        double bmBX1 = 0.0, bmBX2 = 0.0, bmBY1 = 0.0, bmBY2 = 0.0;
        double bmHpX1 = 0.0, bmHpX2 = 0.0, bmHpY1 = 0.0, bmHpY2 = 0.0;
        double presenceMix = 0.0;

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, iE = 0, iF = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0, sE = 0, sF = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double nGain, gain, treble, mid, bass, presence, nMaster, powerDrive, bias, tubeFeel;
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
    void designPowerFilters();
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;
    double bmAB0 = 1.0, bmAB1 = 0.0, bmAB2 = 0.0, bmAA1 = 0.0, bmAA2 = 0.0;
    double bmBB0 = 1.0, bmBB1 = 0.0, bmBB2 = 0.0, bmBA1 = 0.0, bmBA2 = 0.0;
    double bmHpB0 = 0.0, bmHpB1 = 0.0, bmHpB2 = 0.0, bmHpA1 = 0.0, bmHpA2 = 0.0;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* inputParam = nullptr;   // the Normal channel's Bright switch (id kept for presets)
    juce::AudioParameterFloat* crunchParam = nullptr;
    juce::AudioParameterFloat* nGainParam = nullptr;
    juce::AudioParameterFloat* nMasterParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedNGain, smoothedNMaster, smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
