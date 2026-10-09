#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Marshall 1959 Super Lead-style guitar amplifier (the 100 W "plexi"), modelled at component level on NodalCircuit from
    Marshall's own circuit drawings of the 1959SLP reissue (drawing 1959-01-60-02, 2002, and 59X-60-02, 1993;
    docs/circuits/SuperLead1959.md -- read that file to understand this processor).

    Same three-part structure as the Bassman-style amplifier (a preamp block, a power block, a small supply model), and the
    same solver machinery around them; what is different is the circuit:
      1. the preamp -- V1's two triodes (each channel has its OWN 68k stopper, cathode network and plate load: Channel I / Bright has
         a small 680 nF cathode bypass and a 3n3 coupling cap with a 4n7 bright cap across its Loudness pot, Channel II / Normal a
         330 uF bypass and a 22 nF coupling), the two 470k mixing resistors into V2A, and V2B, the cathode follower (an ideal
         follower here);
      2. the Marshall bass / middle / treble stack, the V3 long-tailed-pair phase inverter (cathode tail 470 + 10k into the
         feedback node), four EL34s as two PAIRS (each pair is one pentode with twice the current: identical tubes on the same
         nodes), the output transformer, and the 47k global feedback from the 16 ohm terminal into the Presence network;
      3. the solid-state rectifier, choke and the chain of filter nodes (plates, screens, phase inverter, V2, V1).

    Controls, page 1: Input (Normal / Jumped / Bright: which channel's jack(s) the guitar is patched into), Loudness I (the
    bright channel), Loudness II (the normal channel), Treble, Middle, Bass, Presence, Page 2 (synthetic, plus Output -- a plug-in level
    control; the real amp has no master volume): Power Drive (a master volume before the tone stack), Bias (the trimmer on the bias
    supply) and Tube Feel (how much the supply sags and how little negative feedback there is: 0 = stiff, 1 = the real amp) and Speaker
    (the load: 4 / 8 / 16 ohm, a real speaker with its voice-coil inductance and cone resonance, on the transformer's 16 ohm tap).
*/
class SuperLeadStyleAmplifierProcessor : public EffectProcessor
{
public:
    SuperLeadStyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Super Lead-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd8c27a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 16 ohm tap) is scaled by this to get a signal level: 100 W into 16 ohm is 57 V peak. */
    static constexpr double outputScale = 1.0 / 56.0;

    // ---- reduced-order power stage (2026-09-27) ----
    // The phase inverter + power tubes + output transformer are the sole source of this amp's expensive, variable-cost Newton
    // tail (confirmed: opening the global feedback loop did NOT remove it, so it is the tube devices' own dynamics under
    // extreme drive, not the loop coupling -- see docs/circuits/NodalCircuitSolver.md). A CONSTANT-cost behavioural
    // replacement is a deliberate, user-approved exception to this project's usual circuit-fidelity-over-ease rule (see
    // memory `circuit-modeling-fidelity-priority`), fitted to REAL numbers measured from the full reference model (not a
    // guessed shape), and kept OFF by default until heard, same as HM2StyleDistortionProcessor::reducedOrder.
    // Boundary: the tone stack (Treble/Middle/Bass, a real linear circuit) is UNCHANGED and always solved; only what comes
    // after `toneStackOut` -- phase inverter, power tubes, transformer, negative feedback, Presence, the physical speaker
    // load -- is replaced by `behavioralPowerStage()`. Full detail, the calibration sweep, and known gaps (Presence and the
    // speaker's own resonance are NOT reproduced by this path yet): docs/circuits/SuperLead1959.md.
    // Default false here so every OTHER test in this file (internal probes: bias, phase inverter, feedback, THD, speaker
    // resonance -- none of which exist once reducedOrder builds the tone-stack-only circuit) keeps working unchanged; the
    // shipped app turns this on centrally in EffectRegistry.cpp's factory, not here. User listened (2026-09-27) to the
    // TS808+Super Lead preset with this on and approved it as the real default.
    static inline bool reducedOrder = false;
    static constexpr double bmGain0 = 9.5;   // closed-loop small-signal gain, toneStackOut -> speaker, at the nominal rail
    static constexpr double bmYmax = 0.198;  // peak output as a fraction of the (possibly sagged) rail, at full saturation
    static constexpr double bmKneeN = 6.0;   // knee sharpness of the saturating curve (fitted, see the .cpp)
    // The removed phase inverter / power tubes / output transformer / feedback loop have their OWN frequency response
    // beyond the tone stack (Miller capacitances, OT bandwidth, the loop's own frequency-dependent gain) -- a real,
    // measured effect (2026-09-27, found by PedalUnityLevelTests failing at "noon"): the full reference model's
    // small-signal gain is ~2-4 dB lower from 500 Hz-1.65 kHz than at 100 Hz. A flat memoryless curve has none of this, so
    // a high-shelf cut restores it approximately (not exactly -- the real curve dips more than a single shelf can match,
    // see the .cpp for the measured points). Fitted, not guessed: see behavioralPowerStage()'s own comment.
    static constexpr double bmShelfHz = 90.0;
    static constexpr double bmShelfHfGain = 0.55; // ~-5.2 dB above the shelf

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { mixNode, channelIPlate, channelIIPlate, gainStagePlate, followerOut, toneStackOut, phaseInverterGrid, phaseInverterPlateA,
                       phaseInverterPlateB, phaseInverterTail, powerPlateA, powerPlateB, powerGridA, speaker, biasNode, feedbackNode };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    /** Supply rails at the last update. */
    double railPlates() const noexcept { return channels[0].supply.voltage (channels[0].sA); }
    double railScreens() const noexcept { return channels[0].supply.voltage (channels[0].sB); }
    double railPhaseInverter() const noexcept { return channels[0].supply.voltage (channels[0].sC); }
    double railV2() const noexcept { return channels[0].supply.voltage (channels[0].sD); }
    double railV1() const noexcept { return channels[0].supply.voltage (channels[0].sE); }
    double debugIterations (int block) const noexcept;
    int debugLastPowerIterations() const noexcept; // diagnostic: where does a rare expensive sample's cost actually go
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    long long debugSanityRejects() const noexcept { return sanityRejects; }
    double debugWorstSaneVolts() const noexcept { return worstSaneVolts; }
    int debugRecoveries() const noexcept { return recoveries; }
    /** Whether channel 0's last processed sample was solved (and sane). */
    bool debugLastSampleOk() const noexcept { return lastSampleOk; }
    // A wall-clock "real-time guard" (cut a late block to a few Newton iterations) was tried and REMOVED 2026-09-27: the worst
    // reference block already measures up to ~68% of the block's real-time budget on this machine (above the guard's own
    // trigger threshold), so it fired on ordinary hot playing, not just emergencies -- and once it fires, 3 iterations is not
    // enough to keep the power stage sane, so it caused MORE sanity rejects/recoveries than doing nothing (confirmed: the
    // "hot pedal into ONE channel" test went from 0 recoveries to 500+ with the guard active, 0 with it removed). See
    // docs/circuits/NodalCircuitSolver.md and docs/circuits/SuperLead1959.md for the measurements and what was tried instead.
    /** Test hook: replaces the speaker by a plain resistor (a pure resistive load, to compare the real speaker against). */
    void debugSetResistiveLoad (double ohms);
    /** Test hook: the 47k negative-feedback resistor (a huge value opens the loop). */
    void debugSetFeedbackResistance (double ohms);
    /** Total plate / screen current of the four output tubes at the last solved sample. */
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
        int pSrcV1 = 0, pSrcV2 = 0, pSrcInNormal = 0, pSrcInBright = 0;
        int rVolTop[2] {}, rVolBot[2] {};              // [0] Loudness II (normal), [1] Loudness I (bright)
        NodalCircuit::Node pMix = 0, pPlate2 = 0, pFollower = 0, pPlateBright = 0, pPlateNormal = 0;
        double followerDc = 0.0;

        // power section
        int wSrcCf = 0, wSrcPi = 0, wSrcCt = 0, wSrcBias = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        int rFeedback = 0, rTrebleTop = 0, rTrebleBottom = 0, rBass = 0, rMidTop = 0, rMidBottom = 0, rPresTop = 0, rPresBottom = 0, rBiasTrim = 0;
        int penA = 0, penB = 0;
        NodalCircuit::Node wToneIn = 0, wOut = 0, wPlateA = 0, wPlateB = 0, wGridA = 0, wTail = 0, wTone = 0, wPP1 = 0, wPP2 = 0, wPowerGridA = 0,
                           wBias = 0, wFeedback = 0;
        double screenDropA = 0.0, screenDropB = 0.0;
        double vScreen = 470.0;

        // reducedOrder only: behavioural power stage state (see behavioralPowerStage()); bmRail is set to the real
        // nominal rail in prepare().
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0, bmToneState = 0.0;

        // supply
        int iA = 0, iB = 0, iC = 0, iD = 0, iE = 0, srcVoc = 0;
        int rRect = 0;
        NodalCircuit::Node sA = 0, sB = 0, sC = 0, sD = 0, sE = 0;
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
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0; // loudness compensation for the 4 / 8 ohm loads on the 16 ohm tap
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    long long sanityRejects = 0;
    double worstSaneVolts = 0.0;
    Knobs lastKnobs {};
    double idleSupplyCurrent = 0.0;
    double feedbackOverride = 0.0; // test hook: > 0 replaces the feedback resistor (Tube Feel no longer sets it)
    void updateSupply (Channel& ch) const;
    /** reducedOrder only: the toneStackOut -> speaker transfer, fitted to the full reference model (see the header's
        "reduced-order power stage" comment and the .cpp for the calibration data). Updates ch.bmRail/ch.bmEnvelope
        (a slow envelope follower driving a measured sag lookup) and returns this sample's speaker-equivalent voltage. */
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
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

    juce::SmoothedValue<float> smoothedVolNormal, smoothedVolBright, smoothedTreble, smoothedMiddle, smoothedBass,
        smoothedPresence, smoothedOutput, smoothedPower, smoothedBias, smoothedFeel;

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
    bool lastSampleOk = true;

    static constexpr int controlInterval = 16;
    static constexpr int supplyInterval = 8;
    static constexpr int restRefreshInterval = 512; // ~10.7 ms at 48 kHz: how often the known-good recovery snapshot is refreshed
};

} // namespace openguitarmultifx
