#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Marshall JCM800-style guitar amplifier (the 2203, 100 W master-volume lead head), modelled at component level on
    NodalCircuit from Marshall's own factory drawing ("2203 STD", schematicheaven.net; docs/circuits/JCM8002203.md --
    read that file to understand this processor). The power section (phase inverter, four EL34s as two pairs, output
    transformer, global feedback, Presence, supply) is structurally IDENTICAL to `SuperLeadStyleAmplifierProcessor`'s
    own (both are 100 W 4xEL34 Marshalls) and reuses that already-verified code and tube fit unchanged. What's
    genuinely different, and newly built here, is the PREAMP: unlike every other amp on this project's roadmap so
    far (two input channels, each with one gain stage, mixed together), the 2203 has a SINGLE input (High/Low
    sensitivity jacks) driving FOUR CASCADED gain stages in series (V1a -> Gain pot -> V1b -> V2a -> V2b, the last a
    cathode follower into the tone stack) -- the real source of this amp's much higher gain and "master volume lead"
    character, and why a dedicated Gain knob sits ahead of the tone stack instead of two per-channel Volume knobs.

    Controls, page 1 (the real 2203 panel): Input (High / Low sensitivity jack), Preamp (VR1), Treble, Middle, Bass,
    Presence, Master (VR2; parameter id j8_power). Page 2 (synthetic): Bias (the trimmer on the bias supply), Tube Feel
    (how much the supply sags and how little negative feedback there is: 0 = stiff, 1 = the real amp), Speaker
    (4 / 8 / 16 ohm, on the transformer's 16 ohm tap) and Output (a plug-in level control).
*/
class JCM800StyleAmplifierProcessor : public EffectProcessor
{
public:
    JCM800StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "JCM800-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
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
    // speaker's own resonance are NOT reproduced by this path yet): docs/circuits/JCM8002203.md.
    // Default false here so every OTHER test in this file (internal probes: bias, phase inverter, feedback, THD, speaker
    // resonance -- none of which exist once reducedOrder builds the tone-stack-only circuit) keeps working unchanged; the
    // shipped app turns this on centrally in EffectRegistry.cpp's factory, not here.
    static inline bool reducedOrder = false;
    // bmGain0/bmYmax/bmKneeN are DELIBERATELY still the Super Lead's own fitted numbers, not an independent J8_POWERCAL
    // fit -- see docs/circuits/JCM8002203.md's "A power-stage instability" section. Short version: the full-topology
    // reference model this fit would come from has its own unresolved silence self-oscillation; the tube-softening fix
    // that reaches genuine stability also collapses the reference's own absolute gain by ~100x AND leaves its
    // saturation curve visibly irregular (non-monotonic drive-vs-output through part of a J8_POWERCAL sweep) -- fitting
    // bmGain0/bmYmax to that data would bake a qualitatively wrong (not just quieter) compression curve into the
    // shipped default. The Super Lead's own numbers (same power-section topology, same tube fit) are a reasonable
    // stand-in verified to sound healthy for the same plucked-note stress this amp's own reference model struggles
    // with (`Tests/JCM800StyleAmplifierProcessorTests.cpp`'s reducedOrder pluck test). Revisit once the reference
    // model's own instability has a real root cause, not just a stability band-aid.
    static constexpr double bmGain0 = 9.5;
    static constexpr double bmYmax = 0.198;
    static constexpr double bmKneeN = 6.0;
    // Same fitted-from-measurement sections as SuperLeadStyleAmplifierProcessor (same topology, same documented
    // placeholder choice to share the Super Lead's fit -- see the bmGain0 note above). 2026-10-09: this amp's own
    // full reference power netlist measured a dead Presence and a non-physical ~40 dB midband notch, so fitting
    // against the structurally identical, healthy Super Lead reference is the right call until the J8 power
    // model's known stability compromise is resolved. The constants below replace the old 90 Hz shelf, which
    // under-measured the real response by ~7-9 dB toward the top (the dark, dead-presence audit symptom).
    static constexpr double bmBaseDc = 1.1387;                        // section gain at DC (~+1.1 dB, keeps 100 Hz at unity)
    static constexpr double bmBaseZ1Hz = 118.0, bmBaseZ2Hz = 3226.0;  // zeros
    static constexpr double bmBaseP1Hz = 86.0,  bmBaseP2Hz = 4888.0;  // poles
    static constexpr double bmPresHz = 4684.0, bmPresQ = 0.67, bmPresZeroHz = 3205.0;
    static constexpr double bmPresMixK = 0.727, bmPresMixR = 0.909;   // m(p) saturates at ~8x (~19 dB) at p = 1
    // Level re-trim (2026-10-09): the refit section is ~2.7 dB hotter at noon than the shelf it replaced, and
    // PedalUnityLevel needs noon at unity while the registry's -17.47 dB trim stays put. This folds the re-derived
    // -2.69 dB (measured by that same test) into the stage; bump the registry trim to -20.16 dB if it is ever moved.
    static constexpr double bmLevelTrim = 0.734;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { v1aPlate, v1bPlate, gainStagePlate, followerOut, toneStackOut, phaseInverterGrid, phaseInverterPlateA,
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
    // docs/circuits/NodalCircuitSolver.md and docs/circuits/JCM8002203.md for the measurements and what was tried instead.
    /** Test hook: replaces the speaker by a plain resistor (a pure resistive load, to compare the real speaker against). */
    void debugSetResistiveLoad (double ohms);
    /** Test hook: the 47k negative-feedback resistor (a huge value opens the loop). */
    void debugSetFeedbackResistance (double ohms);
    /** Diagnostic only: freezes the supply's dynamic current-draw feedback at its prepare()-time idle value instead of
        updating it from the tubes' actual draw every supplyInterval samples -- isolates whether a self-oscillation is
        coming from the sag loop's own (discretely-updated) coupling to the audio-rate solve, per
        [[circuit-tube-model-loop-instability]]'s diagnostic step 2. */
    void debugFreezeSupplyCurrent (bool freeze) { supplyCurrentFrozen = freeze; }
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

        // preamp: a single input driving FOUR cascaded gain stages (V1a -> Gain pot -> V1b -> V2a -> V2b follower),
        // not two channels mixed together -- see the header's own note on why this amp's preamp differs from every
        // other one on this project's roadmap.
        int pSrcV1 = 0, pSrcV2 = 0, pSrcIn = 0;
        int rGainTop = 0, rGainBot = 0;
        NodalCircuit::Node pPlate2 = 0, pFollower = 0, pPlateV1a = 0, pPlateV1b = 0;
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
        // nominal rail in prepare(). The four pairs are the two fitted biquads' direct-form-I histories.
        double bmRail = 0.0, bmEnvelope = 0.0, bmOutput = 0.0;
        double bmBaseX1 = 0.0, bmBaseX2 = 0.0, bmBaseY1 = 0.0, bmBaseY2 = 0.0;
        double bmHpX1 = 0.0, bmHpX2 = 0.0, bmHpY1 = 0.0, bmHpY2 = 0.0;
        double presenceMix = 0.0; // set in updatePots from the (smoothed) Presence knob

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
        double gain, treble, middle, bass, presence, powerDrive, bias, tubeFeel;
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
    bool supplyCurrentFrozen = false; // diagnostic only, see debugFreezeSupplyCurrent()
    void updateSupply (Channel& ch) const;
    /** reducedOrder only: the toneStackOut -> speaker transfer, fitted to the full reference model (see the header's
        "reduced-order power stage" comment and the .cpp for the calibration data). Updates ch.bmRail/ch.bmEnvelope
        (a slow envelope follower driving a measured sag lookup) and returns this sample's speaker-equivalent voltage. */
    double behavioralPowerStage (Channel& ch, double toneVoltage) const noexcept;
    /** prepare() only: discretizes the two fitted analog sections (bmBase*, bmPres*) into biquad coefficients at the
        current sample rate. The presence section's numerator is normalized to unity at 6 kHz so presenceMix is the
        measured gain law directly. */
    void designPowerFilters();
    double bmBaseB0 = 0.0, bmBaseB1 = 0.0, bmBaseB2 = 0.0, bmBaseA1 = 0.0, bmBaseA2 = 0.0;
    double bmHpB0 = 0.0, bmHpB1 = 0.0, bmHpB2 = 0.0, bmHpA1 = 0.0, bmHpA2 = 0.0;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* tubeFeelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedGain, smoothedTreble, smoothedMiddle, smoothedBass,
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
