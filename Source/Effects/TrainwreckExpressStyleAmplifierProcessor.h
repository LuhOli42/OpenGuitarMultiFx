#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Trainwreck Express-style guitar amplifier (Ken Fischer's hand-built Express, the Liverpool's sibling), modelled at
    component level on NodalCircuit -- docs/circuits/TrainwreckExpress.md, read that file to understand this processor.

    Three parts, the same structure as the AC15-style amplifier:
      1. the preamp -- three cascaded 12AX7 gain stages (V1B 100k / 1k5 + 22 uF, the single 1M Volume pot, V1A 100k / 2k7
         with a partial bypass, V2A 100k / 10k unbypassed: the cold "clipper"), then the Trainwreck tone stack driven straight
         from V2A's plate (no cathode follower: an "anode-driven" stack);
      2. the V3 long-tailed-pair phase inverter (82k / 100k plates, 470 + 22k tail), two EL34s on 220k grid leaks into a
         FIXED bias supply, the 5k plate-to-plate output transformer -- and NO global negative feedback;
      3. a solid-state rectifier and filter chain (plates, screens, phase inverter, preamp).

    The Komet Concorde (KometConcordeStyleAmplifierProcessor, docs/circuits/KometConcorde.md) is the same family and the same
    solver; it is built from this class with Model::concorde (its own preamp order, cathode follower, Hi-Cut and 4k8 OT).

    Controls, page 1: Volume, Treble, Middle, Bass, Presence (+ Hi-Cut and Touch on the Concorde). Page 2 (synthetic, plus
    Output -- a plug-in level control; neither real amp has a master volume): Power Drive (a master ahead of the phase
    inverter), Bias (the fixed-bias trimmer), Tube Feel (supply sag: 0 = stiff, 1 = the real amp) and Speaker (4 / 8 / 16 ohm,
    a real speaker on the transformer's 16 ohm tap).
*/
class TrainwreckExpressStyleAmplifierProcessor : public EffectProcessor
{
public:
    TrainwreckExpressStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Trainwreck Express"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6b1d1d); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 16 ohm tap) is scaled by this to get a signal level: ~45 W into 16 ohm is
        38 V peak. */
    static constexpr double outputScale = 1.0 / 38.0;     // reducedOrder speaker-level mapping (below)
    static constexpr double fullOutputScale = 1.0 / 38.0; // full netlist

    /** Ships reducedOrder in the app: the full LTP PI + 2x EL34 + OT netlist (no NFB) is the calibration
        reference, the behavioural stage below is what the A7S's eco tier runs. EffectRegistry.cpp flips
        this on centrally; the unit tests reset it to false so they keep probing the reference model.
        Per-model constants live in Spec (the Express and the Concorde share the class); fitted
        (TWX_POWERCAL) against each model's own stabilised reference. */
    static inline bool reducedOrder = false;
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 80.0;    // speaker-magnetics low-shelf pole
    static constexpr double bmShelfHfGain = 0.55;
    static constexpr double bmDcHz = 9.0;        // OT low-frequency saturation rolloff

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { stage1Plate, stage1Cathode, stage2Plate, stage2Cathode, stage3Plate, stage3Cathode, follower, toneOut,
                       piGrid, piPlateA, piPlateB, piCathode, powerGridA, powerGridB, powerPlateA, powerPlateB, speaker, biasNode,
                       powerInput };
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
    /** Plate current of each output tube at the last solved sample. */
    double plateCurrentA() const noexcept;
    double plateCurrentB() const noexcept;
    bool isConcorde() const noexcept { return model == Model::concorde; }

protected:
    enum class Model { express, concorde };
    explicit TrainwreckExpressStyleAmplifierProcessor (Model m);

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
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate2 = 0, pK2 = 0, pPlate3 = 0, pK3 = 0, pFollower = 0, pOut = 0;
        double outDc = 0.0;

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rPresence = 0, rHiCut = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wPiGrid = 0, wPiPlateA = 0, wPiPlateB = 0, wPiK = 0, wGridA = 0, wGridB = 0, wPP1 = 0, wPP2 = 0,
                           wBias = 0, wOut = 0;

        // supply: rectifier -> plate rail (sA) -> screen node (sB); the phase-inverter / preamp taps are RC-filtered from sB
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;

        // reducedOrder behavioural-stage state (EffectRegistry.cpp flips reducedOrder on for the
        // shipping build): the fitted power-stage envelope/rail, the latest sample output and the
        // tone/dc/presence filters that shape it.
        NodalCircuit::Node wTone = 0; ///< survives in the reduced path -- the PI coupling cap's input node.
        double bmRail = 0.0;      // latest sag-managed plate-rail estimate, volts
        double bmEnvelope = 0.0;  // |input| follower, volts
        double bmOutput = 0.0;
        double bmToneState = 0.0;
        double bmDcState = 0.0;
        double bmPresState = 0.0;   // presence HF-shelf lowpass state (reducedOrder only)
        double bmCutState = 0.0;    // Hi-Cut lowpass state (reducedOrder, Concorde)
        tubeamp::PowerStageTail bmTail;
    };

    struct Spec;
    const Spec& spec() const noexcept;

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, treble, middle, bass, presence, hiCut, powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    /** reducedOrder: sag-managed plate-rail estimate from the envelope follower (fitted, TWX_POWERCAL). */
    double sagRail (double envelope) const noexcept;
    /** reducedOrder: fitted power-stage surrogate -- the envelope-scaled knee + the OT/speaker shaping
        all the removed blocks used to provide. process() calls it with the PI input drive. */
    double behavioralPowerStage (Channel& ch, double driveVoltage, double bias, double feel) const noexcept;

    const Model model;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* hiCutParam = nullptr;  // Concorde only
    juce::AudioParameterFloat* touchParam = nullptr;  // Concorde only: Fast / Gradual
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedMiddle, smoothedBass, smoothedPresence, smoothedHiCut,
        smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0, failuresPre = 0, failuresPower = 0;
    bool dcOk = false;
    bool lastSampleOk = true;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
