#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Ampeg B-15N Portaflex-style bass amplifier, modelled at component level on NodalCircuit from the
    amp's factory schematic (see docs/circuits/AmpegB15.md -- read that file to understand this processor).

    Two solver blocks plus a small power-supply model:
      1. the preamp -- two 6SL7 gain stages around the passive bass/treble network, the Volume pot and
         the Ultra-Lo / Ultra-Hi rocker networks;
      2. everything the global feedback loop passes through -- the third 6SL7's floating-paraphase
         phase splitter (one half drives the first output grid, a plate tap feeds the second half which
         drives the other), the two 6L6GCs on fixed ~-50 V bias, the output transformer (coupled
         inductors), the speaker and the negative feedback into the driver's cathode;
      3. the rectifier / filter supply, whose sag under load is a large part of the "flip-top" feel.

    Controls, page 1: Input (0 dB / -15 dB -- the amp's two front-panel jacks), Volume, Ultra-Lo,
    Ultra-Hi (the faceplate rockers), Bass, Treble, Master. Page 2 (synthetic, plus Output -- a plug-in
    level control): Power Drive (a level between the preamp and the phase splitter), Bias (the output
    grids' bias tap, hot/cold), Tube Feel (how much the supply sags and how little negative feedback
    there is: 0 = stiff, 1 = the real amp) and Speaker (4 / 8 / 16 ohm).
*/
class AmpegB15StyleAmplifierProcessor : public EffectProcessor
{
public:
    AmpegB15StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "B-15-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3a8a5a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 78.1; // reducedOrder speaker-level mapping (below)
    static constexpr double fullOutputScale = 1.0 / 40.0; // ~25 W into 8 ohm is ~14 Vrms at the terminal

    /** Ships reducedOrder in the app (the full push-pull + OT + NFB netlist is the calibration
        reference -- same pattern as BassmanStyleAmplifierProcessor). EffectRegistry.cpp flips this
        on centrally; the unit tests reset it to false so they keep probing the reference model. */
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 25.0;      // closed-loop gain of the driver + push-pull + OT + NFB
    static constexpr double bmYmax = 0.208;      // fraction of the sag rail reachable at the speaker
    static constexpr double bmAsym = 0.09;       // push-pull clip asymmetry
    static constexpr double bmGridClampV = 24.0; // driver input where the paraphase stage saturates
    static constexpr double bmDcHz = 8.0;        // OT low-frequency saturation rolloff
    static constexpr double bmShelfHz = 70.0;    // speaker-magnetics low-shelf pole
    static constexpr double bmShelfHfGain = 0.53;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { firstPlate, toneStackOut, secondPlate, driverPlate, inverterPlate,
                       powerGridA, powerPlateA, powerPlateB, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    /** Supply rails at the last update: plates+screens, preamp+driver. */
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    /** Development/test hook: replaces the negative-feedback resistor (a huge value opens the loop). */
    void debugSetFeedbackResistance (double ohms);
    /** Test hook: replaces the speaker by a plain resistor. */
    void debugSetResistiveLoad (double ohms);
    double debugIterations (int block) const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
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

        // preamp
        int pSrcVcc = 0, pSrcIn = 0;
        int rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0;
        int rVolumeTop = 0, rVolumeBot = 0, rMasterTop = 0, rMasterBot = 0;
        int rUltraLoA = 0, capUltraHi = 0;
        NodalCircuit::Node pPlate1 = 0, pTone = 0, pPlate2 = 0, pOut = 0;

        // power section
        int wSrcPre = 0, wSrcVdr = 0, wSrcCt = 0, wSrcNeg = 0;
        int rFeedback = 0, rBiasTapA = 0, rBiasTapB = 0;
        int rSpkRe = 0, rSpkRp = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wDrvPlate = 0, wInvPlate = 0, wPowerGridA = 0, wPP1 = 0, wPP2 = 0, wOut = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 380.0;

        // supply
        int iA = 0, iB = 0, srcVoc = 0, rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        NodalCircuit::Node wTone = 0;
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0, bmDcState = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double input, volume, ultraLo, ultraHi, bass, treble, master;
        double powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void applySpeaker (Channel& ch, int index) const;
    double sagRail (double envelope) const noexcept;
    double behavioralPowerStage (Channel& ch, double toneVoltage) noexcept;
    void recover (Channel& ch) const;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double speakerGain = 1.0;
    double idleSupplyCurrent = 0.0;
    bool resistiveLoadForced = false;
    double feedbackOverride = 0.0;
    void updateSupply (Channel& ch) const;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* ultraLoParam = nullptr;
    juce::AudioParameterFloat* ultraHiParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedBass, smoothedTreble,
        smoothedMaster, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;
    int appliedSpeaker = -1;

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
    bool channel1Stale = false;
    bool channelsSynced = true;
    long long identicalRun = 0;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
