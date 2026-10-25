#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Ampeg V4B-style bass amplifier, modelled at component level on NodalCircuit from the amp's
    1971 factory schematic (see docs/circuits/AmpegV4B.md -- read that file to understand this processor).

    Two solver blocks plus a small power-supply model, all from the factory V-4B drawing (4010267, 5-71):
      1. the preamp -- V1a, the Ultra-Lo ladder, the 1M linear Volume with Ultra-Hi, V1b, the bass / treble
         network, the 6K11 active mid stage (L101 + the 300 / 800 / 3000 Hz selector on the 50k Mid-Range pot)
         and the 12DW7's 12AX7 cathode follower to the Ext Amp jacks;
      2. everything the global feedback loop passes through -- the 12DW7's 12AU7 half (R34 NFB onto its 33 ohm
         cathode resistor), the 12AU7 long-tailed pair, the four 7027As as two banks on -64 V fixed bias, the
         output transformer (coupled inductors) and the speaker;
      3. the bridge supply (545 V plates, R55 to the 540 V screens, the 360 / 325 V small-signal rails).

    Controls, page 1: Input (a synthetic 0 / -15 dB pad), Volume, Ultra-Lo, Ultra-Hi (the faceplate rocker
    switches), Bass, Mid + Mid Frequency (SW3: 300 / 800 / 3000 Hz), Treble, Master (synthetic: a linear level
    into the power amp). Page 2 (synthetic, plus Output -- a plug-in level control): Power
    Drive (a master volume between the preamp and the power amp), Bias (the -64 V bias rail, hot/cold), Tube Feel (how much the supply sags and how little negative
    feedback there is: 0 = stiff and solid-state-like, 1 = the real amp) and Speaker (2 / 4 / 8 ohm, a
    speaker with its voice-coil inductance and cone resonance).
*/
class AmpegV4BStyleAmplifierProcessor : public EffectProcessor
{
public:
    AmpegV4BStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "V4B"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc94a5a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 116.5; // reducedOrder level mapping // 100 W into 4 ohm is ~20 Vrms at the terminal
    static constexpr double fullOutputScale = 1.0 / 30.0; // reference-model level mapping (see outputScale)

    /** Ships reducedOrder in the app (the full push-pull + OT + NFB netlist is the calibration
        reference -- same pattern as BassmanStyleAmplifierProcessor). EffectRegistry.cpp flips this
        on centrally; the unit tests reset it to false so they keep probing the reference model. */
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 120.0;     // V3b + LTP + push-pull + OT closed-loop gain (fitted)
    static constexpr double bmYmax = 0.078;      // fraction of the sag rail reachable at the speaker
    static constexpr double bmAsym = 0.09;       // push-pull clip asymmetry
    static constexpr double bmGridClampV = 24.0; // LTP input where grid conduction clamps
    static constexpr double bmDcHz = 8.0;        // OT low-frequency saturation rolloff
    static constexpr double bmShelfHz = 70.0;    // speaker-magnetics low-shelf pole
    static constexpr double bmShelfHfGain = 0.53;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { firstPlate, secondPlate, toneStackOut, recoveryPlate, midNode, followerOut,
                       phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB, phaseInverterTail,
                       driverPlateA, powerGridA, powerPlateA, powerPlateB, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    /** Supply rails at the last update: plates (B+), screens, the long-tailed pair's 360 V, the 325 V preamp rail. */
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railDriver() const noexcept { return channels[0].driverRail; }
    double railPreamp() const noexcept { return channels[0].preRail; }
    /** Development/test hook: replaces the negative-feedback resistor (a huge value opens the loop). */
    void debugSetFeedbackResistance (double ohms);
    /** Test hook: replaces the speaker by a plain resistor. */
    void debugSetResistiveLoad (double ohms);

    double debugIterations (int block) const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    long long debugSanityRejects() const noexcept { return sanityRejects; }
    double debugWorstRejectedVolts() const noexcept { return worstRejectedVolts; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    int debugRecoveries() const noexcept { return recoveries; }
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

        // preamp
        int pSrcVcc = 0, pSrcVccLow = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0, rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0;
        int rUlDirect = 0, rUlIn = 0, rUlOut = 0, capUltraHi = 0, rUhBypass = 0;
        int rMidA = 0, rMidB = 0, rCoil = 0, grpMidL = 0, capMidC = 0;
        NodalCircuit::Node pPlate1 = 0, pPlate2 = 0, pTone = 0, pPlate3 = 0, pMid = 0, pFollower = 0;

        // power section
        int wSrcPre = 0, wSrcPi = 0, wSrcCt = 0, wSrcNeg = 0, wSrcVdr = 0;
        int rFeedback = 0;
        int rSpkRe = 0, rSpkRp = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wGridA = 0, wPlateA = 0, wPlateB = 0, wTail = 0, wDrvPlateA = 0,
                           wPowerGridA = 0, wPP1 = 0, wPP2 = 0, wOut = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 365.0;

        // supply
        int iA = 0, iB = 0, srcVoc = 0, rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;                 // plates (545 V), screens (540 V)
        double driverRail = 360.0, preRail = 325.0, lowRail = 318.0;
        double sumPlate = 0.0, sumScreen = 0.0;
        NodalCircuit::Node wTone = 0;
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0, bmDcState = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double input, gain, ultraLo, ultraHi, bass, middle, midFreq, treble, master;
        double powerDrive, bias, tubeFeel;
        int speaker; // 0 = 2 ohm, 1 = 4, 2 = 8
    };
    void updatePots (const Knobs& k);
    void applySpeaker (Channel& ch, int index) const;
    double sagRail (double envelope) const noexcept;
    double behavioralPowerStage (Channel& ch, double toneVoltage, double bias, double feel) noexcept;
    void applyMidFreq (Channel& ch, int index) const;
    void recover (Channel& ch) const;
    mutable int recoveries = 0;
    long long sanityRejects = 0;
    double worstRejectedVolts = 0.0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double speakerGain = 1.0;
    double idleSupplyCurrent = 0.0;
    bool resistiveLoadForced = false;
    double feedbackOverride = 0.0;
    void updateSupply (Channel& ch) const;
    void trackSmallRails (Channel& ch, double dt) const;

    std::array<Channel, 2> channels;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* ultraLoParam = nullptr;
    juce::AudioParameterFloat* ultraHiParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* midFreqParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedBass, smoothedMiddle, smoothedTreble,
        smoothedMaster, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;
    int appliedSpeaker = -1;
    int appliedMidFreq = -1;

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
