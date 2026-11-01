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
    A Dumble Steel String Singer-style guitar amplifier, modelled at component level on NodalCircuit
    (docs/circuits/DumbleSteelString.md -- read that file to understand this processor). The Steel String
    Singer is Howard Dumble's ultra-clean flagship: a ~100 W head built around FOUR 6L6GC beam tetrodes,
    an unusually high-voltage supply, and his signature direct-coupled cathode-follower drivers feeding the
    output grids. It is famous for clean headroom that does not run out -- Stevie Ray Vaughan's amp.

    Modelled, after the #002 netlist: V1A into the distributed tone stack, Volume, V1B, the Hi/Lo 7-position filter
    bank, V2A and the cathode follower with the LNFB loop; the front-panel switches (Bright, Deep, Guitar / Mic,
    Accent); the reverb (send taken off V1B's grid, the spring tank, the return stages and the 220K / 220K mixer at
    the Level pot's input); the 12AX7 long-tailed-pair phase inverter with global negative feedback, four 6L6GC as
    two push-pull pairs in fixed bias, and the output transformer into a resonant speaker load. The FET input and
    the direct-coupled driver stage (simplified to capacitive coupling) are not modelled.

    Controls, page 1 (the front panel): Volume, Bright, Deep, Guitar / Mic, Treble, Middle, Bass, Filter (Hi),
    Filter (Lo), Reverb (On / Off), Accent, Level (Send / Return fixed at 0.7). Page 2: Presence Trim (an internal trim on #002), Power
    Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output, Cab.
*/
class DumbleSteelStringStyleAmplifierProcessor : public EffectProcessor
{
public:
    DumbleSteelStringStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    /** The 7-position Filter rotaries read as stepped knobs, like the panel's own. */
    bool prefersSteppedKnob (const juce::AudioParameterFloat& p) const override { return &p == filterHiParam || &p == filterLoParam; }
    std::unique_ptr<juce::XmlElement> getState() const override { return EffectProcessor::getState(); }
    /** Presets from before 2026-10-31 carry `sss_voicing` (Jazz / PAB / Rock) and `sss_mid_boost`: Jazz becomes Mic,
        PAB and Rock become Guitar, Mid Boost is dropped. */
    void setState (const juce::XmlElement& state) override;
    void migrateLegacySnapshotParams (std::vector<std::pair<juce::String, float>>& params) const override;
    const char* getName() const override { return "Dumble Steel String"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3b2f1e); } // tweed brown
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** 100 W into 16 ohm is ~57 V peak. */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    static inline bool reducedOrder = false;
    // Twin Reverb's fitted 6L6GC constants (same tube type and push-pull topology).
    static constexpr double bmGain0 = 11.875;
    static constexpr double bmYmax = 0.1188;
    static constexpr double bmKneeN = 7.5;
    static constexpr double bmShelfHz = 72.0;
    static constexpr double bmShelfHfGain = 0.55;
    static constexpr double bmLevelTrim = 0.8648;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, v2aPlate, followerOut, toneStackOut,
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

        // preamp: three cascaded 12AX7 gain stages + cathode follower
        int pSrcV3 = 0, pSrcV2 = 0, pSrcE = 0, pSrcIn = 0;
        int rVolTop = 0, rVolBot = 0;
        NodalCircuit::Node pPlateV1a = 0, pPlateV1b = 0, pPlateV2a = 0, pFollower = 0;
        double followerDc = 0.0;

        // front-panel switch elements (pre circuit): Bright cap across the Volume pot, Hi/Lo 7-position filter links
        int capBright = 0;
        NodalCircuit::Node pVolWiper = 0; // V1B's grid side of the Volume pot: the reverb send's tap
        int rHiLink[7] = { 0, 0, 0, 0, 0, 0, 0 };
        int rLoLink[7] = { 0, 0, 0, 0, 0, 0, 0 };

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBassTop = 0, rBassBottom = 0, rMidVar = 0,
            rPresTop = 0, rPresBottom = 0, rBiasTrim = 0, rMasterTop = 0, rMasterBottom = 0;
        // Guitar / Mic: Mic moves the treble pot's bottom from C35 (rC35Link) onto the bass wiper (rPadLink) and lifts
        // the R63 merge (rMergeLink). Deep swaps the direct link for the shelf network.
        int rMergeLink = 0, rPadLink = 0, rC35Link = 0, rDeepLink = 0, capAccent = 0;
        int wSrcRet = 0; // U40's cathode: the reverb return into the R85 220K mixer leg
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0,
                           wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 460.0;
        double vCt = 460.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)
        tubeamp::CouplingCapHighpass piCoupling; // the PI's input cap, which reducedOrder otherwise skips

        // reverb: C20 into the Send pot, C27 into the Return pot (one-pole high-passes)
        double sendHpState = 0.0, returnHpState = 0.0;

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
        double volume, treble, mid, bass, presence, master, powerDrive, bias, tubeFeel;
        int speaker;
        bool bright = false, deep = false, accent = false, mic = false, reverbOn = false;
        int filterHi = 0, filterLo = 0; // 0..6, matching the real 7-position filter rotaries
    };
    void updatePots (const Knobs& k);
    /** Reads the front-panel switch/filter params directly (stepped -- no smoothing needed) and
        combines them with the smoothed knob values. */
    Knobs knobsFromSmoothed (double vo, double tr, double mi, double ba, double pr, double ms,
                             double pw, double bi, double fe, int speaker) const;
    Knobs knobsFromParams() const;
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

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* filterHiParam = nullptr;
    juce::AudioParameterFloat* filterLoParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* deepParam = nullptr;
    juce::AudioParameterFloat* accentParam = nullptr;
    juce::AudioParameterFloat* guitarMicParam = nullptr;
    juce::AudioParameterFloat* reverbParam = nullptr;

    // The spring tank, shared by both channels, fed this block's sends and read one block later.
    SpringReverbProcessor spring;
    juce::AudioBuffer<float> reverbSend, reverbReturn;
    int reverbReturnLength = 0;
    int maxBlock = 0;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedMid, smoothedBass,
        smoothedPresence, smoothedMaster, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
