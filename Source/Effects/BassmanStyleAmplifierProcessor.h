#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Fender Bassman 5F6-A-style guitar amplifier, modelled at component level on NodalCircuit from the amp's
    schematic (see docs/circuits/Bassman5F6A.md -- read that file to understand this processor).

    Two solver blocks plus a small power-supply model:
      1. the preamp -- both channels' 12AY7 triodes (shared cathode), their volume pots and the 270k mixing
         resistors into the 12AX7 gain stage, whose plate drives the cathode follower (an ideal follower here);
      2. everything the global feedback loop passes through -- the follower's output impedance, the passive bass /
         middle / treble tone stack, the 12AX7 long-tailed-pair phase inverter, the two 6L6/5881 beam tetrodes with
         their grid leaks, screens and fixed bias, the output transformer (coupled inductors) and the 27k negative
         feedback into the phase inverter, shaped by the presence control;
      3. the rectifier / choke / filter-capacitor supply, whose sag under load is a large part of the character.

    Controls, page 1: Input (Normal / Jumped / Bright -- which front-panel jack(s) the guitar is patched into; each
    channel's own grid stopper returns to its OWN jack now, so leaving one disconnected really does silence its Volume
    knob, matching the real amp with nothing plugged into it), Volume (Normal), Volume (Bright), Treble, Middle, Bass,
    Presence, Page 2 (synthetic, plus Output -- a plug-in level control; the real amp has no master volume): Power Drive (a master volume between the phase inverter and the power
    tubes), Bias (the -48 V grid supply), Tube Feel (how much the supply sags and how little negative feedback there
    is: 0 = stiff and solid-state-like, 1 = the real amp) and Speaker (the load: 4 / 8 / 16 ohm, a speaker with its
    voice-coil inductance and cone resonance, so the presence and the bass "thump" interact with the amp).
*/
class BassmanStyleAmplifierProcessor : public EffectProcessor
{
public:
    BassmanStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Bassman-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc9a24a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 18.8; // 8 ohm secondary level mapping -- rescaled with the reduced
                                                // power stage's retuned drive law (keeps the unity trim the registry was calibrated for)

    // ---- reduced-order power stage (2026-09-27) ----
    // Same mechanism as SuperLeadStyleAmplifierProcessor (see that header's own note for the full reasoning, the
    // calibration methodology and the found/fixed speaker-level bug): the phase inverter, power tubes, output transformer,
    // global feedback and physical speaker are replaced by a fitted, constant-cost curve; the tone stack stays real and
    // always solved. Default false here so every OTHER test in BassmanStyleAmplifierProcessorTests.cpp keeps working
    // unchanged; EffectRegistry.cpp turns it on centrally for the real app. docs/circuits/Bassman5F6A.md has the
    // calibration data and verified numbers.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 2.00;   // closed-loop small-signal gain, toneStackOut -> speaker, at the nominal rail
    static constexpr double bmYmax = 0.092;  // peak output as a fraction of the (possibly sagged) rail, at full saturation
    static constexpr double bmAsym = 0.09;   // push-pull clip lopsidedness (earlier saturation toward grid conduction)
    static constexpr double bmGridClampV = 24.0; // LTP grid-conduction clamp on the delivered drive (see .cpp)
    static constexpr double bmDcHz = 8.0;    // OT DC-block on the asymmetric stage output (real transformer cannot pass DC)
    // Same reasoning as SuperLeadStyleAmplifierProcessor's own note: the removed PI/power tubes/OT/feedback loop have a
    // real frequency response beyond the tone stack that a flat memoryless curve lacks -- restored approximately with a
    // high-shelf cut, fitted against the full reference model's own measured gain at several frequencies (see the .cpp).
    static constexpr double bmShelfHz = 70.0;
    static constexpr double bmShelfHfGain = 0.53; // ~-5.5 dB above the shelf   // knee sharpness of the saturating curve (fitted, see the .cpp)

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { mixNode, brightPlate, gainStagePlate, followerOut, toneStackOut, phaseInverterGrid, phaseInverterPlateA, phaseInverterPlateB,
                       phaseInverterTail, powerPlateA, powerPlateB, powerGridA, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    /** Supply rails at the last update: plates (B+), screens, phase inverter, preamp. */
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPhaseInverter() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railPreamp() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    /** Development/test hook: the 27k negative-feedback resistor (a huge value opens the loop). */
    void debugSetFeedbackResistance (double ohms);
    /** Test hook: replaces the speaker by a plain resistor (the condition of Kuehnel's published measurements). */
    void debugSetResistiveLoad (double ohms);
    double debugIterations (int block) const noexcept;
    /** Failed solves of channel 0's preamp / power block since prepare(). */
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    /** Samples the solver CONVERGED on but the speaker-voltage sanity check rejected, and the largest such
        excursion seen -- these hold the output for a sample, which is a step in the waveform (a click). */
    long long debugSanityRejects() const noexcept { return sanityRejects; }
    double debugWorstRejectedVolts() const noexcept { return worstRejectedVolts; }
    /** Largest speaker voltage among samples the sanity check ACCEPTED -- i.e. how far the legitimate signal
        actually swings, which is where any output ceiling has to sit above. */
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    int debugRecoveries() const noexcept { return recoveries; } // average Newton iterations per sample: 0 preamp, 1 power section
    double plateCurrentTotal() const noexcept;
    double screenCurrentTotal() const noexcept;

private:
    struct Channel
    {
        NodalCircuit pre, power, supply;
        // A recent KNOWN-GOOD state (memory only), restored if the solvers lose the plot. Seeded at prepare() (the settled idle
        // point) and then refreshed every restRefreshInterval samples while the channel is converging cleanly, so a recovery
        // during a loud held note lands back near what the amp was actually doing, not back at silence (see recover()).
        NodalCircuit::DynamicState preRest, powerRest, supplyRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        // recover() restores the rest state; the output is kept continuous across it (a decaying offset), so what was a
        // pop is a short thump at most.
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp
        int pSrcVcc = 0, pSrcInNormal = 0, pSrcInBright = 0;
        int rVolTop[2] {}, rVolBot[2] {};              // [0] normal, [1] bright
        NodalCircuit::Node pMix = 0, pPlate2 = 0, pFollower = 0, pBrightPlate = 0;
        double followerDc = 0.0;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rRect = 0, rSpkRe = 0, rSpkRp = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMid = 0, rPresTop = 0, rPresBottom = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wSlope = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0, wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 450.0;

        // reducedOrder only: behavioural power stage state (see behavioralPowerStage())
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // follower-drive conditioner state (notch + lowpass on the follower AC before it couples into
        // the power block -- removes the preamp solve's fs/2 weave artifact; see process())
        double cfDrivePrev = 0.0, cfDriveLp = 0.0, cfDriveDc = 0.0;
        double bmDcState = 0.0;

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, srcVoc = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0;
        double sumPlate = 0.0, sumScreen = 0.0;
        int sumCount = 0;
        int supplyCounter = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volNormal, volBright, treble, middle, bass, presence, powerDrive, bias, tubeFeel;
        int speaker; // 0 = 4 ohm, 1 = 8, 2 = 16
    };
    void updatePots (const Knobs& k);
    void applySpeaker (Channel& ch, int index) const;
    void recover (Channel& ch) const;
    mutable int recoveries = 0;
    long long sanityRejects = 0;
    double worstRejectedVolts = 0.0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double speakerGain = 1.0; // loudness compensation for the 4 / 16 ohm loads
    double idleSupplyCurrent = 0.0;
    bool resistiveLoadForced = false;
    double feedbackOverride = 0.0; // test hook: > 0 replaces the feedback resistor (Tube Feel no longer sets it)
    void updateSupply (Channel& ch) const;
    /** reducedOrder only: see SuperLeadStyleAmplifierProcessor::behavioralPowerStage() for the mechanism. */
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volNormalParam = nullptr;
    juce::AudioParameterFloat* volBrightParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* inputParam = nullptr;

    juce::SmoothedValue<float> smoothedVolNormal, smoothedVolBright, smoothedTreble, smoothedMiddle, smoothedBass,
        smoothedPresence, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;
    int appliedSpeaker = -1;

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
    bool channel1Stale = false;
    bool channelsSynced = true;
    long long identicalRun = 0;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512; // ~10.7 ms at 48 kHz: how often the known-good recovery snapshot is refreshed
};

} // namespace openguitarmultifx
