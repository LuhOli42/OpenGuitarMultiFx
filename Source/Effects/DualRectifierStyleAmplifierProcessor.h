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
    A Mesa/Boogie Dual Rectifier-style guitar amplifier (the original 2-channel solo head), modelled at component level on
    NodalCircuit from the factory schematic set (preamp "RF-1F", power amp, supplies and LDR switching matrix, 6-93
    GEO. M.; docs/circuits/DualRectifier.md -- read that file to understand this processor).

    One preamp chain (V1A -> V2A -> V2B -> V3A -> V3B follower), two Gain pots, two complete tone stacks with their own
    Masters, and the drawing's LDRs re-voicing it into the five modes of its truth table: ORANGE Normal (Vintage), ORANGE
    Clean, ORANGE cloned to Modern, RED Modern, RED cloned to Vintage. Modern drops the global negative feedback and the
    treble rolloff; Clean opens the cathode bypasses, pads V1A's output and doubles the feedback. 12AX7 long-tailed-pair
    PI, four 6L6GC or EL34 (the rear BIAS switch swaps the tube model and the -51 V / -39 V bias) as two push-pull pairs.

    Controls, page 1 = the real panel: Channel (Orange / Red) with that channel's own Gain, Treble, Mid, Bass, Presence,
    Master (ChannelKnobMemory.h), and the rear switches Orange Gain (Clean / Hi Gain), Channel Style (Normal / Orange to
    Modern / Red to Vintage), Rectifier (Tubes / Diodes), Power (Bold / Spongy), Bias Switch (6L6 / EL34). Page 2
    (synthetic): Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm), Output, Cab.
*/
class DualRectifierStyleAmplifierProcessor : public EffectProcessor
{
public:
    DualRectifierStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    std::unique_ptr<juce::XmlElement> getState() const override;
    void setState (const juce::XmlElement& state) override;
    const char* getName() const override { return "Dual Rectifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff1a1a2e); } // deep navy (Mesa chrome/dark)
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** 100 W into 16 ohm is ~57 V peak. */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage ----
    // Per-mode level match against RED Modern at the default knobs (AmpSwitchLevelSurvey, SWITCH_BASE for the Orange
    // modes: the dB midpoint of the E3 tone and the guitar DI, docs/circuits/ChannelLevelMatch.md). Order: Orange
    // Normal, Orange Clean, Orange to Modern, Red Modern, Red to Vintage. The Vintage modes sit ~13 dB under Modern
    // because their feedback loop takes that much gain off the power amp (the manual warns Modern is far louder).
    static constexpr std::array<double, 5> modeLevelMatchDb { 13.6, 11.3, 0.2, 0.0, 13.3 };
    static inline bool reducedOrder = false;
    // Refitted 2026-11-02 against the rebuilt full netlist (factory PI with BOTH halves conducting -- the old one had
    // V5A cut off, so the loop and the Presence were dead): Modern (no loop, the default) is the base. G0 from the
    // small-signal level (Red, Gain .05, Master .25: within ~2 dB at 100 Hz / 1 kHz / 4 kHz); the rest by the
    // AmpFullVsReduced chord search (worst band 6.4 -> 1.9 dB, level < 1 dB).
    static constexpr double bmGain0 = 55.0;
    static constexpr double bmYmax = 0.198;
    static constexpr double bmKneeN = 6.0;
    // The frequency response the reduced path's removed stages provide: an LF bump at bmBumpHz (resonant high-pass,
    // bmBumpQp = resonance height), a one-pole low cut at bmCutHz, and a zero/pole pair for the presence ridge into
    // the top rolloff.
    static constexpr double bmBumpHz = 80.0, bmBumpQp = 1.3;
    static constexpr double bmCutHz = 15.0;
    static constexpr double bmTopZHz = 1000.0, bmTopPHz = 5000.0, bmTopQp = 1.4;
    // physical ceiling: the stages this replaces clip at the rail, and the fitted filters' resonance can
    // overshoot the knee's bound ~2x on saturated LF -- the cap keeps emitted volts inside the sanity bound.
    static constexpr double bmOutMax = 62.0;
    static constexpr double bmLevelTrim = 0.55;
    // The loop and tube switches, folded into the behavioural stage: drive into / level out of it, in dB, at Vintage's
    // loop (nfbAmount 1), Clean's doubled loop (~2) and with EL34s; Modern (no loop) is the fitted base. Interpolated in
    // nfbAmount so an LDR fade glides. From the full netlist's small-signal levels (Vintage -10.5..-15.7 dB under Modern;
    // EL34 +3.7 dB over 6L6 in Modern); the saturated levels then match within 1 dB with no output correction.
    static constexpr double bmVintageInDb = -14.5, bmVintageOutDb = 0.0;
    static constexpr double bmCleanInDb = -18.5, bmCleanOutDb = 0.0;
    static constexpr double bmEL34InDb = 3.0, bmEL34OutDb = 0.0;

    // ---- diagnostics ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v2aPlate, v2bPlate, v3aPlate, followerOut, toneStackOut,
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
    // The drawing's switching LDRs (LDR11-13 are the FX loop, not modelled).
    enum Ldr { ldr1, ldr2, ldr3a, ldr3b, ldr4, ldr5, ldr6, ldr7, ldr8, ldr9, ldr10, ldr14, ldr15, ldr16, ldr19, ldr20, numLdrs };

    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        double chanTrim = 1.0; // eased channel level match
        bool alignOutput = false;

        // preamp: four cascaded 12AX7 gain stages + cathode follower
        int pSrcV2 = 0, pSrcV3 = 0, pSrcE = 0, pSrcIn = 0;
        int rRdGainTop = 0, rRdGainBot = 0, rOrGainTop = 0, rOrGainBot = 0;
        std::array<int, numLdrs> ldr {}; // resistor handles, in `pre` or `power` (see updateLdrs)
        NodalCircuit::Node pPlateV1a = 0, pPlateV2a = 0, pPlateV2b = 0, pPlateV3a = 0, pFollower = 0;
        double followerDc = 0.0;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rPresence = 0, rBiasTrim = 0;
        int rRdTrebleTop = 0, rRdTrebleBottom = 0, rRdBass = 0, rRdMidTop = 0, rRdMidBottom = 0, rRdMasterTop = 0,
            rRdMasterBottom = 0, rRdPresence = 0;
        int rOrTrebleTop = 0, rOrTrebleBottom = 0, rOrBass = 0, rOrMidTop = 0, rOrMidBottom = 0, rOrMasterTop = 0,
            rOrMasterBottom = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0,
                           wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0, wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 460.0;

        // reducedOrder behavioural power stage state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)
        double bmAX1 = 0.0, bmAX2 = 0.0, bmAY1 = 0.0, bmAY2 = 0.0;
        double bmBX1 = 0.0, bmBX2 = 0.0, bmBY1 = 0.0, bmBY2 = 0.0;
        double bmCutState = 0.0;
        double bmPresState = 0.0;   // presence HF-shelf lowpass state (reducedOrder only)
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
        double gain, treble, mid, bass, presence, master, powerDrive, bias, tubeFeel;
        int speaker;
        int channel; // whose pots the knobs move: 0 Orange, 1 Red
    };
    void updatePots (const Knobs& k);
    void updateLdrs (bool snap);
    /** 0 Orange Normal, 1 Orange Clean, 2 Orange to Modern, 3 Red Modern, 4 Red to Vintage. */
    int currentMode() const noexcept
    {
        if (juce::roundToInt (channelParam->get()) == 1)
            return juce::roundToInt (styleParam->get()) == 2 ? 4 : 3;
        if (orangeGainParam->get() < 0.5f)
            return 1;
        return juce::roundToInt (styleParam->get()) == 1 ? 2 : 0;
    }
    std::array<double, numLdrs> ldrLog {};
    double nfbAmount = 1.0;       // reducedOrder: 0 = no loop (Modern), 1 = Vintage's, ~2 = Clean's doubled loop
    double orangePresence = 0.3;  // the Orange Presence (in the NFB loop), held while Red is selected
    KorenPentode model6L6, modelEL34;
    bool appliedEL34 = false;
    bool appliedEL34Reduced = false;
    static constexpr double spongyRailScale = 0.90;
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
    struct ReducedFit
    {
        double g0 = bmGain0, ymax = bmYmax, knee = bmKneeN, bumpHz = bmBumpHz, bumpQp = bmBumpQp, cutHz = bmCutHz,
               topZ = bmTopZHz, topP = bmTopPHz, topQp = bmTopQp, trim = bmLevelTrim,
               vinDb = bmVintageInDb, voutDb = bmVintageOutDb, cinDb = bmCleanInDb, coutDb = bmCleanOutDb,
               elInDb = bmEL34InDb, elOutDb = bmEL34OutDb;
    } fit;
    double bmAB0 = 1.0, bmAB1 = 0.0, bmAB2 = 0.0, bmAA1 = 0.0, bmAA2 = 0.0;
    double bmBB0 = 1.0, bmBB1 = 0.0, bmBB2 = 0.0, bmBA1 = 0.0, bmBA2 = 0.0;

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
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* spongyParam = nullptr;
    juce::AudioParameterFloat* orangeGainParam = nullptr;
    juce::AudioParameterFloat* styleParam = nullptr;
    juce::AudioParameterFloat* rectifierParam = nullptr;
    juce::AudioParameterFloat* tubesParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    double rectifierScale = 1.0; // rectifier resistance multiplier (1 = diodes, 5 = 5U4 tubes)
    double rectifierSag = 1.0;   // extra sag depth in the reduced model with the tube rectifier
    double railNow = 460.0;      // B+ after the rectifier / Spongy droop

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMid, smoothedBass,
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

    std::unique_ptr<ChannelKnobMemory> channelMemory;

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
