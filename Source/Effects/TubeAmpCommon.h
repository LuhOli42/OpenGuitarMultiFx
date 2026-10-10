#pragma once

#include "NodalCircuit.h"
#include "TubeModels.h"

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

/**
    Voicing preset for PowerStageTail: how the removed output transformer + physical speaker + push-pull
    imbalance colour the reduced-order behavioural power stage, per amp class. `otHz` is the transformer
    leakage-inductance rolloff's -3 dB point (vintage iron starts to matter around 8-12 kHz, small class-A
    iron stays open to ~13-15 kHz); `resHz`/`resDb`/`resQ` are the speaker cone resonance as it appears at
    the speaker terminals once the amp's negative feedback has damped it (class-A no-NFB amps show the most);
    `lfHz`/`core` are the split frequency and depth of the output transformer's LF core saturation; `asym` is
    the even-order content of an imperfectly-balanced push-pull pair; `dyn` is how much harder-driven signal
    lets the resonance bloom (feedback stops controlling the output as the stage compresses).
*/
struct PowerTailVoicing
{
    double otHz, resHz, resDb, resQ, lfHz, core, asym, dyn;
};

namespace voicings
{
    // Blackface/silverface-style: big iron, moderate NFB, low even-order content.
    // resDb stays small everywhere: the fitted shelf/biquads already reproduce the full model's static
    // cone resonance, so the tail adds only a hint of it plus the dyn envelope bloom.
    inline constexpr PowerTailVoicing fenderClean { 10000.0, 95.0, 1.0, 1.0, 160.0, 0.35, 0.06, 0.6 };
    // EL34/KT66 NFB amps (plexi through JCM): earlier transformer rolloff, a bit more even-order grit.
    inline constexpr PowerTailVoicing britishNfb { 10000.0, 100.0, 0.0, 1.0, 160.0, 0.3, 0.09, 0.7 };
    // Cathode-biased EL84 class-A (Vox/Matchless family): no global feedback, so the cone resonance is wide
    // open and the small OT saturates earlier; most even-order content of the bunch.
    inline constexpr PowerTailVoicing classAChime { 13000.0, 105.0, 1.0, 0.9, 180.0, 0.9, 0.12, 0.8 };
    // Modern high-gain: tight feedback and fitted filters that already carry the LF resonance, so only the
    // transformer colour and a little even-order content are added here.
    inline constexpr PowerTailVoicing highGain { 11000.0, 95.0, 0.0, 1.0, 160.0, 0.3, 0.04, 0.4 };
    // Big bass iron: resonance down in the 60-80 Hz band, earlier top rolloff, deeper core saturation.
    inline constexpr PowerTailVoicing bassAmp { 7500.0, 70.0, 1.5, 1.0, 140.0, 0.8, 0.06, 0.5 };
    // Single-ended small-amp (Herzog-style): no push-pull cancellation at all, so the most even-order
    // content of any voicing, no feedback damping, and a small OT that saturates early.
    inline constexpr PowerTailVoicing singleEnded { 11000.0, 110.0, 1.5, 0.9, 170.0, 1.0, 0.18, 0.8 };
    // Direct-coupled solid-state (Acoustic 360-style): no output transformer and no tube imbalance, so
    // only the speaker cone reaction remains -- the OT poles sit at the 0.4*fs clamp where they pass ~flat.
    inline constexpr PowerTailVoicing solidState { 40000.0, 75.0, 1.5, 1.0, 140.0, 0.0, 0.0, 0.5 };
}

/** The constant-cost pieces of a tube power amp that the reduced-order behavioural stage does not get from
    its fitted knee + shelving filter: the output transformer's high-frequency leakage-inductance rolloff
    (two cascaded poles -- what keeps clipped treble from sounding fizzy), its low-frequency core saturation
    (flux is proportional to V/f, so only the low-frequency split is soft-compressed -- what glues bass notes),
    the speaker cone resonance reflected back to the OT secondary (a band-limited bump the amp's negative
    feedback only partly damps, and which blooms as the amp is driven into compression), and a quadratic
    (even-order) term for the never-perfectly-balanced push-pull pair. All amounts are normalized by
    `refVolts` (the amp's full-drive speaker volts), so one set of voicing numbers works at any rail.
    Constant cost per sample: one biquad, three one-pole filters, a handful of multiplies. */
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
        const double w = 2.0 * pi * v.resHz / fs;
        const double alpha = std::sin (w) / (2.0 * v.resQ);
        const double a0 = 1.0 + alpha;
        resB0 = alpha / a0; resB2 = -alpha / a0;      // constant-0 dB-peak bandpass, added on top of dry
        resA1 = -2.0 * std::cos (w) / a0; resA2 = (1.0 - alpha) / a0;
        resAdd = std::pow (10.0, v.resDb / 20.0) - 1.0;
        coreAmt = v.core; asymAmt = v.asym; dynAmt = v.dyn;
        invRef = 1.0 / juce::jmax (1.0e-9, refVolts);
        lp1 = lp2 = lfState = env = dc = 0.0;
        resX1 = resX2 = resY1 = resY2 = 0.0;
    }

    double process (double x) noexcept
    {
        // Speaker cone resonance at the terminals; the drive envelope lets it bloom as the stage compresses
        // (feedback loses control exactly when the amp is being leaned on).
        const double rb = resB0 * x + resB2 * resX2 - resA1 * resY1 - resA2 * resY2;
        resX2 = resX1; resX1 = x; resY2 = resY1; resY1 = rb;
        env += envC * (std::abs (x) * invRef - env);
        const double v0 = x + resAdd * (1.0 + dynAmt * juce::jlimit (0.0, 1.0, env)) * rb;
        // Even-order content: a push-pull pair is never perfectly balanced. The quadratic leaves a
        // positive mean under drive which a transformer can't pass -- remove it like the real OT does.
        const double v = v0 + asymAmt * v0 * v0 * invRef;
        dc += dcC * (v - dc);
        const double vdc = v - dc;
        // OT core saturation: only the LF split saturates (flux ~ V/f).
        lfState += lfC * (vdc - lfState);
        const double lfn = lfState * invRef;
        const double vc = (vdc - lfState) + lfState / (1.0 + coreAmt * lfn * lfn);
        if (lpC < 0.0)
            return vc; // no output transformer (solid-state voicing)
        // OT leakage inductance.
        lp1 += lpC * (vc - lp1);
        lp2 += lpC * (lp1 - lp2);
        return lp2;
    }

private:
    double lp1 = 0.0, lp2 = 0.0;
    double lfState = 0.0;
    double resX1 = 0.0, resX2 = 0.0, resY1 = 0.0, resY2 = 0.0;
    double env = 0.0, dc = 0.0;
    double lpC = 0.0, lfC = 0.0, envC = 0.0, dcC = 0.0;
    double resB0 = 0.0, resB2 = 0.0, resA1 = 0.0, resA2 = 0.0, resAdd = 0.0;
    double coreAmt = 0.0, asymAmt = 0.0, dynAmt = 0.0, invRef = 1.0;
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
