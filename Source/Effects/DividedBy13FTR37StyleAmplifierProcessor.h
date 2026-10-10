#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Divided by 13 FTR 37-style guitar amplifier (Fred Taccone's 37 W channel-blendable head: a 5879-pentode "Click"
    channel and a blackface-like clean channel mixed into a cathodyne-free LTP phase inverter and a fixed-bias
    4x6V6GT push-pull, GZ34-rectified), modelled at component level on NodalCircuit -- docs/circuits/DividedBy13FTR37.md,
    read that file to understand this processor.

    Three parts, the same structure as the Twin Reverb / Trainwreck models:
      1. the preamp -- two independent channels sharing one NodalCircuit: Channel 1 (the "Click" channel: a 5879
         sharp-cutoff pentode gain stage into a 12AX7, with the 6-position Tone click switch modelled as a switched
         shunt capacitor -- clockwise = more lows) and Channel 2 (the clean channel: a 12AX7 into a blackface-style
         Treble/Bass stack into a second 12AX7, whose cathode-bypass is the Volume knob's pull Mid+Gain boost). Both
         channels' volume wipers mix into pMix, like the real amp's shared power section;
      2. the power section -- a 12AX7 long-tailed-pair phase inverter, four fixed-bias 6V6GTs (the second tube of each
         push-pull pair is cathode-lifted by the Full/Half power switch -- 37 W vs 18 W), the output transformer, and
         no global negative feedback;
      3. a GZ34-style rectifier and filter chain (plates, screens, phase inverter, preamp).

    The built-in spring reverb is NOT modelled (a separate ReverbProcessor covers that ground, same standing rule as
    the Twin Reverb's own tank); the Reverb knob is therefore absent from the parameter list.

    Controls, page 1: Input (Channel 1 / Channel 2 / Both -- the real amp's separate input jacks), Ch1 Volume, Ch1 Tone
    (the six-position click switch), Ch2 Volume, Boost (the pull Mid+Gain), Ch2 Treble, Ch2 Bass, Power (Full / Half).
    Page 2 (synthetic, plus Output -- a plug-in level control; the real amp has no master volume): Power Drive (a
    master ahead of the phase inverter), Bias (the fixed-bias supply trim), Tube Feel (supply sag) and Speaker
    (4 / 8 / 16 ohm on the real amp's taps).
*/
class DividedBy13FTR37StyleAmplifierProcessor : public EffectProcessor
{
public:
    DividedBy13FTR37StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Divided by 13 FTR 37-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb03030); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 8 ohm tap) scaled to a signal level: 37 W into 8 ohm is ~17 V rms /
        ~24 V peak. The reduced-order stage emits the same speaker-volts scale. */
    static constexpr double outputScale = 3.0 / 26.0;
    static constexpr double fullOutputScale = 3.0 / 26.0;

    // ---- reduced-order power stage (same mechanism as TwinReverbStyleAmplifierProcessor's, see that header) ----
    // Boundary: the whole preamp (both channels + both tone stacks) stays a real solved circuit; the LTP PI, the four
    // 6V6GTs, the Full/Half switch, the OT and the physical speaker are replaced by behavioralPowerStage(). Default
    // false (the internal-probe tests assume the full topology); the shipped app turns it on in EffectRegistry.cpp.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 70.0;   // closed-loop small-signal gain, pMix -> speaker, at the nominal rail (F37_POWERCAL)
    static constexpr double bmYmax = 0.30;    // peak output as a fraction of the (sagged) rail at full saturation -- fitted
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.7;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { ch1Plate, ch1K2, ch2Plate1, ch2Plate2, mixNode, pOut,
                       piPlateA, piPlateB, piCathode, powerGridA, powerPlateA, powerPlateB, biasNode, speaker };
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
    /** Test hook: replaces the speaker by a plain resistor. */
    void debugSetResistiveLoad (double ohms);
    /** Total plate current of the 6V6 bank (amps) -- the Full/Half test reads it. */
    double plateCurrentTotal() const noexcept;

private:
    struct Channel
    {
        // The Click channel's 5879 pentode lives in its own block (preCh1): the EF86 fit's sharp knee makes the
        // joint 10-port Newton solve diverge from a cold start, and the AC15 -- the only other amp with a preamp
        // pentode -- gets away with it only because its EF86 is alone in its block. The two blocks exchange
        // Thevenin tap voltages per sample, the same boundary the preamp/power split already uses.
        NodalCircuit preCh1, pre, power, supply;
        NodalCircuit::DynamicState preCh1Rest, preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: preCh1 = Click channel (5879 + 12AX7 + Click tap), pre = Ch2 chain + the pMix summing node.
        // pSrcMixRef (in preCh1) injects the previous sample's pMix behind ch1's 100k mix resistor; pSrcCh1Tap
        // (in pre) injects ch1's tap behind its own 100k -- identical loading to one lumped circuit.
        int pSrcIn1 = 0, pSrcIn2 = 0, pSrcRail = 0, pSrcRail1 = 0, pSrcMixRef = 0, pSrcCh1Tap = 0;
        int rVol1Top = 0, rVol1Bot = 0, rVol2Top = 0, rVol2Bot = 0;
        int rTrebleTop = 0, rTrebleBot = 0, rBass = 0, rBoost = 0;
        int capClick = 0, penPre1 = 0;
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate1b = 0, pK1b = 0, pToneTap = 0;
        NodalCircuit::Node pPlate2a = 0, pK2a = 0, pPlate2b = 0, pK2b = 0, pBoostTap = 0, pMix = 0;
        double mixDc = 0.0;

        // power section
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rLiftA = 0, rLiftB = 0;
        int penA1 = 0, penA2 = 0, penB1 = 0, penB2 = 0;
        NodalCircuit::Node wPiGrid = 0, wPiPlateA = 0, wPiPlateB = 0, wPiK = 0, wGridA = 0, wGridB = 0, wBias = 0,
                           wPP1 = 0, wPP2 = 0, wKA2 = 0, wKB2 = 0, wOut = 0;

        // reducedOrder behavioural state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply: rectifier -> plate rail (sA) -> screen node (sB); PI / preamp taps are RC-filtered from sB
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;
    };

    void buildChannel (Channel& ch);
    // The 5879 pentode's sharp Koren knee makes the cold-start Newton solve over this 5-device preamp diverge;
    // buildPreamp is therefore run twice like the AC30's follower trick: once with a tame triode in the pentode's
    // slot purely to measure the DC neighbourhood, then again with the real device and those voltages as guesses.
    struct PreampSeed
    {
        double plate1 = 0.0, k1 = 0.0, plate1b = 0.0, k1b = 0.0;
        double plate2a = 0.0, k2a = 0.0, plate2b = 0.0, k2b = 0.0;
    };
    void buildPreampCh1 (NodalCircuit& c, Channel& ch, const PreampSeed* seed, bool triodeStandin);
    void buildPreamp (NodalCircuit& c, Channel& ch);
    void buildPower (Channel& ch);
    struct Knobs
    {
        double volume1, volume2, treble, bass, powerDrive, bias, tubeFeel;
        int input;     // 0 = Ch1, 1 = Ch2, 2 = Both
        int click;     // 0..5
        int boost;     // 0 off, 1 on
        int halfPower; // 0 Full, 1 Half
        int speaker;   // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    int appliedSpeaker = -1;
    int appliedClick = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* ch1VolumeParam = nullptr;
    juce::AudioParameterFloat* clickParam = nullptr;
    juce::AudioParameterFloat* ch2VolumeParam = nullptr;
    juce::AudioParameterFloat* boostParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* halfPowerParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedCh1Volume, smoothedCh2Volume, smoothedTreble, smoothedBass, smoothedOutput,
        smoothedPower, smoothedBias, smoothedFeel;

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
