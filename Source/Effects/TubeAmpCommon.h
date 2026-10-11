#pragma once

#include "NodalCircuit.h"
#include "TubeModels.h"
#include "../Engine/SoftCeiling.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>

namespace openguitarmultifx::tubeamp
{

/**
    What the tube-amplifier processors (Bassman, Super Lead) have in common and that is not a circuit topology: the guitar
    speaker as the amplifier sees it, the input protection, the de-click after a solver recovery, and the 12AX7 cathode
    follower's numbers. The circuits themselves (docs/circuits/Bassman5F6A.md, SuperLead1959.md) differ in every stage.
*/

/** A guitar speaker seen from the amplifier: voice coil (Re, Le) in series with the cone's mechanical resonance
    (a parallel RLC that peaks at f0). Everything scales with the nominal impedance: R and L up, C down. */
struct SpeakerModel { double re, le, rp, lp, cp; };

inline SpeakerModel speakerModel (double nominalOhms)
{
    const double z = nominalOhms / 8.0, f0 = 85.0, q = 5.0, twoPi = 6.283185307179586;
    const double rp = 32.0 * z;
    return { 6.5 * z, 0.55e-3 * z, rp, rp / (twoPi * f0 * q), q / (twoPi * f0 * rp) };
}

// Input protection. A guitar peaks at 0.3-0.5 V; a pedal with its Level up can hand the amp several volts, and above ~0.7 V of
// peak the power stage's output transformer starts flying back to twice the supply on every edge and the solver loses the
// operating point (docs/circuits/Bassman5F6A.md, "A hot pedal into the amp"). The first stage is nowhere near clipping at
// 0.65 V, so the limit only takes the top off a very hot signal: nothing below the knee changes.
constexpr double inputKnee = 0.4, inputSpan = 0.25;
inline double inputLimit (double x) noexcept
{
    const double a = std::abs (x);
    return a <= inputKnee ? x : std::copysign (inputKnee + inputSpan * std::tanh ((a - inputKnee) / inputSpan), x);
}

constexpr double declickDecay = 0.996; // per sample: the offset left by a restore dies away with a ~5 ms time constant

/** The emitted float's full scale: an amp's output stage cannot swing past its rails, so a hot Output/Level
    knob pushing the model toward the host's 0 dBFS clip should saturate, not emit an unbounded float that
    hard-clips downstream. The model-stage half of the two-stage contract in Engine/SoftCeiling.h -- the
    device boundary's knee sits at this ceiling, so a limited emit reaches the DAC unchanged. */
constexpr double outKnee = modelKnee, outCeil = modelCeiling;
inline double outputCeiling (double x) noexcept
{
    return modelSoftCeiling (x);
}

/** The resolved DEFAULT for a newly created amp's Cab selector (from ~/.config cab_voicing.txt and the
    OGMFX_CAB env var, which overrides it in renders/tests). The #65 global kill switch this replaced is gone:
    cab on/off is now a per-amp page-2 parameter the tail polls per sample, so this flag is only read at
    parameter construction time (control thread) -- it no longer needs to gate already-live amps. */
inline std::atomic<bool> cabVoicingEnabled { true };

/**
    Voicing preset for PowerStageTail: how the removed output transformer + physical speaker + push-pull
    imbalance colour the reduced-order behavioural power stage, per amp class. `otHz` is the transformer
    leakage-inductance rolloff's -3 dB point (vintage iron starts to matter around 8-12 kHz, small class-A
    iron stays open to ~13-15 kHz); `resHz`/`resDb`/`resQ` are the speaker cone resonance as it appears at
    the speaker terminals once the amp's negative feedback has damped it (class-A no-NFB amps show the most);
    `lfHz`/`core` are the split frequency and depth of the output transformer's LF core saturation; `asym` is
    the even-order content of an imperfectly-balanced push-pull pair; `dyn` is how much harder-driven signal
    lets the resonance bloom (feedback stops controlling the output as the stage compresses). `cabResHz`/
    `cabResDb`/`cabLoHz`/`cabLpHz`/`cabAirHz` voice the loudspeaker + cabinet the full netlist reaches
    through its real speaker RLC: a cone/cab resonance bump that falls away again below `cabLoHz`, the
    steep top rolloff past a few kHz, and a gentler continuation into the top octave (a guitar cab is
    effectively gone above ~6-8 kHz).
*/
struct PowerTailVoicing
{
    double otHz, resHz, resDb, resQ, lfHz, core, asym, dyn;
    double cabResHz, cabResDb, cabLoHz, cabLpHz, cabAirHz;
    // Optional band-limited presence bump (the OT/speaker/NFB presence peak the full netlist shows
    // around ~9-10 kHz). Trailing defaults keep every existing voicing aggregate-init valid and
    // leave it silent: presenceDb = 0 adds nothing.
    double presenceHz = 0.0, presenceDb = 0.0, presenceQ = 1.5;
    // Optional top-octave dip: real cabs and these amps' own netlists put a broad notch around
    // 8-10 kHz rather than a monotone rolloff. 0 dB = off (keeps every other voicing untouched).
    double cabNotchHz = 0.0, cabNotchDb = 0.0;
};

namespace voicings
{
    // Blackface/silverface-style: big iron, moderate NFB, low even-order content.
    // resDb stays small everywhere: the fitted shelf/biquads already reproduce the full model's static
    // cone resonance, so the tail adds only a hint of it plus the dyn envelope bloom.
    // Cab: open-back 1x12/2x12 -- resonance nearer 80 Hz, slightly airier top than a closed 4x12.
    // The bump stays small: Bassman sits ~1 dB under the PedalUnityLevel bound already, and the tail
    // must not add more than a fraction of a dB around the E3 test fundamental.
    inline constexpr PowerTailVoicing fenderClean { 10000.0, 95.0, 1.0, 1.0, 160.0, 0.35, 0.06, 0.6,
                                                    80.0, 1.0, 70.0, 5800.0, 8200.0 };
    // EL34/KT66 NFB amps (plexi through JCM): earlier transformer rolloff, a bit more even-order grit.
    // Cab: closed-back 4x12 -- the ~100 Hz thump and the steep Celestion-style top. The thump is
    // kept to +1.5 dB: it feeds straight into the PedalUnityLevel measurement band (~165 Hz).
    inline constexpr PowerTailVoicing britishNfb { 10000.0, 100.0, 0.0, 1.0, 160.0, 0.3, 0.09, 0.7,
                                                   100.0, 1.5, 60.0, 5400.0, 7800.0 };
    // Cathode-biased EL84 class-A (Vox/Matchless family): no global feedback, so the cone resonance is wide
    // open and the small OT saturates earlier; most even-order content of the bunch.
    // Cab: open-back alnico 1x12/2x12 -- like fenderClean with a touch less air.
    inline constexpr PowerTailVoicing classAChime { 13000.0, 105.0, 1.0, 0.9, 180.0, 0.9, 0.12, 0.8,
                                                    90.0, 2.0, 70.0, 5700.0, 8000.0 };
    // Modern high-gain: tight feedback and fitted filters that already carry the LF resonance, so only the
    // transformer colour and a little even-order content are added here.
    // Cab: closed-back 4x12, V30-style -- marginally darker/sharper top than the vintage voicing.
    inline constexpr PowerTailVoicing highGain { 11000.0, 95.0, 0.0, 1.0, 160.0, 0.3, 0.04, 0.4,
                                                 100.0, 2.5, 60.0, 5300.0, 7600.0 };
    // Per-amp top-band rebalances vs the shared highGain voicing: the October audit measured each reduced
    // path 20-40 dB darker than its own full netlist at 9-12 kHz while the nonlinear band flooded -- the
    // cab LP corner had folded the top octave into one narrow harsh band. These keep the high-gain LF/res
    // character but let the speaker's top through the way the full models do.
    inline constexpr PowerTailVoicing highGainEVH { 11000.0, 95.0, 0.0, 1.0, 160.0, 0.3, 0.04, 0.4,
                                                    100.0, 2.5, 60.0, 5600.0, 11000.0, 0.0, 0.0, 1.5, 8500.0, -15.0 };
    inline constexpr PowerTailVoicing highGainENGL { 11000.0, 95.0, 0.0, 1.0, 160.0, 0.3, 0.04, 0.4,
                                                     100.0, 2.5, 60.0, 5800.0, 11000.0, 0.0, 0.0, 1.5, 8500.0, -23.0 };
    inline constexpr PowerTailVoicing highGainSLO { 11000.0, 95.0, 0.0, 1.0, 160.0, 0.3, 0.04, 0.4,
                                                    100.0, 2.5, 60.0, 4500.0, 14000.0, 0.0, 0.0, 1.5, 9000.0, -23.0 };
    // Big bass iron: resonance down in the 60-80 Hz band, earlier top rolloff, deeper core saturation.
    // Cab: 4x10/8x10/1x15 bass box -- resonance in the 50s and the top is gone by ~4 kHz.
    inline constexpr PowerTailVoicing bassAmp { 7500.0, 70.0, 1.5, 1.0, 140.0, 0.8, 0.06, 0.5,
                                                58.0, 2.5, 45.0, 3800.0, 5600.0 };
    // Single-ended small-amp (Herzog-style): no push-pull cancellation at all, so the most even-order
    // content of any voicing, no feedback damping, and a small OT that saturates early.
    // Cab: small open-back 1x12 -- higher resonance, mid-scooped top like the vintage voicings.
    inline constexpr PowerTailVoicing singleEnded { 11000.0, 110.0, 1.5, 0.9, 170.0, 1.0, 0.18, 0.8,
                                                    105.0, 2.0, 70.0, 5500.0, 7800.0 };
    // Direct-coupled solid-state (Acoustic 360-style): no output transformer and no tube imbalance, so
    // only the speaker cone reaction remains -- the OT poles sit at the 0.4*fs clamp where they pass ~flat.
    // Cab: the 1x18 folded-horn-style bass bin -- deepest resonance, earliest top cutoff.
    inline constexpr PowerTailVoicing solidState { 40000.0, 75.0, 1.5, 1.0, 140.0, 0.0, 0.0, 0.5,
                                                   55.0, 2.5, 40.0, 3600.0, 5400.0 };
}

/** One speaker/cabinet voicing, decoupled from the amp voicings below: exactly the CabSim coefficient
    inputs. Selection 1 on a tail's Cab selector is always the amp's own (stock) profile, captured out of
    its PowerTailVoicing at prepare() -- the alternates a selector offers come from the `cabs::` table. */
struct CabProfile
{
    double resHz = 0.0, resDb = 0.0, loHz = 0.0, lpHz = 0.0, airHz = 0.0, notchHz = 0.0, notchDb = 0.0;
};

struct CabOption
{
    const char* name;   // shown on the page-2 Cab selector's option stack
    CabProfile profile;
};

namespace cabs
{
    // Shared alternates for the page-2 Cab selectors. Each amp wires in the few that make sense
    // alongside its own voiced (stock) cab; the numbers are the same (resonance bump, low cutoff,
    // top rolloff, air pole) the voicings table above encodes.
    inline constexpr CabOption greenback412 { "4x12 GB",     { 100.0, 1.5, 60.0, 5400.0, 7800.0 } };
    inline constexpr CabOption v30_412      { "4x12 V30",   { 100.0, 2.5, 60.0, 5300.0, 7600.0 } };
    inline constexpr CabOption alnico212    { "2x12 Alnico", { 90.0, 2.0, 70.0, 5700.0, 8000.0 } };
    inline constexpr CabOption jensen212    { "2x12 Jensen", { 80.0, 1.0, 70.0, 5800.0, 8200.0 } };
    inline constexpr CabOption open112      { "1x12 Open",   { 105.0, 2.0, 70.0, 5500.0, 7800.0 } };
    inline constexpr CabOption ev112        { "1x12 EV",     { 95.0, 1.5, 65.0, 6200.0, 8600.0 } };
    inline constexpr CabOption tweed410     { "4x10 Tweed",  { 90.0, 2.0, 55.0, 4800.0, 7000.0 } };
    inline constexpr CabOption bass810      { "8x10 Fridge", { 58.0, 2.5, 45.0, 3800.0, 5600.0 } };
    inline constexpr CabOption bass115      { "1x15 Bass",   { 60.0, 2.0, 42.0, 3600.0, 5200.0 } };
}

/** Builds the page-2 "Cab" selector every voiced amp shares: index 0 = Off, 1 = the amp's own voiced cab
    (`stockName`, its historic pairing, already encoded in its PowerTailVoicing), then the alternates
    (index order must match the `setCabChoices` list the amp's prepare() wires in). Stepped + <= 8
    options, so ParameterPanel renders it as an option stack, not a knob. The default follows
    `cabVoicingEnabled` (the persisted/env default): Off when the user has cabs defaulted off, otherwise
    the stock cab -- and a preset's own `*_cab` attribute overrides either at setState() time. */
template <size_t N>
inline std::unique_ptr<juce::AudioParameterFloat> makeCabParam (
    const juce::String& paramId, const char* stockName, const CabOption* const (&alts)[N])
{
    juce::StringArray names;
    names.add ("Off");
    names.add (stockName);
    for (const auto* alt : alts)
        names.add (alt->name);
    // Hoisted out of the return expression: reading names.size() there while a
    // sibling argument moves `names` into the lambda is unspecified order --
    // GCC evaluated the move first and built the range as 0 .. -1 (empty
    // selector on screen).
    const int optionCount = names.size();
    return std::make_unique<juce::AudioParameterFloat> (
        paramId, "Cab", juce::NormalisableRange<float> (0.0f, (float) (optionCount - 1), 1.0f),
        cabVoicingEnabled.load (std::memory_order_relaxed) ? 1.0f : 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [names = std::move (names)] (float v, int)
            { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; }));
}

/** The static acoustic signature of the cabinet itself, applied at the very end of PowerStageTail: a
    cone/cab resonance bump (`cabResHz`/`cabResDb`), a two-pole lowpass at `cabLpHz` (a guitar speaker's
    response collapses a little above 4-6 kHz -- what actually removes the harsh edge a raw power-amp
    output has), a one-pole lowpass at `cabAirHz` continuing the fall through the top octave, and a
    one-pole highpass at `cabLoHz` rolling off below the resonance the way the real box does (a bump,
    not a shelf). Two biquads' worth of state plus two poles -- effectively free per sample. */
struct CabSim
{
    void prepare (double fs, double resHz, double resDb, double loHz, double lpHz, double airHz,
                  double notchHz = 0.0, double notchDb = 0.0) noexcept
    {
        const double pi = juce::MathConstants<double>::pi;
        // Biquad designs go unstable when their frequency is at/above ~Nyquist (a 32 kHz WAV through
        // RenderFx is a real case). A corner that high means "no filter at this rate" -- bypass rather
        // than cram it under the limit. Just under Nyquist is the bound (stable for w0 < pi); the OT
        // poles' tighter 0.4*fs is a property of their coefficient solve, not of these biquads.
        const double cap = 0.49 * fs;
        loC = 1.0 - std::exp (-2.0 * pi * juce::jmin (loHz, cap) / fs);
        // Cone/cab resonance: RBJ peaking biquad, +cabResDb at cabResHz, back to 0 dB either side.
        const double w0 = 2.0 * pi * juce::jmin (resHz, cap) / fs;
        const double A = std::pow (10.0, resDb / 40.0);
        const double alpha = std::sin (w0) / 1.6; // Q ~= 0.8: a broad physical hump, not a resonant peak
        const double a0p = 1.0 + alpha / A;
        pkB0 = (1.0 + alpha * A) / a0p; pkB1 = -2.0 * std::cos (w0) / a0p; pkB2 = (1.0 - alpha * A) / a0p;
        pkA1 = pkB1; pkA2 = (1.0 - alpha / A) / a0p;
        // Top-octave dip: the same peaking biquad with negative gain. Bypassed when cabNotchDb == 0.
        notchOn = std::abs (notchDb) > 1.0e-9;
        const double wN = 2.0 * pi * juce::jmax (1.0, notchHz) / fs;
        const double AN = std::pow (10.0, notchDb / 40.0);
        const double aN = std::sin (wN) / 1.4; // Q ~= 0.7: a wide shallow trough, not a surgical notch
        const double a0n = 1.0 + aN / AN;
        nkB0 = (1.0 + aN * AN) / a0n; nkB1 = -2.0 * std::cos (wN) / a0n; nkB2 = (1.0 - aN * AN) / a0n;
        nkA1 = nkB1; nkA2 = (1.0 - aN / AN) / a0n;
        // Steep top: Butterworth biquad LP at cabLpHz; above the cap it stays at identity (flat).
        if (lpHz < cap)
        {
            const double wl = 2.0 * pi * lpHz / fs;
            const double al = std::sin (wl) / 1.41421356; // Q = sqrt(2)/2
            const double a0l = 1.0 + al;
            lpB0 = (1.0 - std::cos (wl)) * 0.5 / a0l; lpB1 = (1.0 - std::cos (wl)) / a0l; lpB2 = lpB0;
            lpA1 = -2.0 * std::cos (wl) / a0l; lpA2 = (1.0 - al) / a0l;
        }
        else
        {
            lpB0 = 1.0; lpB1 = lpB2 = lpA1 = lpA2 = 0.0;
        }
        // Gentle continuation into the top octave; airC = 1.0 tracks the input exactly = flat.
        airC = airHz < cap ? 1.0 - std::exp (-2.0 * pi * airHz / fs) : 1.0;
        lo = pkX1 = pkX2 = pkY1 = pkY2 = lpX1 = lpX2 = lpY1 = lpY2 = air = 0.0;
        nkX1 = nkX2 = nkY1 = nkY2 = 0.0;
    }

    double process (double x) noexcept
    {
        lo += loC * (x - lo);       // tracked low band: removing it is the below-resonance rolloff
        const double h = x - lo;
        const double p = pkB0 * h + pkB1 * pkX1 + pkB2 * pkX2 - pkA1 * pkY1 - pkA2 * pkY2;
        pkX2 = pkX1; pkX1 = h; pkY2 = pkY1; pkY1 = p;
        const double d = notchOn ? nkB0 * p + nkB1 * nkX1 + nkB2 * nkX2 - nkA1 * nkY1 - nkA2 * nkY2 : p;
        nkX2 = nkX1; nkX1 = p; nkY2 = nkY1; nkY1 = d;
        const double l = lpB0 * d + lpB1 * lpX1 + lpB2 * lpX2 - lpA1 * lpY1 - lpA2 * lpY2;
        lpX2 = lpX1; lpX1 = d; lpY2 = lpY1; lpY1 = l;
        air += airC * (l - air);
        return air;
    }

private:
    double pkX1 = 0.0, pkX2 = 0.0, pkY1 = 0.0, pkY2 = 0.0;
    double lpX1 = 0.0, lpX2 = 0.0, lpY1 = 0.0, lpY2 = 0.0;
    double air = 0.0;
    double pkB0 = 0.0, pkB1 = 0.0, pkB2 = 0.0, pkA1 = 0.0, pkA2 = 0.0;
    double nkX1 = 0.0, nkX2 = 0.0, nkY1 = 0.0, nkY2 = 0.0;
    double nkB0 = 0.0, nkB1 = 0.0, nkB2 = 0.0, nkA1 = 0.0, nkA2 = 0.0;
    bool notchOn = false;
    double lpB0 = 0.0, lpB1 = 0.0, lpB2 = 0.0, lpA1 = 0.0, lpA2 = 0.0;
    double airC = 0.0, loC = 0.0;
    double lo = 0.0;
};

/** The constant-cost pieces of a tube power amp that the reduced-order behavioural stage does not get from
    its fitted knee + shelving filter: the output transformer's high-frequency leakage-inductance rolloff
    (two cascaded poles -- what keeps clipped treble from sounding fizzy), its low-frequency core saturation
    (flux is proportional to V/f, so only the low-frequency split is soft-compressed -- what glues bass notes),
    the speaker cone resonance reflected back to the OT secondary (a band-limited bump the amp's negative
    feedback only partly damps, and which blooms as the amp is driven into compression), and a quadratic
    (even-order) term for the never-perfectly-balanced push-pull pair, and finally the cabinet voicing
    (`CabSim`: the acoustic top rolloff + cone/cab bump a real speaker applies after the terminals). All
    amounts are normalized by `refVolts` (the amp's full-drive speaker volts), so one set of voicing
    numbers works at any rail.
    Constant cost per sample: three biquads, four one-pole filters, a handful of multiplies. */
struct PowerStageTail
{
    void prepare (double sampleRate, const PowerTailVoicing& v, double refVolts) noexcept
    {
        const double fs = juce::jmax (1.0, sampleRate);
        const double pi = juce::MathConstants<double>::pi;
        // Two coincident one-pole LPs. Solve the discrete coefficient so the cascade measures exactly
        // -3 dB at otHz (each pole |H|^2 = 1/sqrt(2) there) instead of the usual exp() approximation,
        // which drifts ~1 dB hot once otHz is a substantial fraction of Nyquist. otHz is clamped under
        // Nyquist; voicings well past it (e.g. solidState) then run the poles ~flat, as intended.
        const double wOt = 2.0 * pi * juce::jmin (v.otHz, 0.4 * fs) / fs;
        const double b = 2.0 - 1.41421356 * std::cos (wOt);
        // Negative lpC = flat: voicings at/above the clamp (e.g. solidState -- no OT at all) bypass it.
        lpC = v.otHz < 0.4 * fs ? 1.0 - (b - std::sqrt (std::max (0.0, b * b - 0.34313))) / 0.58579 : -1.0;
        lfC = 1.0 - std::exp (-2.0 * pi * v.lfHz / fs);
        envC = 1.0 - std::exp (-1.0 / (0.02 * fs)); // ~20 ms: tracks the note level, not individual cycles
        dcC = 1.0 - std::exp (-2.0 * pi * 12.0 / fs); // the OT can't pass DC: ~12 Hz mean subtraction
        // Same just-under-Nyquist cap as CabSim: a resonance at/above ~Nyquist can't be voiced anyway,
        // and the biquad design diverges there (32 kHz hosts are real), so both bumps drop out cleanly.
        const double cap = 0.49 * fs;
        if (v.resHz < cap)
        {
            const double w = 2.0 * pi * v.resHz / fs;
            const double alpha = std::sin (w) / (2.0 * v.resQ);
            const double a0 = 1.0 + alpha;
            resB0 = alpha / a0; resB2 = -alpha / a0;  // constant-0 dB-peak bandpass, added on top of dry
            resA1 = -2.0 * std::cos (w) / a0; resA2 = (1.0 - alpha) / a0;
            resAdd = std::pow (10.0, v.resDb / 20.0) - 1.0;
        }
        else
            resAdd = 0.0;
        if (v.presenceHz > 0.0 && v.presenceDb > 0.0 && v.presenceHz < cap)
        {
            const double wp = 2.0 * pi * v.presenceHz / fs;
            const double ap = std::sin (wp) / (2.0 * v.presenceQ);
            const double a0p = 1.0 + ap;
            presB0 = ap / a0p; presB2 = -ap / a0p;   // same constant-0 dB-peak bandpass form as the cone resonance
            presA1 = -2.0 * std::cos (wp) / a0p; presA2 = (1.0 - ap) / a0p;
            presAdd = std::pow (10.0, v.presenceDb / 20.0) - 1.0;
        }
        else
            presAdd = 0.0;
        coreAmt = v.core; asymAmt = v.asym; dynAmt = v.dyn;
        invRef = 1.0 / juce::jmax (1.0e-9, refVolts);
        lp1 = lp2 = lfState = env = dc = 0.0;
        resX1 = resX2 = resY1 = resY2 = 0.0;
        presX1 = presX2 = presY1 = presY2 = 0.0;
        // Selection 1 = the amp's own voiced cab, captured out of the voicing here so process() can
        // rebuild the sim on an index change without remembering the whole voicing. The starting index
        // snaps straight to the selector's current value (or the resolved default when no selector is
        // wired) -- prepare() runs before the block is live, so a fade is neither needed nor wanted
        // (a preset restored with the cab off must not fade it in first).
        stockCab = { v.cabResHz, v.cabResDb, v.cabLoHz, v.cabLpHz, v.cabAirHz, v.cabNotchHz, v.cabNotchDb };
        cabFs = fs;
        pendingSel = activeSel = juce::jlimit (0, 1 + numAlts,
                                               cabParam != nullptr ? juce::roundToInt (cabParam->get())
                                                                   : defaultCabSelection());
        const CabProfile& initial = profileFor (activeSel);
        cab.prepare (fs, initial.resHz, initial.resDb, initial.loHz, initial.lpHz, initial.airHz,
                     initial.notchHz, initial.notchDb);
        cabMixC = 1.0 - std::exp (-1.0 / (0.004 * fs)); // ~4 ms crossfade on a selection change -- no click
        cabMix = cabTarget = activeSel > 0 ? 1.0 : 0.0;
    }

    /** The amp wires its page-2 Cab selector in here along with the alternates it offers -- call at
        prepare() time (control thread), before prepare() itself. `p` is only ever READ on the audio
        thread (one float load per sample, which is atomic inside AudioParameterFloat); the param group
        owns it and outlives the prepared channel state. */
    template <size_t N>
    void setCabChoices (const juce::AudioParameterFloat* p, const CabOption* const (&alts)[N]) noexcept
    {
        cabParam = p;
        numAlts = juce::jmin ((int) N, maxCabAlts);
        for (int i = 0; i < numAlts; ++i)
            altProfiles[(size_t) i] = alts[i]->profile;
    }

    double process (double x) noexcept
    {
        // Speaker cone resonance at the terminals; the drive envelope lets it bloom as the stage compresses
        // (feedback loses control exactly when the amp is being leaned on).
        const double rb = resB0 * x + resB2 * resX2 - resA1 * resY1 - resA2 * resY2;
        resX2 = resX1; resX1 = x; resY2 = resY1; resY1 = rb;
        env += envC * (std::abs (x) * invRef - env);
        double v0 = x + resAdd * (1.0 + dynAmt * juce::jlimit (0.0, 1.0, env)) * rb;
        if (presAdd > 0.0)
        {
            const double rp = presB0 * x + presB2 * presX2 - presA1 * presY1 - presA2 * presY2;
            presX2 = presX1; presX1 = x; presY2 = presY1; presY1 = rp;
            v0 += presAdd * rp;
        }
        // Even-order content: a push-pull pair is never perfectly balanced. The quadratic leaves a
        // positive mean under drive which a transformer can't pass -- remove it like the real OT does.
        const double v = v0 + asymAmt * v0 * v0 * invRef;
        dc += dcC * (v - dc);
        const double vdc = v - dc;
        // OT core saturation: only the LF split saturates (flux ~ V/f).
        lfState += lfC * (vdc - lfState);
        const double lfn = lfState * invRef;
        const double vc = (vdc - lfState) + lfState / (1.0 + coreAmt * lfn * lfn);
        // OT leakage inductance (the solid-state voicing has none -- lpC < 0 there).
        double base = vc;
        if (lpC >= 0.0)
        {
            lp1 += lpC * (vc - lp1);
            lp2 += lpC * (lp1 - lp2);
            base = lp2;
        }
        // The page-2 Cab selector is polled per sample -- one float load, nothing else. An index change
        // first fades the current voice out; exactly at the fade's zero crossing the sim is rebuilt for
        // the new index (bounded coefficient math, no allocation, and no stale-state thump), then it
        // fades back in. Off simply fades out and stays there. The cab keeps running while bypassed so
        // its filter state stays current; a settled Off returns `base` bit-for-bit.
        const int wantSel = juce::jlimit (0, 1 + numAlts,
                                          cabParam != nullptr ? juce::roundToInt (cabParam->get()) : activeSel);
        if (wantSel != pendingSel)
        {
            pendingSel = wantSel;
            if (wantSel != activeSel)
                cabTarget = 0.0;
        }
        const double cabOut = cab.process (base);
        cabMix += cabMixC * (cabTarget - cabMix);
        if (cabMix < 1.0e-9)
        {
            cabMix = 0.0;
            if (pendingSel != activeSel)
            {
                activeSel = pendingSel;
                if (activeSel > 0)
                {
                    const CabProfile& p = profileFor (activeSel);
                    cab.prepare (cabFs, p.resHz, p.resDb, p.loHz, p.lpHz, p.airHz, p.notchHz, p.notchDb);
                }
                cabTarget = activeSel > 0 ? 1.0 : 0.0;
            }
        }
        return base + cabMix * (cabOut - base);
    }

private:
    double lp1 = 0.0, lp2 = 0.0;
    double lfState = 0.0;
    double resX1 = 0.0, resX2 = 0.0, resY1 = 0.0, resY2 = 0.0;
    double presX1 = 0.0, presX2 = 0.0, presY1 = 0.0, presY2 = 0.0;
    double env = 0.0, dc = 0.0;
    double lpC = 0.0, lfC = 0.0, envC = 0.0, dcC = 0.0;
    double resB0 = 0.0, resB2 = 0.0, resA1 = 0.0, resA2 = 0.0, resAdd = 0.0;
    double presB0 = 0.0, presB2 = 0.0, presA1 = 0.0, presA2 = 0.0, presAdd = 0.0;
    double coreAmt = 0.0, asymAmt = 0.0, dynAmt = 0.0, invRef = 1.0;
    double cabMix = 0.0, cabMixC = 0.0, cabTarget = 0.0, cabFs = 0.0;
    const juce::AudioParameterFloat* cabParam = nullptr; // watched at audio rate; written once at prepare
    CabProfile stockCab;
    static constexpr int maxCabAlts = 6;
    CabProfile altProfiles[maxCabAlts] {};
    int numAlts = 0;
    int activeSel = 0, pendingSel = 0;

    static int defaultCabSelection() noexcept
    {
        return cabVoicingEnabled.load (std::memory_order_relaxed) ? 1 : 0;
    }

    const CabProfile& profileFor (int sel) const noexcept
    {
        return sel <= 1 ? stockCab : altProfiles[(size_t) juce::jmin (sel - 2, numAlts - 1)];
    }

    CabSim cab; // acoustic cab voicing: always last (TubeAmpCommon.h)
};

// The 12AX7 cathode follower into a 100k cathode load (the stage before the tone stack in both amps).
constexpr double cathodeFollowerImpedance = 531.0;
constexpr double cathodeFollowerGain = 0.984;

/** DC cathode voltage of the 12AX7 cathode follower (plate on `vcc`, 100k cathode load) for a given grid voltage: the cathode
    current the tube passes at that voltage equals what the load draws. Bisection on the tube model itself (a monotonic
    function of the cathode voltage), so it cannot land on a wrong root the way a Newton DC solve from a bad guess can. */
inline double cathodeFollowerDc (double vcc, double vgrid)
{
    const KorenTriode tube { };
    double lo = 0.0, hi = vcc;
    for (int i = 0; i < 80; ++i)
    {
        const double vk = 0.5 * (lo + hi);
        const double ip = tube.evaluate (vgrid - vk, vcc - vk).ip;
        (ip - vk / 100.0e3 > 0.0 ? lo : hi) = vk; // still more tube current than the load draws: the cathode rises
    }
    return 0.5 * (lo + hi);
}

/** The coupling cap + grid leak in front of the phase inverter. A reduced-order power stage skips the inverter, so
    when the node it reads is DC-coupled to a cathode follower or a plate (tens to hundreds of volts), this is the part
    that keeps that DC out -- without it the behavioural curve sits pinned at one rail and the amp outputs only DC
    (silence). reset() with the node's settled DC so the first sample does not start with a thump. */
struct CouplingCapHighpass
{
    double lowpass = 0.0, coeff = 0.0;

    void prepare (double sampleRate, double capFarads, double leakOhms, double settledDc) noexcept
    {
        coeff = 1.0 - std::exp (-1.0 / (capFarads * leakOhms * sampleRate));
        lowpass = settledDc;
    }

    double process (double x) noexcept
    {
        lowpass += coeff * (x - lowpass);
        return x - lowpass;
    }
};

} // namespace openguitarmultifx::tubeamp
