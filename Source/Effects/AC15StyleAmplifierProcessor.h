#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Vox AC15-style guitar amplifier (the original 1959/1960 EF86-preamp "Twin" chassis, Channel I only --
    docs/circuits/AC15Twin.md -- read that file to understand this processor). Genuinely different family from every
    Marshall/Fender amp built so far on this roadmap: an EF86 pentode preamp gain stage (not a triode), a cathodyne
    (split-load) phase inverter -- a SINGLE triode half with equal plate/cathode load resistors producing two
    anti-phase outputs, not a long-tailed pair -- and a pair of EL84s in Class A, CATHODE-BIASED (self-bias, no fixed
    bias supply) with NO global negative feedback loop at all. Channel II (the "Vibravox" tremolo channel, its own
    ECF82 triode-pentode and oscillator) is not modelled, matching this project's standing rule against modelling
    built-in vibrato/tremolo oscillators (same call as the Twin Reverb/Deluxe Reverb).

    Controls, page 1: Input (High / Low sensitivity jack), Volume, Tone (a single treble-cut control, not a Fender/
    Marshall TMB stack -- the real AC15 has no Bass/Middle/Presence), Output (a plug-in level control; the real amp
    has no master volume). Page 2: Power Drive (a synthetic master ahead of the phase inverter, matching this
    project's convention for every other amp here), Bias (a synthetic shift of the shared cathode-bias operating
    point -- the real amp has no adjustable bias trim, being self-biased), Tube Feel, Speaker (4 / 8 / 16 ohm, on the
    transformer's 16 ohm tap; the real amp ships a fixed ~15 ohm speaker).
*/
class AC15StyleAmplifierProcessor : public EffectProcessor
{
public:
    AC15StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "AC15-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 16 ohm tap) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 40.0;

    // ---- reduced-order power stage, built in from the start ----
    // Same pattern as every amp since the Super Lead: the phase inverter + power tubes + output transformer are
    // replaced by a constant-cost behavioural curve (behavioralPowerStage()) once reducedOrder is set, fitted to
    // this amp's OWN full reference model (no shared-power-section shortcut available here -- the cathodyne PI and
    // cathode-biased EL84 pair are unique to this amp on the roadmap so far). Boundary: everything up to and
    // including the Tone control is unchanged; only what comes after it (phase inverter, power tubes, transformer,
    // the physical speaker load) is replaced.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 0.070;
    static constexpr double bmYmax = 0.002837;
    static constexpr double bmKneeN = 1.5;
    static constexpr double bmShelfHz = 120.0;
    static constexpr double bmShelfHfGain = 0.7;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { preampPlate, toneOut, volumeOut, piGrid, piPlate, piCathode, powerGridA, powerPlateA, powerPlateB,
                       speaker, cathodeBias };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
    void debugSetResistiveLoad (double ohms);
    /** Test hook: the shared cathode-bias resistor (a huge value starves the tubes toward cutoff, isolating whether
        an instability lives in the bias network). */
    void debugSetCathodeResistance (double ohms);
    void debugFreezeSupplyCurrent (bool freeze) { supplyCurrentFrozen = freeze; }
    double plateCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: EF86 gain stage -> Tone -> Volume
        int pSrcIn = 0, pSrcRail = 0;
        int rToneTop = 0, rVolTop = 0, rVolBot = 0;
        int penPre = 0;
        NodalCircuit::Node pPlate = 0, pToneOut = 0, pVolOut = 0;
        double vScreenPre = 220.0;

        // power section: cathodyne PI + 2x EL84, cathode-biased, no NFB
        int wSrcCf = 0, wSrcRail = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rCathodeBias = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPiPlate = 0, wPiCathode = 0, wPiGrid = 0, wGridA = 0, wGridB = 0,
                           wPP1 = 0, wPP2 = 0, wCathodeBias = 0;
        double vScreenPower = 280.0;

        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply: rectifier -> choke -> main B+ (sA), decoupled preamp tap (sB)
        int iA = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, tone, powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    double cathodeResistanceOverride = 0.0; // test hook: > 0 replaces the shared cathode resistor
    bool supplyCurrentFrozen = false;
    void updateSupply (Channel& ch) const;
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTone, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
