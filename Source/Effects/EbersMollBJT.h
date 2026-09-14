#pragma once

#include <cmath>

namespace openguitarmultifx
{

/**
    A bipolar junction transistor modelled with the full (large-signal)
    Ebers-Moll equations, solved exactly (to numerical precision) via 3D
    Newton-Raphson every sample -- a reusable building block for
    circuit-modeling processors, not specific to any one pedal (see
    Source/Effects/AGENTS.md's decision log for why chowdsp_wdf itself
    has no transistor element and this exists instead).

    chowdsp_wdf provides no BJT element, and deriving one as a generic
    WDF R-type multi-port adaptor is a much harder, less provable
    undertaking than solving this circuit's actual nodal equations
    directly -- see the booster processor's own design notes for the
    full reasoning (informed by researching Chowdhury-DSP's own BYOD
    RangeBooster, which makes the same choice for a near-identical
    circuit; the equations below are this project's own from-scratch
    derivation from the standard textbook Ebers-Moll model, not BYOD's
    code, which is GPL-3.0 and reference-only per this project's rule).

    ## Why 3 terminals, not 2
    Every terminal (base, emitter, collector) in the circuits this is
    built for has its own real dynamics (a resistor and/or a capacitor's
    companion model), so none of the three can be treated as an ideal,
    non-loading voltage source without giving up real fidelity. This
    solves the full 3-terminal problem: given each terminal's own
    Thevenin-equivalent (a resistance + a source voltage, folding in
    whatever fixed resistors and capacitor companion-model history terms
    the surrounding circuit reduces to), it solves simultaneously for
    the three terminal voltages that satisfy KCL at every terminal AND
    the Ebers-Moll junction equations -- exactly what SPICE does for
    every nonlinear device at every timestep, not a novel or exotic
    technique.

    ## Equations (standard NPN Ebers-Moll; PNP handled by mirroring)
    v_be = V_B - V_E, v_bc = V_B - V_C. With Is (saturation current), Vt
    (thermal voltage), betaF/betaR (forward/reverse current gain):

        i_C = Is*(exp(v_be/Vt) - exp(v_bc/Vt)) - (Is/betaR)*(exp(v_bc/Vt) - 1)
        i_E = -Is*(exp(v_be/Vt) - exp(v_bc/Vt)) - (Is/betaF)*(exp(v_be/Vt) - 1)
        i_B = (Is/betaF)*(exp(v_be/Vt) - 1) + (Is/betaR)*(exp(v_bc/Vt) - 1)

    (i_B + i_C + i_E == 0 identically -- a useful sanity check on this
    derivation, verified by hand before implementing.)

    For each terminal X with external Thevenin equivalent (Rth_X, Vth_X),
    KCL requires (Vth_X - V_X)/Rth_X == i_X, i.e. the residual

        F_X = (Vth_X - V_X)/Rth_X - i_X(v_be, v_bc) == 0

    Newton-Raphson solves the 3x3 system F(V_B,V_E,V_C) = 0 using the
    analytically-derived Jacobian (see solve()'s implementation).

    ## PNP
    A PNP transistor is a mirror image of an NPN one: negate every
    terminal voltage (and correspondingly every current), and it obeys
    the exact same NPN equations above in the negated ("mirrored")
    coordinate system. Callers of a PNP circuit should mirror their
    Thevenin voltages going in (negate) and mirror the solved terminal
    voltages coming back out (negate again) -- solve() itself only ever
    implements the NPN equations; polarity is entirely the caller's
    responsibility, kept out of this class so the numerically-sensitive
    part has only one code path to get right and verify.

    ## Numerical safety
    exp() arguments are clamped -- without this, a large transient
    (silence, a hot input spike, or a bad initial guess before the first
    real convergence) could push exp() into overflow/NaN territory,
    which would then never recover since NaN propagates through every
    subsequent sample once it appears. This clamp is physically
    reasonable too: real silicon junctions never see forward voltages
    much past ~1V before destructive currents flow, so ~40*Vt (~1V) is
    already far beyond anything the real device would survive.
*/
class EbersMollBJT
{
public:
    void setParameters (double saturationCurrent, double thermalVoltage, double betaForward, double betaReverse) noexcept
    {
        Is = saturationCurrent;
        Vt = thermalVoltage;
        betaF = betaForward;
        betaR = betaReverse;
    }

    /** Solves for (V_B, V_E, V_C) given each terminal's Thevenin-equivalent
        resistance/voltage (NPN convention -- mirror in/out for PNP, see
        class doc comment). vbIn/veIn/vcIn are the previous sample's
        converged solution, used as the Newton-Raphson starting point
        (this system is smooth enough between consecutive samples that a
        handful of iterations from the last answer converges reliably --
        the same warm-start approach any real-time nonlinear circuit
        solver uses). Returns false if the solve failed to converge
        within maxIterations (callers should fall back to holding the
        previous sample's values rather than using a non-converged,
        potentially wild result). */
    bool solve (double rthB, double vthB, double rthE, double vthE, double rthC, double vthC,
                double& vb, double& ve, double& vc, int maxIterations = 12, double tolerance = 1.0e-9) noexcept
    {
        vb = vbPrev;
        ve = vePrev;
        vc = vcPrev;

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            const double vbe = clamp (vb - ve);
            const double vbc = clamp (vb - vc);

            const double eVbe = std::exp (vbe / Vt);
            const double eVbc = std::exp (vbc / Vt);

            const double iC = Is * (eVbe - eVbc) - (Is / betaR) * (eVbc - 1.0);
            const double iE = -Is * (eVbe - eVbc) - (Is / betaF) * (eVbe - 1.0);
            const double iB = (Is / betaF) * (eVbe - 1.0) + (Is / betaR) * (eVbc - 1.0);

            const double fB = (vthB - vb) / rthB - iB;
            const double fE = (vthE - ve) / rthE - iE;
            const double fC = (vthC - vc) / rthC - iC;

            if (std::abs (fB) < tolerance && std::abs (fE) < tolerance && std::abs (fC) < tolerance)
            {
                vbPrev = vb;
                vePrev = ve;
                vcPrev = vc;
                return true;
            }

            // Partial derivatives of the diode currents w.r.t. the two
            // junction voltages -- see class doc comment for the derivation.
            const double dIbe = (Is / Vt) * eVbe; // d(Is*(exp(vbe/Vt)-1))/d(vbe)
            const double dIbc = (Is / Vt) * eVbc; // d(Is*(exp(vbc/Vt)-1))/d(vbc)

            const double diB_dvbe = dIbe / betaF;
            const double diB_dvbc = dIbc / betaR;
            const double diC_dvbe = dIbe;
            const double diC_dvbc = -dIbc * (1.0 + 1.0 / betaR);
            const double diE_dvbe = -dIbe * (1.0 + 1.0 / betaF);
            const double diE_dvbc = dIbc;

            // J[row][col], row/col order (B, E, C). F_X = (Vth_X-V_X)/Rth_X - i_X,
            // v_be = V_B-V_E, v_bc = V_B-V_C, so d(v_be)/dV_B = d(v_bc)/dV_B = 1,
            // d(v_be)/dV_E = -1, d(v_bc)/dV_C = -1, all other cross terms 0.
            const double J[3][3] = {
                { -1.0 / rthB - (diB_dvbe + diB_dvbc), diB_dvbe, diB_dvbc },
                { -(diE_dvbe + diE_dvbc), -1.0 / rthE + diE_dvbe, diE_dvbc },
                { -(diC_dvbe + diC_dvbc), diC_dvbe, -1.0 / rthC + diC_dvbc }
            };
            const double F[3] = { fB, fE, fC };

            double delta[3];
            if (! solve3x3 (J, F, delta))
                break; // singular Jacobian -- bail out to the non-converged fallback below

            vb -= delta[0];
            ve -= delta[1];
            vc -= delta[2];
        }

        // Didn't converge (or hit a singular Jacobian) -- fall back to the
        // last sample's known-good solution rather than handing the
        // caller whatever wild intermediate iterate Newton's method was
        // last at. A single dropped update is inaudible; a spurious spike
        // from a bad iterate would not be.
        vb = vbPrev;
        ve = vePrev;
        vc = vcPrev;
        return false;
    }

    void reset (double initialVb, double initialVe, double initialVc) noexcept
    {
        vbPrev = initialVb;
        vePrev = initialVe;
        vcPrev = initialVc;
    }

    /** The last converged solution (this sample's, once solve() has been
        called, otherwise the previous sample's) -- lets a caller use one
        terminal's value as a same-device, previous-sample "known" source
        for another terminal's Thevenin equivalent, when a passive branch
        bridges two of this device's own terminals (e.g. collector-to-base
        feedback) and so can't be resolved by simple series/parallel
        reduction before this device's own solve. See the DS-1 doc's
        explanation of this exact case for why previous-sample (not exact
        simultaneous) is the deliberate, documented choice. */
    void getLastSolved (double& vb, double& ve, double& vc) const noexcept
    {
        vb = vbPrev;
        ve = vePrev;
        vc = vcPrev;
    }

private:
    static double clamp (double v) noexcept { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

    /** Solves the 3x3 linear system J*x = F for x via Cramer's rule --
        fine at this fixed small size, no need for a general linear
        algebra dependency. Returns false if J is (numerically) singular. */
    static bool solve3x3 (const double J[3][3], const double F[3], double x[3]) noexcept
    {
        const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1])
                            - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                            + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);

        if (std::abs (det) < 1.0e-18)
            return false;

        const double invDet = 1.0 / det;

        for (int col = 0; col < 3; ++col)
        {
            double M[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    M[r][c] = (c == col) ? F[r] : J[r][c];

            const double detM = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
                                 - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                                 + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);

            x[col] = detM * invDet;
        }

        return true;
    }

    double Is = 5.0e-12;
    double Vt = 25.85e-3;
    double betaF = 200.0;
    double betaR = 4.0;

    double vbPrev = 0.0, vePrev = 0.0, vcPrev = 0.0;
};

} // namespace openguitarmultifx
