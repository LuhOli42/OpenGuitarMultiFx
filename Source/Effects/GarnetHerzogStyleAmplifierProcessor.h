#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Garnet Herzog-style tube overdrive unit (the little 1960s Canadian box Randy Bachman chained into a second amp's
    input), modelled at component level on NodalCircuit -- docs/circuits/GarnetHerzog.md, read that file to understand
    this processor.

    Three parts, the same structure as the Trainwreck Express-style amplifier:
      1. the preamp -- two cascaded 12AX7 gain stages (V1A 220k / 1k5 + 25 uF, the 1M Input Volume pot between them,
         V1B 100k / 1k5 + 25 uF), with the unit's Deep switch adding a second .022 uF coupling cap in parallel;
      2. a single-ended 6V6 power stage (470R + 25 uF cathode, screen at the rail) driving the Champ-type output
         transformer -- whose secondary feeds the ~6 ohm / 10 W dummy LOAD resistor, not a speaker: the Herzog is a
         load-box, and the Output Volume pot + 150k series resistor attenuate that back down to instrument level;
      3. the silicon-diode supply (320 / 315 / 295 V nodes on the Garnet drawing).

    Controls, page 1: Volume (input volume), Deep, Level (output volume). Page 2 (synthetic): Power Drive (a master
    ahead of the 6V6 grid), Tube Feel (supply sag), Output (a plug-in level control; the real unit's Level already
    shapes the tap).
*/
class GarnetHerzogStyleAmplifierProcessor : public EffectProcessor
{
public:
    GarnetHerzogStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Garnet Herzog-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a4a12); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The Herzog's output is already instrument-level: the ~7 V peak the dummy load reaches is attenuated by the
        output pot + 150k / next-amp-input divider, leaving a few volts -- scale ~6 V to full scale. */
    static constexpr double outputScale = 1.0 / 6.0;

    /** Full-order netlist keeps the original level mapping -- the rescale below only compensates the reduced path. */
    static constexpr double fullOutputScale = outputScale;

    // ---- reduced-order power stage ----
    /** Behavioural replacement for the 6V6/SE-transformer/load netlist (see docs/circuits/GarnetHerzog.md,
        "reduced-order power stage"), default off so the internal-probe tests always see the full topology; the
        shipped build turns it on once in EffectRegistry, same as the other heavy amps. */
    static inline bool reducedOrder = false;

    // drive -> output-tap small-signal gain at the nominal rail, output knee height as a fraction of the sagged
    // rail, knee sharpness, and the fitted first-order shelf standing in for the transformer + load response
    // (HZG_POWERCAL in the test file).
    static constexpr double bmGain0 = 1.0162;
    static constexpr double bmYmax = 0.02346;
    static constexpr double bmKneeN = 5.0;
    static constexpr double bmShelfHz = 110.0;
    static constexpr double bmShelfHfGain = 1.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { stage1Plate, stage1Cathode, stage2Plate, stage2Cathode, powerGrid, powerPlate, powerCathode,
                       load, output, biasNode };
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
    double plateCurrent() const noexcept;

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
        int rVolTop = 0, rVolBot = 0;
        int capDeep = 0;
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate2 = 0, pK2 = 0;
        double outDc = 0.0;

        // power section: 6V6 -> SE transformer -> 6 ohm load -> output pot + 150k
        int wSrcIn = 0, wSrcRail = 0;
        int rLevelTop = 0, rLevelBot = 0;
        int pen = 0;
        NodalCircuit::Node wGrid = 0, wPP = 0, wK = 0, wLoad = 0, wOut = 0;

        // supply: diodes -> reservoir (plates) -> 1k -> screens -> 10k -> preamp tap
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double preRail = 0.0;

        // reducedOrder only: behavioural power stage state (see behavioralPowerStage())
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs { double volume, deep, level, powerDrive, tubeFeel; };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    /** reducedOrder only: envelope-following sag + saturating gain + fitted shelf standing in for the 6V6, the
        single-ended transformer, the dummy load and the output divider. Returns post-divider volts (the wOut
        equivalent) at Level = 1; process() applies the Level taper itself. */
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* deepParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedDeep, smoothedLevel, smoothedPower, smoothedFeel, smoothedOutput;

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
