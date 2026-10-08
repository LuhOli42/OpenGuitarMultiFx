#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Fender Deluxe Reverb-style guitar amplifier (the blackface "AB763" circuit, ~22 W), modelled at component level on
    NodalCircuit from Fender's own factory schematic ("DELUXE REVERB-AMP AB763"; docs/circuits/DeluxeReverbAB763.md --
    read that file to understand this processor). Same family and the same architecture as
    TwinReverbStyleAmplifierProcessor (built first, see its own header comment for the shared reasoning) -- this doc only
    notes what's specific here: a 2-band tone stack (no Middle -- a fixed 6.8k resistor takes its place, the real amp has
    no Middle knob either), a 12AT7 (not 12AX7) phase inverter, and a SINGLE 6V6GT per side (not a doubled pair).

    Scope: the CLEAN AMPLIFICATION PATH only -- no spring reverb tank/driver modelled (a separate ReverbProcessor covers
    that ground). The built-in bias-modulated tremolo (labelled "vibrato" on the real amp) IS modelled as a sine LFO
    applied at the same signal-chain position as the AB763's V3A/V3B roach-coupler oscillator.

    Controls, page 1: Input (Normal / Vibrato / Both), Volume, Treble, Bass (no Middle on this amp), Speed, Intensity,
    Output. Page 2: Power Drive, Bias, Tube Feel, Speaker (4 / 8 / 16 ohm) -- same page-2 convention as every other
    modelled amp in this project. Speed and Intensity control the built-in bias-modulated tremolo (labelled "vibrato" on
    the real amp). The oscillator is a sine LFO applied as amplitude modulation on the signal feeding the power stage,
    matching the AB763's V3A/V3B roach-coupler position in the signal chain.
*/
class DeluxeReverbStyleAmplifierProcessor : public EffectProcessor
{
public:
    DeluxeReverbStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Deluxe Reverb-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8ec9e0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 8 ohm tap -- the real amp's single 12" Jensen) scaled to a signal
        level: ~22 W into 8 ohm is ~13.3 V rms / 18.8 V peak. */
    static constexpr double outputScale = 1.0 / 18.8;

    // ---- reduced-order power stage (see SuperLeadStyleAmplifierProcessor's own note for the full mechanism/reasoning) ----
    // Built in from the start this time (per the user's own request, 2026-09-28): calibrated as part of the initial build,
    // not bolted on after. Boundary: BOTH channels' preamp + tone stack (a real, always-solved linear/triode circuit) stay
    // exact; the gain-recovery stage, phase inverter, two 6V6GT, output transformer, feedback and physical speaker are
    // replaced by behavioralPowerStage(). Default false (every internal-probe test assumes the full topology); the shipped
    // app turns this on in EffectRegistry.cpp, same as the Super Lead/Bassman/Twin Reverb. See docs/circuits/DeluxeReverbAB763.md.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 50.0;   // fitted from this amp's own DR_POWERCAL sweep, see the .cpp
    static constexpr double bmYmax = 0.0298;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.7;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { channelNormalPlate, channelVibratoPlate, mixNode, recoveryPlate, phaseInverterGrid, phaseInverterPlateA,
                       phaseInverterPlateB, phaseInverterTail, powerPlateA, powerPlateB, powerGridA, speaker, biasNode, feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept { return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0; }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPhaseInverter() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double debugIterations (int block) const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    long long debugSanityRejects() const noexcept { return sanityRejects; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    int debugRecoveries() const noexcept { return recoveries; }
    bool debugLastSampleOk() const noexcept { return lastSampleOk; }
    void debugSetResistiveLoad (double ohms);
    void debugSetFeedbackResistance (double ohms);
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

        // preamp: both channels' gain triode + tone stack live here, mixing into pMix
        int pSrcVcc = 0, pSrcInNormal = 0, pSrcInVibrato = 0;
        int rVolTop[2] {}, rVolBot[2] {};               // [0] Normal, [1] Vibrato
        int rTrebleTop[2] {}, rTrebleBottom[2] {}, rBass[2] {}; // no Middle pot on this amp (a fixed 6.8k instead)
        NodalCircuit::Node pPlateNormal = 0, pPlateVibrato = 0, pMix = 0;
        double followerDc = 0.0; // unused (no cathode follower here); kept for symmetry with the other amps' updateSupply()

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wRecoveryIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0,
                           wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 458.0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply
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
        double volume, treble, bass, powerDrive, bias, tubeFeel;
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
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* speedParam = nullptr;
    juce::AudioParameterFloat* intensityParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedBass, smoothedOutput, smoothedSpeed,
        smoothedIntensity, smoothedPower, smoothedBias, smoothedFeel;

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
    double lfoPhase = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0, failuresPre = 0, failuresPower = 0;
    bool dcOk = false;
    bool lastSampleOk = true;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
