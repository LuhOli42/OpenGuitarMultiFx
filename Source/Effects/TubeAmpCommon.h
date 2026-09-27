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

} // namespace openguitarmultifx::tubeamp
