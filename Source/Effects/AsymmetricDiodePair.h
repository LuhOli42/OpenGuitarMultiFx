#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace openguitarmultifx
{

/**
    Two silicon diode branches wired antiparallel across the same two
    nodes, but with a DIFFERENT number of diodes in series on each side
    (e.g. 1 diode one way, 2 in series the other way) -- a reusable
    building block for circuit-modeling processors, not specific to any
    one pedal. `chowdsp_wdf`'s own `DiodePairT` (Werner et al.'s Lambert-W
    closed-form solve) only covers the SYMMETRIC case (the same diode
    count both directions, via its `nDiodes` parameter multiplying Vt
    equally on both sides) -- checked before writing this (see
    Source/Effects/AGENTS.md's decision log), so an asymmetric pair is a
    genuine gap, same situation `ShichmanHodgesJFET`/`EbersMollBJT` fill
    for devices `chowdsp_wdf` has no element for at all.

    ## Why asymmetric diode counts matter
    N silicon diodes in series behave, to an excellent approximation, like
    one diode with N times the thermal voltage (the same standard
    technique `chowdsp_wdf`'s own `nDiodes` parameter uses) -- so 1 diode
    one way and 2 in series the other way clip at roughly Vt and 2*Vt
    respectively (~0.6V vs ~1.2V for silicon), giving a genuinely
    ASYMMETRIC clipping curve: one half-cycle clips earlier/softer, the
    other later/harder. This is a real, audible, load-bearing part of a
    handful of vintage overdrive circuits (the BOSS OD-1 being the
    textbook example -- 3 diodes in its op-amp's feedback loop, 2 one way
    and 1 the other) and can't be captured by a single-Vt symmetric model.

    ## Solved via 1D Newton-Raphson, not 2D/3D like the BJT/JFET
    Unlike a 3-terminal transistor, this is a single 2-terminal nonlinear
    element -- one unknown (the port voltage V, defined positive in the
    "forward" branch's conducting direction), one equation:

        I(V) = Is*(exp(V/VtF) - 1) - Is*(exp(-V/VtR) - 1)

    where VtF = nDiodesForward*Vt, VtR = nDiodesReverse*Vt (the first term
    is the forward branch's Shockley current, the second is the reverse
    branch's, conducting when V is negative). Given the branch's Thevenin
    equivalent (Rth, Vth) from the surrounding circuit, KCL requires
    (Vth - V)/Rth == I(V); Newton-Raphson solves this exactly like
    EbersMollBJT solves its own KCL residuals, just for one equation
    instead of three.
*/
class AsymmetricDiodePair
{
public:
    static inline std::atomic<long long> solveFailures { 0 }; // see EbersMollBJT::solveFailures
    void setParameters (double saturationCurrent, double thermalVoltage,
                         double diodeCountForward, double diodeCountReverse) noexcept
    {
        Is = saturationCurrent;
        VtForward = thermalVoltage * diodeCountForward;
        VtReverse = thermalVoltage * diodeCountReverse;
    }

    /** Solves for the port voltage V given the branch's Thevenin
        equivalent (Rth, Vth). Newton-Raphson starts from an analytic
        upper bound on the root (see the comment inside), not last
        sample's value. Returns false if the solve failed to converge, in
        which case `v` is set to the last CONVERGED value and the caller
        should use that rather than a non-converged result. */
    bool solve (double rth, double vth, double& v, int maxIterations = 12, double tolerance = 1.0e-9) noexcept
    {
        // Start from an analytic UPPER BOUND on the root instead of last
        // sample's value: the diode alone carrying the whole Thevenin
        // short-circuit current vth/rth sits at V0 = Vt*ln(1 + (vth/rth)/Is)
        // (mirrored for negative vth), and since the resistor takes some of
        // that current the true root is at or below V0 in magnitude (never
        // above vth either). f(V) = (vth-V)/rth - I(V) is concave and
        // decreasing, so Newton from the right of the root converges
        // monotonically. Warm-starting from vPrev instead overshoots badly
        // when the drive is hot: the exponential's Newton step from far
        // away only sheds ~Vt per iteration, so a sample that jumps from
        // "diodes off" to "hard clipping" ran out of iterations (measured:
        // ~0.5% failed solves at max gain, each one an audible held-sample
        // glitch).
        if (vth >= 0.0)
            v = std::min (vth, VtForward * std::log1p ((vth / rth) / Is));
        else
            v = -std::min (-vth, VtReverse * std::log1p ((-vth / rth) / Is));

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            const double vF = clamp (v);
            const double vR = clamp (-v);

            const double eF = std::exp (vF / VtForward);
            const double eR = std::exp (vR / VtReverse);

            const double current = Is * (eF - 1.0) - Is * (eR - 1.0);
            const double f = (vth - v) / rth - current;

            if (std::abs (f) < tolerance)
            {
                vPrev = v;
                return true;
            }

            // dI/dv = Is/VtF*eF + Is/VtR*eR (both terms push current up as
            // v increases, since the reverse branch's forward voltage vR
            // decreases as v increases -- d(vR)/dv = -1, times its own
            // negative sign in I(V), giving a positive contribution).
            const double dIdv = (Is / VtForward) * eF + (Is / VtReverse) * eR;
            const double dfdv = -1.0 / rth - dIdv;

            if (std::abs (dfdv) < 1.0e-18)
                break; // singular derivative -- bail out to the non-converged fallback below

            v -= f / dfdv;
        }

        solveFailures.fetch_add (1, std::memory_order_relaxed);
        v = vPrev;
        return false;
    }

    void reset (double initialV) noexcept { vPrev = initialV; }

private:
    static double clamp (double v) noexcept { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

    double Is = 2.52e-9;
    double VtForward = 25.85e-3;
    double VtReverse = 25.85e-3;

    double vPrev = 0.0;
};

} // namespace openguitarmultifx
