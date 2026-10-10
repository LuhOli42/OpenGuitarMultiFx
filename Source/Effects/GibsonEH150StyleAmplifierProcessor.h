#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Gibson EH-150-style guitar amplifier (the Style-4 circuit of 1941-42, the amp Charlie Christian played through),
    modelled at component level on NodalCircuit -- docs/circuits/GibsonEH150.md, read that file to understand this
    processor.

    Three parts, the same structure as the Trainwreck Express-style amplifier:
      1. the instrument-channel preamp -- a 6SQ7 high-mu triode (the octal predecessor of the 6SL7/12AX7 family),
         the 1M Volume pot, then the 6SQ7 "common" second stage, with the unit's single Tone control (a treble-cut
         network, era-typical) on its plate;
      2. the 6N7 twin-triode paraphase phase inverter (the part Gibson fitted when they retired the interstage
         transformer) driving two CATHODE-BIASED 6L6s in push-pull through the ~5k output transformer -- and NO global
         negative feedback;
      3. the 5U4G-rectified supply (sagging, ~360 / 330 / 300 / 250 V nodes; see the doc for what is measured vs
         estimated).

    Controls, page 1: Volume, Tone (the real amp's instrument-channel controls). Page 2 (synthetic): Power Drive (a
    master ahead of the phase inverter), Bias (the shared output-tube cathode resistor), Tube Feel (supply sag),
    Speaker (4 / 8 / 16 ohm on the 8 ohm tap), Output.
*/
class GibsonEH150StyleAmplifierProcessor : public EffectProcessor
{
public:
    GibsonEH150StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Gibson EH-150-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3d5a2b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** ~15 W class-A-ish into the 8 ohm tap: 15 V peak speaker volts to full scale. */
    static constexpr double outputScale = 1.0 / 15.0;

    /** Full-order netlist keeps the original level mapping -- the rescale below only compensates the reduced path. */
    static constexpr double fullOutputScale = outputScale;

    // ---- reduced-order power stage ----
    /** Behavioural replacement for the 6N7 paraphase / 6L6 pair / output transformer / speaker netlist (see
        docs/circuits/GibsonEH150.md, "reduced-order power stage"), default off so the internal-probe tests always
        see the full topology; the shipped build turns it on once in EffectRegistry, same as the other heavy amps. */
    static inline bool reducedOrder = false;

    // drive -> speaker small-signal gain at the nominal rail, output knee height as a fraction of the sagged
    // rail, knee sharpness, and the fitted first-order shelf standing in for the transformer + speaker response
    // (EH150_POWERCAL in the test file).
    static constexpr double bmGain0 = 5.9692;
    static constexpr double bmYmax = 0.04142;
    static constexpr double bmKneeN = 4.0;
    static constexpr double bmShelfHz = 95.0;
    static constexpr double bmShelfHfGain = 1.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { stage1Plate, stage1Cathode, stage2Plate, stage2Cathode, toneOut,
                       piPlateA, piPlateB, piCathode, powerGridA, powerGridB, powerPlateA, powerPlateB,
                       powerCathode, speaker, biasNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }
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
        int rVolTop = 0, rVolBot = 0, rTone = 0;
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate2 = 0, pK2 = 0, pOut = 0;
        double outDc = 0.0;

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rBias = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wPiGrid = 0, wPiPlateA = 0, wPiPlateB = 0, wPiK = 0, wGridA = 0, wGridB = 0, wPP1 = 0, wPP2 = 0,
                           wK = 0, wOut = 0;

        // supply
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;

        // reducedOrder only: behavioural power stage state (see behavioralPowerStage())
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
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
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    /** reducedOrder only: envelope-following sag + saturating gain + fitted shelf standing in for the 6N7
        paraphase, the 6L6 pair, the output transformer and the speaker. Returns speaker volts. */
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
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTone, smoothedPower, smoothedBias, smoothedFeel, smoothedOutput;

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
