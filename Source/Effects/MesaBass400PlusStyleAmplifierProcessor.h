#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Mesa/Boogie Bass 400+-style bass amplifier, modelled at component level on NodalCircuit from the
    amp's schematic (see docs/circuits/MesaBass400Plus.md -- read that file to understand this processor).

    Four solver blocks:
      1. the preamp -- two 12AX7 gain stages around the Volume pot (with its pull-Bright), the passive
         bass/treble/middle tone section and the recovery stage;
      2. the seven-band graphic equalizer -- a feedback EQ around a finite-gain stage, each slider a pot
         between the input and output buses with a series L-C trap on its wiper, exactly as in the amp;
      3. everything the global feedback loop passes through -- the long-tailed-pair phase splitter, the
         two common-cathode drivers with their resistive level-shifters (which set the output tubes'
         ~-68 V bias), the twelve 6L6GCs as two push-pull sextets, the output transformer (coupled
         inductors), the speaker and the negative feedback into the phase splitter;
      4. the rectifier / filter supply.

    Controls, page 1: Volume (channel 1), Bright (the Volume's pull switch), Bass, Middle, Treble,
    Master. Page 2: the seven graphic-EQ sliders (40 Hz .. 6.6 kHz), Power Drive, Bias, Tube Feel,
    Speaker, Output.
*/
class MesaBass400PlusStyleAmplifierProcessor : public EffectProcessor
{
public:
    MesaBass400PlusStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Bass 400+-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff8a5ac9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 60.0; // ~270 W into 4 ohm is ~33 Vrms at the terminal

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { firstPlate, secondPlate, toneStackOut, recoveryPlate, eqOut,
                       phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB,
                       driverPlateA, powerGridA, powerPlateA, powerPlateB, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    void debugSetFeedbackResistance (double ohms);
    void debugSetResistiveLoad (double ohms);
    double debugIterations (int block) const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
    double plateCurrentTotal() const noexcept;
    double screenCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, eq, power, supply;
        NodalCircuit::DynamicState preRest, eqRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp
        int pSrcVcc = 0, pSrcIn = 0;
        int rVolumeTop = 0, rVolumeBot = 0, capBright = 0;
        int rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0, rMid = 0;
        int rMasterTop = 0, rMasterBot = 0;
        NodalCircuit::Node pPlate1 = 0, pPlate2 = 0, pTone = 0, pPlate3 = 0, pOut = 0;

        // graphic EQ (linear block)
        int qSrcIn = 0;
        int rEqTop[7] = {}, rEqBot[7] = {};
        NodalCircuit::Node qOut = 0;

        // power section
        int wSrcPre = 0, wSrcPi = 0, wSrcCt = 0, wSrcNeg = 0, wSrcVdr = 0;
        int rFeedback = 0, rBiasTapA = 0, rBiasTapB = 0;
        int rSpkRe = 0, rSpkRp = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wGridA = 0, wPlateA = 0, wPlateB = 0, wTail = 0, wDrvPlateA = 0,
                           wPowerGridA = 0, wPP1 = 0, wPP2 = 0, wOut = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 400.0;

        // supply
        int iA = 0, iB = 0, iC = 0, srcVoc = 0, srcVoc2 = 0, rRect = 0, rRect2 = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double input, volume, bright, bass, middle, treble, master;
        double eq[7];
        double powerDrive, bias, tubeFeel;
        int speaker; // 0 = 2 ohm, 1 = 4, 2 = 8
    };
    void updatePots (const Knobs& k);
    void applySpeaker (Channel& ch, int index) const;
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
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* eqParam[7] = {};
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedBass, smoothedMiddle, smoothedTreble,
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
