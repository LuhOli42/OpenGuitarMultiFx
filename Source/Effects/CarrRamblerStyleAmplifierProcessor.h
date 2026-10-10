#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Carr Rambler-style guitar amplifier (Steve Carr's 1x12" combo: a "classic 60s American" blackface-like preamp into
    an "early 50s" class-A cathode-biased pair of 5881/6L6GC, pentode/triode switchable, zero global feedback), modelled
    at component level on NodalCircuit -- docs/circuits/CarrRambler.md, read that file to understand this processor.

    Three parts, the same structure as the AC15 / Trainwreck models:
      1. the preamp -- a 12AX7 gain stage, the single 1M Volume, the blackface TMB tone stack (the real amp's Mid control
         is a pot here, where a stock blackface has a fixed resistor), and a second 12AX7 recovery stage;
      2. the power section -- a 5751 long-tailed-pair phase inverter, two cathode-biased 6L6GCs on 220k grid leaks whose
         return is the bias-vary tremolo injection node (late-50s-style output-tube-bias tremolo: a slow LFO wiggles the
         grids' DC reference, Speed + Depth on the front panel), a triode/pentode mode switch (screens follow plates in
         triode mode), the output transformer -- and NO global negative feedback;
      3. a rectifier and filter chain (plates, screens, phase inverter, preamp).

    The built-in spring reverb is NOT modelled (a separate ReverbProcessor covers that ground, same standing rule as
    the Twin Reverb's own tank); the Reverb knob is therefore absent from the parameter list.

    Controls, page 1: Volume, Treble, Mid, Bass, Speed, Depth (the all-tube bias-vary tremolo), Mode (Pentode / Triode --
    the real amp's Triode/Pentode toggle, 28 W / 14 W). Page 2 (synthetic, plus Output -- a plug-in level control; the
    real amp has no master volume): Power Drive (a master ahead of the phase inverter), Bias (the shared cathode-bias
    resistor), Tube Feel (supply sag: 0 = stiff, 1 = the real amp) and Speaker (4 / 8 / 16 ohm, a real speaker on the
    transformer's 8 ohm tap).
*/
class CarrRamblerStyleAmplifierProcessor : public EffectProcessor
{
public:
    CarrRamblerStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Carr Rambler-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc8813a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 8 ohm tap) scaled to a signal level: 28 W into 8 ohm is ~15 V rms /
        ~21 V peak. The reduced-order stage emits the same speaker-volts scale. */
    static constexpr double outputScale = 1.0 / 22.0;
    static constexpr double fullOutputScale = 1.0 / 22.0;

    // ---- reduced-order power stage (same mechanism as TwinReverbStyleAmplifierProcessor's, see that header) ----
    // Boundary: the whole preamp (both gain stages + tone stack) stays a real solved circuit; the 5751 PI, the two
    // 6L6GCs, the tremolo injection, the OT and the physical speaker are replaced by behavioralPowerStage(). Default
    // false (the internal-probe tests assume the full topology); the shipped app turns it on in EffectRegistry.cpp.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 60.0;   // closed-loop small-signal gain, pOut -> speaker, at the nominal rail (CR_POWERCAL)
    static constexpr double bmYmax = 0.30;    // peak output as a fraction of the (sagged) rail at full saturation -- fitted
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.7;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { stage1Plate, stage1Cathode, recoveryPlate, recoveryCathode, toneOut, pOut,
                       piPlateA, piPlateB, piCathode, powerGridA, powerPlateA, powerPlateB, cathodeBias, speaker, tremNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPhaseInverter() const noexcept { return channels[0].piRail; }
    double railPreamp() const noexcept { return channels[0].preRail; }
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
    /** Test hook: replaces the speaker by a plain resistor. */
    void debugSetResistiveLoad (double ohms);
    double plateCurrentA() const noexcept;
    double plateCurrentB() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, power, supply;
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp
        int pSrcIn = 0, pSrcRail = 0;
        int rVolTop = 0, rVolBot = 0, rTrebleTop = 0, rTrebleBot = 0, rBass = 0, rMid = 0;
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate2 = 0, pK2 = 0, pOut = 0, pTone = 0;
        double outDc = 0.0, driveDc = 0.0, driveDcCoeff = 0.0;

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcTrem = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rCathodeBias = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wPiGrid = 0, wPiPlateA = 0, wPiPlateB = 0, wPiK = 0, wGridA = 0, wGridB = 0, wPP1 = 0, wPP2 = 0,
                           wTrem = 0, wCathodeBias = 0, wOut = 0;

        // reducedOrder behavioural state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply: rectifier -> plate rail (sA) -> screen node (sB); PI / preamp taps are RC-filtered from sB
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        double fbPlate = 2.0 * 0.042, fbScreen = 2.0 * 0.005; // ~100 ms-filtered measured draw (idle estimate)
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, treble, middle, bass, depth, powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
        int mode;    // 0 = pentode, 1 = triode
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* speedParam = nullptr;
    juce::AudioParameterFloat* intensityParam = nullptr; // panel name "Depth"
    juce::AudioParameterFloat* modeParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedMiddle, smoothedBass, smoothedSpeed,
        smoothedIntensity, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

    /** JUCE's reset() must return the circuit to its DC operating point; prepare() no-ops on a same-rate re-prepare, so
        reset() re-arms the rate and forces the rebuild. Control thread only. */
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
