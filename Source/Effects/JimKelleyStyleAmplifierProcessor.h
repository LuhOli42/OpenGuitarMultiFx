#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeAmpCommon.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Jim Kelley Single Channel Reverb-style guitar amplifier (the late-70s/80s boutique 60 W combo built by
    Jim Kelley in California -- a Dumble-adjacent design with its own topology), modelled at component level
    on NodalCircuit -- docs/circuits/JimKelley.md, read that file to understand this processor.

    Three parts, the same structure as the Carr Rambler / AC15 models:
      1. the preamp -- a 12AX7 input stage (V1a, 150k plate / 1k5 cathode), the single front-panel Gain pot
         (1M audio), a second 12AX7 (V1b) whose grid carries the famous pair of anti-parallel 1N914 signal
         diodes (~1.2 V soft clip, Kelley's own anti-blocking-distortion trick), a Baxandall-style
         active-shelving Treble/Bass stack, and a third 12AX7 (V3a, 68k/4k7) recovery stage;
      2. the power section -- V4a (a 12AX7 mixer triode on a 390k plate load whose cathode is the global-NFB
         injection point AND the Presence network's home), V4b the cathodyne (split-load) phase inverter
         biased by a 3M6/1M5 divider to the rail (grid ~110 V, matching the marked 265 V plate / ~110 V
         cathode), four 6V6GTAs in fixed-bias push-pull pairs (270k grid leaks to -50 V, shared 2k/10W screen
         feed), a ~4.3k plate-to-plate output transformer, and a 100k feedback wire off the 8 ohm speaker tap;
      3. a rectifier and filter chain (plates, screens, PI, preamp).

    The spring reverb is modelled BEHAVIOURALLY (a light allpass/comb tank emulation) rather than as a tank
    netlist: the send taps the preamp output like the real amp's 12AT7 driver, and the return is injected
    into V4a's grid through the real 470k mixer resistor. The Reverb knob is therefore real -- unlike the
    Carr Rambler/Deluxe Reverb which ship no reverb control, this amp's identity ("Single Channel Reverb
    Amplifier") is built around it.

    Controls, page 1: Gain (the post-V1a 1M audio pot), Volume (post-stack level), Treble, Bass, Reverb, and
    the real amp's pull-switches: Presence (on the Gain pot -- .1uF+1k5 shunt in the NFB tail at V4a's
    cathode), Bright (on the Treble pot -- ~120pF across the treble leg), Mid Boost (on the Bass pot --
    470pF across the Gain pot's top leg, per the owner's manual), and Power Select (60 W = 4x6V6 / 30 W =
    one pair disengaged). Page 2 (synthetic): Power Drive, Bias (the -50 V fixed-bias supply), Tube Feel
    (supply sag), Speaker (4 / 8 / 16 ohm on the 8 ohm tap), Output, Cab.
*/
class JimKelleyStyleAmplifierProcessor : public EffectProcessor
{
public:
    JimKelleyStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Jim Kelley"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb8442a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 8 ohm tap) scaled to a signal level: 60 W into 8 ohm is
        ~22 V rms / ~31 V peak. The reduced-order stage emits the same speaker-volts scale. */
    static constexpr double outputScale = 1.0 / 32.0;
    static constexpr double fullOutputScale = 1.0 / 32.0;

    // ---- reduced-order power stage (same mechanism as CarrRamblerStyleAmplifierProcessor's) ----
    // Boundary: the whole preamp (V1a, V1b + diode clamps, Baxandall stack, V3a) stays a real solved
    // circuit; V4a, the cathodyne PI, the four 6V6s, the OT and the physical speaker are replaced by
    // behavioralPowerStage(). Default false (tests probe the full topology); EffectRegistry turns it on.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 35.0;     // closed-loop small-signal gain pOut -> speaker (NFB amp) -- placeholder
    static constexpr double bmYmax = 0.30;      // peak output as a fraction of the sagged rail -- placeholder
    static constexpr double bmLevelTrim = 0.20; // placeholder pending this amp's own POWERCAL measurement
    static constexpr double bmKneeN = 6.0;
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.6;
    static constexpr double bmAsym = 0.0;
    static constexpr double bmDcHz = 8.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { stage1Plate, stage1Cathode, clipPlate, clipCathode, toneOut, recPlate, recCathode,
                       pOut, mixPlate, mixCathode, piPlate, piCathode, powerGridA, powerGridB,
                       powerPlateA, powerPlateB, biasNode, speaker, reverbRet };
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

        // preamp (V1a -> Gain -> V1b + clamps -> Baxandall -> V3a -> Volume)
        int pSrcIn = 0, pSrcRail = 0;
        int rGainTop = 0, rGainBot = 0, rTrebleTop = 0, rTrebleBot = 0, rBassTop = 0, rBassBot = 0;
        int rVolTop = 0, rVolBot = 0;
        int capBright = 0, capMidBoost = 0;
        NodalCircuit::Node pPlate1 = 0, pK1 = 0, pPlate2 = 0, pK2 = 0, pTone = 0, pPlate3 = 0, pK3 = 0,
                           pOut = 0;
        double outDc = 0.0, driveDc = 0.0, driveDcCoeff = 0.0;

        // behavioural spring tank (send taps pOut, return drives the 470k into V4a's grid)
        struct Spring
        {
            std::vector<double> buf1, buf2, buf3;
            int pos = 0;
            double damp = 0.0;
            void prepare (double sampleRate);
            void clear();
            double process (double x) noexcept;
        } spring;
        double revLp = 0.0;

        // power section (V4a mixer + V4b cathodyne + 4x 6V6GTA + OT + speaker + global NFB)
        int wSrcIn = 0, wSrcRail = 0, wSrcPi = 0, wSrcBias = 0, wSrcRev = 0;
        int rBiasTrim = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rPres = 0, rNfb = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wMixPlate = 0, wMixK = 0, wPiGrid = 0, wPiPlate = 0, wPiK = 0, wGridA = 0,
                           wGridB = 0, wPP1 = 0, wPP2 = 0, wBias = 0, wOut = 0, wRev = 0;

        // reducedOrder behavioural state
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0, bmDcState = 0.0;
        tubeamp::PowerStageTail bmTail; // OT + speaker reaction + even-order tail (TubeAmpCommon.h)

        // supply: rectifier -> plate rail (sA) -> screen node (sB); PI / preamp taps are RC-filtered from sB
        int iA = 0, iB = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        double fbPlate = 4.0 * 0.015, fbScreen = 4.0 * 0.0035; // ~100 ms-filtered draw, 4x6V6 idle estimate
        int sumCount = 0;
        int supplyCounter = 0;
        double piRail = 0.0, preRail = 0.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double gain, volume, treble, bass, reverb, powerDrive, bias, tubeFeel;
        int speaker;       // 0 = 4 ohm, 1 = 8, 2 = 16
        int presence, bright, midBoost, powerSel;
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    void updateSupply (Channel& ch) const;
    double preampOutput (const Channel& ch) const noexcept;
    double behavioralPowerStage (Channel& ch, double driveVoltage) const noexcept;

    int appliedSpeaker = -1, appliedBright = -1, appliedMidBoost = -1, appliedPresence = -1, appliedPowerSel = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* cabParam = nullptr;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* reverbParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* midBoostParam = nullptr;
    juce::AudioParameterFloat* powerSelParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedVolume, smoothedTreble, smoothedBass, smoothedReverb,
        smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

    /** JUCE's reset() must return the circuit to its DC operating point; prepare() no-ops on a same-rate
        re-prepare, so reset() re-arms the rate and forces the rebuild. Control thread only. */
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
