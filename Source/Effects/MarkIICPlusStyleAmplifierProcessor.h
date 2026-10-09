#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Mesa/Boogie Mark IIC+-style guitar amplifier, modelled at component level on NodalCircuit from Bancika's
    "Mark IIc+ inspired preamp" (diy-fever.com) -- docs/circuits/MarkIICPlus.md. Rebuilt 2026-10-08 to that drawing's
    topology: the defining Mark trait is the tone stack RIGHT AFTER the first stage, so the EQ shapes the signal
    before the cascade distorts it.

    Preamp: V1a -> Treble 250KB / Bass 250KA / Middle 10KB stack (Pull Shift swaps its caps) -> Volume 1 (1MA, Pull
    Bright) -> V1b. Rhythm: V1b -> 3.3M || 10pF -> V3a. Lead adds V1b -> 680K -> Lead Drive (1MA) -> V2a (120pF, 82K)
    -> 270K / 1nF -> V2b (270K plate, 3.3K cathode) -> 250pF || 220K -> V3a's grid (87K, 547pF). V3a -> 47K -> Lead
    Master (250KA) -> 150K / 4.7K -> V3b cathode follower. In Rhythm the Lead Drive's output is grounded (the real
    amp's channel switching). Then Master -> phase inverter (4x6L6, global feedback with Presence and Pull Deep).

    Controls, page 1 -- the real panel: Channel (Rhythm / Lead), Volume 1, Pull Bright, Treble, Pull Shift, Bass,
    Middle, Master, Pull Deep, Lead Drive, Lead Master, Presence. Page 2 (synthetic): Power Drive, Bias, Tube Feel,
    Speaker (4 / 8 / 16 ohm), Output. Page 3: the 5-band graphic EQ.
*/
class MarkIICPlusStyleAmplifierProcessor : public EffectProcessor
{
public:
    MarkIICPlusStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Mark IIC+-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f4f4f); } // dark slate grey
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** 100 W into 16 ohm is ~57 V peak. */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    // Twin Reverb's fitted 6L6GC constants (same tube type and push-pull topology).
    static constexpr double bmGain0 = 9.5;
    static constexpr double bmYmax = 0.198;
    static constexpr double bmKneeN = 6.0;
    // The removed phase inverter / power tubes / output transformer / speaker / feedback loop have their OWN
    // frequency response beyond the tone stack. The earlier one-pole shelf (bmShelfHz/bmShelfHfGain) was
    // calibrated only up to 1.65 kHz and wrongly kept falling: the measured audit signature (-26 dB @12 kHz,
    // too dark even for a Mark lead channel) plus this amp's own full reference netlist call for a mild
    // +3 dB @80 Hz cone-resonance bump, ~flat mids (the V-EQ scoop already lives in the always-built preamp),
    // a +1.5 dB presence plateau at ~4 kHz and an OPEN top: the reference's measured steep roll is a known
    // dead-presence artifact (below), so the top pair is fitted for a Mark lead channel's real air
    // (≈-0.5 dB @12 kHz rather than the measured -10 dB). Fitted 2026-10-09 as the analog section that
    // response calls for (resonant LF shelf + one zero + resonant HF pole pair), discretized in prepare().
    static constexpr double bmBumpHz = 89.0, bmBumpQp = 4.0, bmBumpQz = 2.29;
    static constexpr double bmTopZHz = 2508.0, bmTopPHz = 9484.0, bmTopQp = 0.30;
    // Presence: the 25K pot + .1 uF leg in the feedback path opens the loop progressively at HF. The
    // reference netlist's presence wiring reads dead (same compromise class as the JCM800's), so the fitted
    // law is shared with the SuperLead's identical 25K/.1uF network: a resonant high-pass contribution into
    // the saturator drive, mix m(p) = K*p/(1-R*p).
    static constexpr double bmPresHz = 4684.0, bmPresQ = 0.67, bmPresZeroHz = 3205.0;
    static constexpr double bmPresMixK = 0.727, bmPresMixR = 0.909;
    // Level re-trim: the refit is ~5 dB hotter at noon than the shelf it replaced, and PedalUnityLevel needs
    // noon at unity while the registry's -11.79 dB trim stays put.
    static constexpr double bmLevelTrim = 0.55;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, v2bPlate, v3aPlate, followerOut, toneStackOut,
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
    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp block 1 (pre0): V1a -> tone stack -> Volume 1; block 2 (pre): V1b -> (rhythm | lead V2a/V2b) -> V3a ->
        // Lead Master -> V3b follower. V1b's grid draws ~no current, so block 1's wiper drives block 2 directly.
        // Block 3 (pre2): V2b -> V3a -> Lead Master -> V3b, fed by V2a's plate through the 22nF/270K it sees anyway
        // and by the rhythm path's 3.3M (both high-impedance couplings, so splitting there is near-exact).
        NodalCircuit pre0, pre2;
        NodalCircuit::DynamicState pre0Rest, pre2Rest;
        int p0SrcV1 = 0, p0SrcIn = 0, pSrcG1b = 0, p2SrcV2 = 0, p2SrcV3 = 0, p2SrcV2a = 0, p2SrcX = 0;
        NodalCircuit::Node p0Wiper = 0, pX = 0;
        int pSrcV1 = 0, pSrcV2 = 0;
        int rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMid = 0, capBass = 0, capMid = 0;
        int rVol1Top = 0, rVol1Bot = 0, rBrightSeries = 0;
        int rGainTop = 0, rGainBot = 0, rLeadMute = 0, rLeadMaster = 0;
        NodalCircuit::Node pPlateV1a = 0, pPlateV1b = 0, pPlateV2a = 0, pPlateV2b = 0, pPlateV3a = 0, pFollower = 0, pToneOut = 0;
        double followerDc = 0.0;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rPresTop = 0, rPresBottom = 0, rBiasTrim = 0, rMasterTop = 0, rMaster = 0, rDeepSeries = 0;
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
        tubeamp::CouplingCapHighpass piCoupling; // the PI's input cap, which reducedOrder otherwise skips

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
        double volume1, gain, leadMaster, treble, mid, bass, presence, master, powerDrive, bias, tubeFeel;
        int speaker, channel;
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
    juce::AudioParameterFloat* volume1Param = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;         // Lead Drive (id kept for presets)
    juce::AudioParameterFloat* leadMasterParam = nullptr;
    juce::AudioParameterFloat* pullBrightParam = nullptr;
    juce::AudioParameterFloat* pullDeepParam = nullptr;
    juce::AudioParameterFloat* pullShiftParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* geqParams[5] {};
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume1, smoothedLeadMaster, smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedMaster, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

    static constexpr int geqBands = 5;
    static constexpr double geqFreqs[geqBands] = { 80.0, 240.0, 750.0, 2200.0, 6600.0 };
    struct BiquadState { double s1 = 0.0, s2 = 0.0; };
    struct BiquadCoeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    BiquadCoeffs geqCoeffs[geqBands] {};
    BiquadState geqState[2][geqBands] {};
    float lastGeqSliders[geqBands] {};
    int geqUpdateCounter = 0;

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
