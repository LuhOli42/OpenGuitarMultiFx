#pragma once

#include <cmath>

namespace openguitarmultifx
{

/**
    An N-channel JFET modelled with the standard SPICE Level-1
    (Shichman-Hodges) large-signal equations, solved via 2D Newton-Raphson
    every sample -- a reusable building block for circuit-modeling
    processors, not specific to any one pedal. chowdsp_wdf has no JFET
    element (checked this project's own research before writing this --
    see Source/Effects/AGENTS.md's decision log), so this fills a real
    gap using the textbook SPICE model, the same approach EbersMollBJT.h
    takes for the BJT gap.

    ## Why 2 unknowns, not 3
    A JFET's gate-channel junction is reverse-biased in normal operation,
    so gate current is ~0 -- a physically accurate simplification (this
    is how a real JFET actually behaves at audio signal levels), not an
    arbitrary shortcut. With I_G == 0 always, KCL at the gate terminal
    is satisfied for ANY Thevenin resistance there, so the gate voltage
    is simply its Thevenin open-circuit voltage -- no solve needed for
    it. Only drain and source are genuine unknowns requiring Newton-
    Raphson, given their own Thevenin-equivalents from the surrounding
    circuit (resistors and/or capacitor companion models).

    ## Equations (SPICE Level 1 / Shichman-Hodges, N-channel)
    Vp is the pinch-off voltage (negative for a depletion-mode N-channel
    device), beta = Idss/Vp^2, lambda is channel-length modulation. The
    device is symmetric in drain/source, so when Vds < 0 the model swaps
    which terminal plays "source" for the equations below and negates
    the result -- exactly what real SPICE does, needed here because this
    project's circuits bias some JFETs at Vgs(DC) == 0 (see the DS-1
    docs), where an audio signal's negative half-cycles genuinely drive
    Vds negative.

        Vgst = Vgs - Vp   (how far the gate is above pinch-off)
        cutoff (Vgst <= 0):        Id = 0
        triode (0 < Vds < Vgst):   Id = beta*Vds*(2*Vgst - Vds)*(1 + lambda*Vds)
        saturation (Vds >= Vgst):  Id = beta*Vgst^2*(1 + lambda*Vds)

    Terminal currents: i_D = Id, i_S = -Id, i_G = 0 (KCL, since i_G == 0).
    For each of drain/source with Thevenin (Rth, Vth), the residual
    solved is F_X = (Vth_X - V_X)/Rth_X - i_X == 0, same pattern as
    EbersMollBJT's F_X, just 2 equations instead of 3.

    The triode/saturation boundary and the Vds-sign swap both make Id
    only C0 (continuous but not smooth) across those boundaries -- the
    real device is genuinely like this (it's where SPICE's own model
    has the same kink), and Newton-Raphson tolerates it fine in practice
    with a warm start from the previous sample, same as any SPICE
    transient solver stepping through it sample by sample.
*/
class ShichmanHodgesJFET
{
public:
    /** Idss: drain saturation current at Vgs=0 (A). pinchOffVoltage: Vp,
        negative for N-channel (V). channelLengthModulation: lambda (1/V). */
    void setParameters (double idss, double pinchOffVoltage, double channelLengthModulation) noexcept
    {
        Vp = pinchOffVoltage;
        beta = idss / (Vp * Vp);
        lambda = channelLengthModulation;
    }

    /** Solves for (V_D, V_S) given the gate's Thevenin open-circuit
        voltage (no gate-current solve needed, see class doc comment) and
        drain/source's own Thevenin-equivalents. vdIn/vsIn are the
        previous sample's converged solution, used as the warm start.
        Returns false if the solve failed to converge, in which case the
        caller should hold the previous sample's values (see solve()'s
        own fallback below, matching EbersMollBJT's convention). */
    bool solve (double vGate, double rthD, double vthD, double rthS, double vthS,
                double& vd, double& vs, int maxIterations = 12, double tolerance = 1.0e-9) noexcept
    {
        vd = vdPrev;
        vs = vsPrev;

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            double iD, diD_dvd, diD_dvs;
            evaluate (vGate, vd, vs, iD, diD_dvd, diD_dvs);

            const double fD = (vthD - vd) / rthD - iD;
            const double fS = (vthS - vs) / rthS - (-iD);

            if (std::abs (fD) < tolerance && std::abs (fS) < tolerance)
            {
                vdPrev = vd;
                vsPrev = vs;
                return true;
            }

            // J*delta = F, J[row][col], row/col order (D, S).
            // F_D = (vthD-vd)/rthD - iD            -> dF_D/dvd = -1/rthD - diD_dvd, dF_D/dvs = -diD_dvs
            // F_S = (vthS-vs)/rthS - (-iD)          -> dF_S/dvd = diD_dvd,           dF_S/dvs = -1/rthS + diD_dvs
            const double J00 = -1.0 / rthD - diD_dvd;
            const double J01 = -diD_dvs;
            const double J10 = diD_dvd;
            const double J11 = -1.0 / rthS + diD_dvs;

            const double det = J00 * J11 - J01 * J10;
            if (std::abs (det) < 1.0e-18)
                break; // singular Jacobian -- bail out to the non-converged fallback below

            const double invDet = 1.0 / det;
            const double deltaD = (fD * J11 - J01 * fS) * invDet;
            const double deltaS = (J00 * fS - J10 * fD) * invDet;

            vd -= deltaD;
            vs -= deltaS;
        }

        vd = vdPrev;
        vs = vsPrev;
        return false;
    }

    void reset (double initialVd, double initialVs) noexcept
    {
        vdPrev = initialVd;
        vsPrev = initialVs;
    }

    /** The last converged solution -- see EbersMollBJT::getLastSolved()'s
        doc comment for why a caller needs this (same-device cross-
        terminal coupling resolved via previous-sample values). */
    void getLastSolved (double& vd, double& vs) const noexcept
    {
        vd = vdPrev;
        vs = vsPrev;
    }

private:
    /** Evaluates Id(vg,vd,vs) and its partial derivatives w.r.t. vd/vs,
        handling the drain/source symmetry (see class doc comment). */
    void evaluate (double vg, double vd, double vs, double& iD, double& diD_dvd, double& diD_dvs) const noexcept
    {
        const double vds = vd - vs;
        const bool swapped = vds < 0.0;

        // In the swapped case the equations below are evaluated with
        // drain/source roles exchanged (vgsEff uses the *other* terminal
        // as the reference), then the result and its derivatives are
        // mapped back and negated -- see class doc comment.
        const double vgsEff = swapped ? (vg - vd) : (vg - vs);
        const double vdsEff = swapped ? -vds : vds;
        const double vgst = vgsEff - Vp;

        double iEff, diEff_dvgsEff, diEff_dvdsEff;

        if (vgst <= 0.0)
        {
            iEff = 0.0;
            diEff_dvgsEff = 0.0;
            diEff_dvdsEff = 0.0;
        }
        else if (vdsEff < vgst)
        {
            // Triode/ohmic region.
            const double onePlusLambdaVds = 1.0 + lambda * vdsEff;
            iEff = beta * vdsEff * (2.0 * vgst - vdsEff) * onePlusLambdaVds;
            diEff_dvgsEff = beta * 2.0 * vdsEff * onePlusLambdaVds; // d(vgst)/d(vgsEff) == 1
            diEff_dvdsEff = beta * (2.0 * vgst - 2.0 * vdsEff) * onePlusLambdaVds
                             + beta * vdsEff * (2.0 * vgst - vdsEff) * lambda;
        }
        else
        {
            // Saturation region.
            const double onePlusLambdaVds = 1.0 + lambda * vdsEff;
            iEff = beta * vgst * vgst * onePlusLambdaVds;
            diEff_dvgsEff = beta * 2.0 * vgst * onePlusLambdaVds;
            diEff_dvdsEff = beta * vgst * vgst * lambda;
        }

        // Map vgsEff/vdsEff derivatives back to (vd, vs) in the actual
        // (non-swapped) coordinate system, then apply the sign flip.
        double diEff_dvd, diEff_dvs;
        if (! swapped)
        {
            // vgsEff = vg - vs, vdsEff = vd - vs
            diEff_dvd = diEff_dvdsEff;
            diEff_dvs = -diEff_dvgsEff - diEff_dvdsEff;
        }
        else
        {
            // vgsEff = vg - vd, vdsEff = -(vd - vs) = vs - vd
            diEff_dvd = -diEff_dvgsEff - diEff_dvdsEff;
            diEff_dvs = diEff_dvdsEff;
        }

        iD = swapped ? -iEff : iEff;
        diD_dvd = swapped ? -diEff_dvd : diEff_dvd;
        diD_dvs = swapped ? -diEff_dvs : diEff_dvs;
    }

    double Vp = -2.0;
    double beta = 1.0e-3;
    double lambda = 0.02;

    double vdPrev = 0.0, vsPrev = 0.0;
};

} // namespace openguitarmultifx
