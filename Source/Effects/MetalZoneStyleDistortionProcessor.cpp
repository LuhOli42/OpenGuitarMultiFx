#include "MetalZoneStyleDistortionProcessor.h"
#include "IconKit.h"
#include "NobelsCommon.h"
#include "PotTaper.h"

#include <IconData.h>

#include <map>
#include <mutex>

namespace openguitarmultifx
{

namespace
{
    using namespace nobels; // vBias, siIs, siNVt, jfetSwitchOhms

    constexpr double supplyVolts = 9.0;
    constexpr double distPotMax = 250.0e3;    // VR01, 250kA
    constexpr double toneA = 100.0e3;         // VR03a / VR03b / VR02b, 100kG
    constexpr double midFreqPotMax = 50.0e3;  // VR02a, 50kC x2
    constexpr double levelPotMax = 50.0e3;    // VR04, 50kA

    // M5218AL-class dual op-amp on a 9 V supply (the drawing names it; its output swings ~1.5 V short of each rail).
    const NodalCircuit::OpAmpMacro m5218 { 1.0e5, 2.5e6, 75.0, 1.5, 7.5, 0.0, 0.0 };
    // 2SC3378GR: a small-signal NPN (the drawing gives no gain figure).
    const NodalCircuit::BjtParams npn2SC3378 { 1.0e-14, 25.85e-3, 300.0, 4.0 };
    // 2SK184GR: the input JFET's Idss / pinch-off (same class of part as the BD-2's 2SK184).
    const NodalCircuit::JfetParams jfet2SK184 { 4.5e-3, -1.2, 0.02 };

    /** One of the drawing's emitter-follower "inductor" branches on an op-amp's (-) leg: C1 to N1; N1 -> rE -> the emitter;
        N1 -> C2 -> the base (rBase to the reference); the collector on the rail, the emitter's 10K to ground. */
    void addBootstrapBranch (NodalCircuit& c, NodalCircuit::Node inv, NodalCircuit::Node rail, NodalCircuit::Node vb,
                             double c1, double rE, double c2, double rBase)
    {
        const auto gnd = NodalCircuit::ground;
        const auto n1 = c.addNode(), b = c.addNode(), e = c.addNode();
        c.addCapacitor (inv, n1, c1);
        c.addResistor (n1, e, rE);
        c.addCapacitor (n1, b, c2);
        c.addResistor (b, vb, rBase);
        c.addBjt (rail, b, e, false, npn2SC3378);
        c.addResistor (e, gnd, 10.0e3);
        c.setInitialGuess (n1, vBias);
        c.setInitialGuess (b, vBias);
        c.setInitialGuess (e, vBias - 0.65);
    }

    /** A linear op-amp (finite gain, a dominant pole from its gain-bandwidth product, an output resistance, no swing limit): for a
        stage whose output cannot reach the rails, so it needs no block of its own. */
    void addLinearOpAmp (NodalCircuit& c, NodalCircuit::Node inPlus, NodalCircuit::Node inMinus, NodalCircuit::Node out, const NodalCircuit::OpAmpMacro& m)
    {
        const auto amp = c.addNode(), px = c.addNode(), ob = c.addNode();
        const double pole = m.gainBandwidth / m.dcGain;
        c.addFiniteGainOpAmp (inPlus, inMinus, amp, m.dcGain, 0.0, 0.0);
        c.addResistor (amp, px, 1.0e6);
        c.addCapacitor (px, NodalCircuit::ground, 1.0 / (2.0 * 3.14159265358979323846 * 1.0e6 * pole));
        c.addFollower (px, ob, 0.0);
        c.addResistor (ob, out, m.outputOhms);
    }

    // ---------- reducedOrder: what the three front blocks collapse to (docs/circuits/MetalZoneStyleDistortion.md) ----------
    //
    // The same circuit, same trapezoidal capacitor model, solved with scalar KCL instead of the
    // Newton-over-ports machine: every device that is not a clip element becomes its linear
    // equivalent (JFET/emitter followers -> addFollower's ideal part, BJT-emitter-follower
    // "inductors" -> an ideal follower), and each op-amp keeps the exact addOpAmpMacro structure
    // (finite gain -> R*C dominant pole -> diode-clamped integrator node -> rail-switched
    // follower -> 75 ohm). In the linear region each stage solves exactly, one shot; past the
    // rails a short fixed-point pass resolves the broken feedback loop, exactly like the
    // netlist's satMode switch. Nothing allocates.
    constexpr double reducedTheta = 0.6;      // NodalCircuit::defaultTheta -- the companion model must match
    constexpr double reducedHistW = (1.0 - reducedTheta) / reducedTheta;
    constexpr double macroPoleCap = 1.0 / (2.0 * 3.14159265358979323846 * 1.0e6 * 25.0); // m5218: GBW/dcGain = 25 Hz pole
    constexpr double railClampIs = 1.0e-9, railClampNVt = 25.85e-3; // addOpAmpMacro's integrator clamp diodes

    // MZ_PROFILE: rough iteration-cost counters, printed at exit (debug only, zero cost when unset)
    struct MzProf { long long newton = 0, pxIter = 0, legIter = 0, clipIter = 0, samples = 0; };
    inline MzProf mzProf;
    inline const bool mzProfiling = std::getenv ("MZ_PROFILE") != nullptr;
    struct MzProfDump
    {
        ~MzProfDump()
        {
            if (mzProfiling)
                juce::Logger::writeToLog (juce::String::formatted (
                    "mzprof: %.2f newton, %.2f pxIter, %.2f legIter, %.2f clipIter per sample (%lld samples)",
                    (double) mzProf.newton / juce::jmax (1LL, mzProf.samples),
                    (double) mzProf.pxIter / juce::jmax (1LL, mzProf.samples),
                    (double) mzProf.legIter / juce::jmax (1LL, mzProf.samples),
                    (double) mzProf.clipIter / juce::jmax (1LL, mzProf.samples), mzProf.samples));
        }
    };
    inline MzProfDump mzProfDump;
    constexpr double jfetDrop = -1.05;                            // Q011 source follower DC seed (fitted)
    constexpr double jfetIdss = 4.5e-3, jfetPinch = -1.2, jfetLambda = 0.02;   // jfet2SK184
    constexpr double followerDrop = 0.65;                          // bootstrap-branch emitter followers

    /** SPICE's pnjlim: compresses a large forward-voltage Newton step logarithmically (same helper
        NodalCircuit and EbersMollBJT use). Needed anywhere the reduced model solves a diode junction
        on its own: a plain Newton step on an exponential overshoots volts past the knee and then
        sheds only ~nVt per iteration on the way back -- the iterate never reaches the clamp inside
        the iteration budget and the node runs unclamped. */
    double limitJunction (double vnew, double vold, double vt, double vcrit) noexcept
    {
        if (vnew > vcrit && std::abs (vnew - vold) > 2.0 * vt)
        {
            if (vold > 0.0)
            {
                const double arg = (vnew - vold) / vt;
                vnew = arg > 0.0 ? vold + vt * (2.0 + std::log (arg - 2.0))
                                 : vold - vt * (2.0 + std::log (2.0 - arg));
            }
            else
                vnew = vt * std::log (vnew / vt);
        }
        else if (vnew < 0.0)
        {
            const double arg = vold > 0.0 ? -vold - 1.0 : 2.0 * vold - 1.0;
            if (vnew < arg)
                vnew = arg;
        }
        return vnew;
    }

    struct ReducedCap
    {
        double g = 0.0, vp = 0.0, ip = 0.0, ieq = 0.0;
        void prepare (double farads, double fs) noexcept { g = farads * fs / reducedTheta; vp = ip = ieq = 0.0; }
        void begin() noexcept { ieq = g * vp + reducedHistW * ip; }
        void commit (double v) noexcept { ip = g * v - ieq; vp = v; }
    };

    /** The integrator node of one addOpAmpMacro: px solves g_rc (vAmp - px) = i_C + the two rail
        clamp diodes; the follower output is px inside the rails, held at a rail outside. */
    struct ReducedAmp
    {
        ReducedCap cpx;
        double px = vBias, out = vBias, vm = vBias;   // vm: last m-node voltage (Newton warm start)

        void prepare (double fs) noexcept { cpx.prepare (macroPoleCap, fs); px = out = vm = vBias; }

        double solvePx (double vAmp) noexcept
        {
            const double vcritR = railClampNVt * std::log (railClampNVt / (1.4142135623730951 * railClampIs));
            double v = px;
            for (int i = 0; i < 8; ++i)
            {
                const double eh = std::exp (std::min ((v - m5218.highRail) / railClampNVt, 40.0));
                const double el = std::exp (std::min ((m5218.lowRail - v) / railClampNVt, 40.0));
                const double f = 1.0e-6 * (vAmp - v) - cpx.g * v + cpx.ieq - railClampIs * (eh - el);
                const double d = -1.0e-6 - cpx.g - railClampIs / railClampNVt * (eh + el);
                // The clamp diodes hold the integrator node ~0.7 V past whichever rail it
                // presses against; without this bound a wild vAmp throws v so far out that
                // the exp()'s dwarf every other term and the Newton stalls off-rail forever.
                // Junction-limit the step on whichever clamp diode the candidate pushes forward
                // (v-highRail for the top one, lowRail-v for the bottom) -- a huge vAmp overshoot
                // otherwise lands so deep in saturation that the Newton crawls back ~nVt/iter.
                const double cand = v - f / d;
                if (mzProfiling) ++mzProf.pxIter;
                const double lTop = m5218.highRail + limitJunction (cand - m5218.highRail, v - m5218.highRail,
                                                                    railClampNVt, vcritR);
                const double lBot = m5218.lowRail - limitJunction (m5218.lowRail - cand, m5218.lowRail - v,
                                                                   railClampNVt, vcritR);
                const double next = std::abs (lTop - v) <= std::abs (lBot - v) ? lTop : lBot;
                v = juce::jlimit (m5218.lowRail - 1.0, m5218.highRail + 1.0, next);
                if (std::abs (f / d) < 1.0e-10)
                    break;
            }
            return v;
        }
    };

    /** Surfaces of a BootstrapLeg's interior solve over the (u, q) domain it actually depends
        on: u = c1.g*vm - c1.ieq + c2.ieq carries the drive plus both cap histories, q = c2.ieq
        the base-KCL offset. u spans volts; q only milliamps, so three q-slices around the
        measured range + a quadratic in q covers it -- Catmull-Rom cubic in u. Built once per
        sample rate and shared by every channel and instance: the values depend only on the
        leg's fixed component values and fs. */
    struct LegLut
    {
        // outputs {v1, vb, ve} x 5 q-slices over +-h -> quadratic in q on the
        // nearest triplet, CR cubic in u
        static constexpr int NU = 2048, NS = 15;
        std::array<std::array<double, NU>, NS> s;
        double u0 = 0.0, invDu = 1.0, q0 = 0.0, invH = 1.0;
    };

    /** IC3b/IC4b's inverting-leg branch: series cap C1 into node n1; n1 -> rE -> the emitter;
        n1 -> C2 -> the base; base -> rBase -> vBias; collector pinned to the rail (a ONE-port
        2SC3378 on vbe -- see addBootstrapBranch in the netlist for the topology this mirrors).
        The leg is one nonlinear admittance i_leg(v_m): the follower's real Vbe knee, its
        can't-sink cut-off and its base current are worth ~8 dB of worst-band error when
        idealised (the earlier affine version). The m-node stays solvable because the stage's
        scalar Newton on v_m -- stageNewton() -- just calls eval() per iteration: the leg's own
        2x2 Newton on (v_b, v_e) is warm-started, bounded, and monotonic, so it converges in a
        handful of iterations where every scheme that coupled the BJT into the output iterate
        cycled across the rails (docs/circuits/MetalZoneStyleDistortion.md). */
    struct BootstrapLeg
    {
        ReducedCap c1, c2;
        double f1, f2, rE, rBase;
        double gE = 0.0, gB = 0.0, v1 = vBias, vb = vBias, ve = vBias - followerDrop;
        int id = 0;
        const LegLut* lut = nullptr; // set in prepare; table is shared const state, off-heap

        // npn2SC3378 as a one-port (collector on the fixed 9 V rail): iE_out = isEff*(e^vbe-1),
        // iB = isB*(e^vbe-1), both ~exact while vbc stays deeply reverse (base can reach at
        // most ~7.5 V, still 1.5 V under the rail). Constants mirror npn2SC3378 {Is, Vt, betaF}.
        static constexpr double isEff = 1.0e-14 * (1.0 + 1.0 / 300.0);
        static constexpr double isB = 1.0e-14 / 300.0;
        static constexpr double invVt = 1.0 / 25.85e-3;
        static constexpr double gE10k = 1.0e-4; // the emitter's 10K to ground

        BootstrapLeg (double c1F, double c2F, double emitterOhms, double baseOhms, int legId)
            : f1 (c1F), f2 (c2F), rE (emitterOhms), rBase (baseOhms), id (legId) {}

        void prepare (double fs) noexcept
        {
            c1.prepare (f1, fs);
            c2.prepare (f2, fs);
            gE = 1.0 / rE;
            gB = 1.0 / rBase;
            v1 = vBias - followerDrop;
            vb = vBias;
            ve = vBias - followerDrop;
        }

        /** The interior solve, parameterised by the two scalars it actually depends on:
            u = c1.g*vm - c1.ieq + c2.ieq (drive + both cap histories folded into one
            current) and q = c2.ieq (the base-KCL history offset). Solves (vb, ve) and
            writes v1/vb/ve for commit(). */
        void solveU (double u, double q, int maxIters = 10) noexcept
        {
            double b = vb, e = ve;
            for (int i = 0; i < maxIters; ++i)
            {
                const double ex = std::exp (std::min ((b - e) * invVt, 40.0));
                const double iEo = isEff * (ex - 1.0), iB = isB * (ex - 1.0);
                const double gT = isEff * invVt * ex, gBi = isB * invVt * ex;
                // emitter KCL solved for v1: iEo is the current the device pushes INTO node e,
                // so gE*(v1 - e) = gE10k*e - iEo (at DC: v1 = e, iEo = gE10k*e).
                const double n1 = e + (gE10k * e - iEo) / gE;
                const double dv1de = 1.0 + (gE10k + gT) / gE, dv1db = -gT / gE;
                // base KCL: current b->v1 through C2 plus rBase plus iB = 0
                const double fb = c2.g * (b - n1) + q + gB * (b - vBias) + iB;
                if (mzProfiling) ++mzProf.legIter;
                // n1 KCL with u standing in for c1.g*vm - c1.ieq + c2.ieq
                const double fn = (c1.g + gE + c2.g) * n1 - u - gE * e - c2.g * b;
                const double j11 = c2.g * (1.0 - dv1db) + gB + gBi;
                const double j12 = -c2.g * dv1de - gBi;
                const double j21 = (c1.g + gE + c2.g) * dv1db - c2.g;
                const double j22 = (c1.g + gE + c2.g) * dv1de - gE - c2.g;
                const double det = j11 * j22 - j12 * j21;
                double db = (j22 * fb - j12 * fn) / det;
                double de = (j11 * fn - j21 * fb) / det;
                // Junction limiting (pnjlim-lite): the vbe move is capped per iteration.
                const double dvbe = (b - db) - (e - de) - (b - e);
                if (std::abs (dvbe) > 0.5)
                {
                    const double s = 0.5 / std::abs (dvbe);
                    db *= s;
                    de *= s;
                }
                db = juce::jlimit (-1.0, 1.0, db);
                de = juce::jlimit (-1.0, 1.0, de);
                b -= db;
                e -= de;
                if (std::abs (db) + std::abs (de) < 1.0e-9)
                    break;
            }
            vb = b;
            ve = e;
            const double ex = std::exp (std::min ((b - e) * invVt, 40.0));
            v1 = e + (gE10k * e - isEff * (ex - 1.0)) / gE;
        }

        /** dv1/dvm at the solved point: differentiate the three KCLs with dvm = 1
            (i.e. du/dvm = c1.g). */
        double dIdvAtSolved() const noexcept
        {
            const double ex = std::exp (std::min ((vb - ve) * invVt, 40.0));
            const double gT = isEff * invVt * ex, gBi = isB * invVt * ex;
            const double d = gE + gE10k + gT;
            const double r1 = (c2.g + gBi * gE / d) / (c2.g + gB + gBi - gBi * gT / d);
            const double r2 = (gE + gT * r1) / d;
            const double dv1 = c1.g / (c1.g + gE + c2.g - gE * r2 - c2.g * r1);
            return c1.g * (1.0 - dv1);
        }

        /** The leg current out of the m node at terminal voltage vm, with the interior nodes
            (v1, v_b, v_e) solved to KCL for this sample's capacitor state. Writes the solved
            v1/vb/ve for commit() and returns the differential conductance through dIdv. */
        double eval (double vm, double& dIdv) noexcept
        {
            const double u = c1.g * vm - c1.ieq + c2.ieq;
            const double q = c2.ieq;
            if (lut != nullptr)
            {
                const double fu = (u - lut->u0) * lut->invDu;
                const int iu = (int) std::floor (fu);
                const double dq = q - lut->q0;
                const double tq = dq * lut->invH; // slice units: |tq| <= 2 covers |dq| <= qH
                if (iu >= 1 && iu <= LegLut::NU - 3 && std::abs (tq) <= 2.0)
                {
                    const double t = fu - iu;
                    const double w0 = t * (t * (1.0 - 0.5 * t) - 0.5);
                    const double w1 = t * t * (1.5 * t - 2.5) + 1.0;
                    const double w2 = t * (t * (2.0 - 1.5 * t) + 0.5);
                    const double w3 = t * t * (0.5 * t - 0.5);
                    // all 5 q-slices through one quartic (Lagrange on {-2,-1,0,1,2}) --
                    // smooth across the whole q range, no kinks at triplet boundaries
                    const double x = tq, x2 = x * x;
                    const double qw[5] = {
                        x * (x2 - 1.0) * (x - 2.0) / 24.0,
                        -x * (x2 - 4.0) * (x - 1.0) / 6.0,
                        (x2 - 4.0) * (x2 - 1.0) / 4.0,
                        -x * (x2 - 4.0) * (x + 1.0) / 6.0,
                        x * (x2 - 1.0) * (x + 2.0) / 24.0 };
                    double uq[LegLut::NS];
                    for (int k = 0; k < LegLut::NS; ++k)
                    {
                        const double* p = lut->s[k].data() + (iu - 1);
                        uq[k] = w0 * p[0] + w1 * p[1] + w2 * p[2] + w3 * p[3];
                    }
                    double out[3];
                    for (int o = 0; o < 3; ++o)
                    {
                        const int base = o * 5;
                        out[o] = qw[0] * uq[base] + qw[1] * uq[base + 1] + qw[2] * uq[base + 2]
                               + qw[3] * uq[base + 3] + qw[4] * uq[base + 4];
                    }
                    v1 = out[0]; vb = out[1]; ve = out[2];
                    dIdv = dIdvAtSolved(); // closed form is exact and cheap; no need for a surface
                    return u - q - c1.g * v1;
                }
            }
            solveU (u, q);
            dIdv = dIdvAtSolved();
            return u - q - c1.g * v1;
        }

        void commit (double vm) noexcept
        {
            c1.commit (vm - v1);
            c2.commit (v1 - vb);
        }
    };

    // The three legs' component values, shared between the member initializers below and the
    // LUT builder (single source so a re-tune can never drift the table off the real branch).
    struct LegParams { double f1, f2, rE, rB; };
    constexpr std::array<LegParams, 3> legParams = { {
        { 0.027e-6, 0.01e-6, 2.2e3, 47.0e3 },   // leg3b  (Q010 on IC3b's (-))
        { 0.015e-6, 0.0015e-6, 1.0e3, 47.0e3 }, // legA4b (Q008 on IC4b's (-))
        { 0.22e-6, 0.047e-6, 470.0, 470.0e3 },  // legB4b (Q007 on IC4b's (-))
    } };

    // (u, q) extents measured by MZ_PROFILE on the DI render at 48 kHz, padded ~2-4x for knob
    // extremes and wild Newton trials; u and q both scale with the caps' conductance, i.e. with
    // fs. Outside the box eval() still takes the exact Newton -- padding only buys speed.
    struct LegLutDomain { double uLo, uHi, q0, qH; };
    constexpr std::array<LegLutDomain, 3> legLutDomains = { {
        { -0.005, 0.05, -0.00046, 0.0006 },  // measured u [0.0066, 0.010], q [-0.00052, -0.00039]
        { -0.005, 0.05, -0.00003, 0.0003 },  // measured u [0.0024, 0.009], q [-0.00008, 0.00001]
        { -0.01, 0.50, -0.0013, 0.003 },     // measured u [0.0225, 0.145], q [-0.00244, -0.00025]
    } };

    void buildLegLut (const LegParams& p, double fs, const LegLutDomain& d, LegLut& lut)
    {
        BootstrapLeg leg (p.f1, p.f2, p.rE, p.rB, 0);
        leg.prepare (fs);
        const double du = (d.uHi - d.uLo) / (LegLut::NU - 3); // nodes 1..NU-2 span [uLo, uHi]
        lut.u0 = d.uLo - du;
        lut.invDu = 1.0 / du;
        lut.q0 = d.q0;
        lut.invH = 2.0 / d.qH; // slice spacing is qH/2
        // The (u, q) -> root map is multi-valued at the domain edges, so build outward from
        // the DC point with warm start -- that follows the physical branch the runtime's
        // warm-started Newton stays on, instead of whatever the edge raster first finds.
        const double uDc = p.f1 * fs / reducedTheta * vBias; // caps open, vm at vBias
        const int iuDc = juce::jmax (1, (int) std::lround ((uDc - lut.u0) * lut.invDu));
        for (int slice = 0; slice < 5; ++slice)
        {
            const double q = d.q0 + (slice - 2) * d.qH * 0.5;
            leg.vb = vBias;
            leg.ve = vBias - followerDrop;
            double bDc = 0.0, eDc = 0.0;
            for (int step = 0; step < LegLut::NU; ++step)
            {
                const int iu = step <= LegLut::NU - 1 - iuDc ? iuDc + step : iuDc - (step - (LegLut::NU - 1 - iuDc));
                if (step == LegLut::NU - iuDc) { leg.vb = bDc; leg.ve = eDc; } // left sweep reseeds at DC
                leg.solveU (lut.u0 + iu * du, q, 200);
                if (iu == iuDc) { bDc = leg.vb; eDc = leg.ve; }
                const size_t i = (size_t) iu;
                lut.s[slice][i] = leg.v1;
                lut.s[5 + slice][i] = leg.vb;
                lut.s[10 + slice][i] = leg.ve;
            }
        }
    }

    struct ReducedLegLuts { std::array<LegLut, 3> t; };

    /** Tables keyed by sample rate, built once under a mutex in prepare() -- the audio thread
        only ever reads them through the legs' const pointers. */
    const ReducedLegLuts& reducedLegLuts (double fs)
    {
        static std::mutex lutMutex;
        static std::map<double, std::unique_ptr<ReducedLegLuts>> cache;
        const std::lock_guard<std::mutex> lock (lutMutex);
        auto& slot = cache[fs];
        if (slot == nullptr)
        {
            slot = std::make_unique<ReducedLegLuts>();
            const double k = fs / 48000.0;
            for (size_t i = 0; i < 3; ++i)
            {
                const auto& d = legLutDomains[i];
                buildLegLut (legParams[i], fs, { d.uLo * k, d.uHi * k, d.q0 * k, d.qH * k }, slot->t[i]);
            }
        }
        return *slot;
    }
}

/** The reducedOrder substitute for blocks 0-2. See the netlist in buildChannel() for the topology
    this mirrors -- node names match, the comments there explain the circuit itself. */
struct MetalZoneStyleDistortionProcessor::ReducedFrontEnd
{
    // input ladder: (10k + C042) -> g -> [JFET follower] -> C039 -> nA -> 200R -> nB -> C033 -> nC
    ReducedCap c042, c039, c033;
    // IC3b: fb 220k || C032; (-) leg = the Q010 bootstrap branch
    ReducedCap c032;
    BootstrapLeg leg3b { legParams[0].f1, legParams[0].f2, legParams[0].rE, legParams[0].rB, 0 };
    ReducedAmp amp3b;
    // IC3a interstage: p {10k, C031} -> C029 -> q {100k}; leg = 1k -> nR -> C030; fb = (rDist+1k) || C028
    ReducedCap c031, c029, c030, c028;
    ReducedAmp amp3a;
    // clipper: C027 -> n27 -> 2k2 -> k {diode pair, 10k -> l}; l: 4k7 -> n31 {C023}, C021 -> m2 {100k}
    ReducedCap c027, c023, c021;
    // IC4b: fb 3k3 || C022; legs = the Q008 and Q007 bootstrap branches
    ReducedCap c022;
    BootstrapLeg legA4b { legParams[1].f1, legParams[1].f2, legParams[1].rE, legParams[1].rB, 1 };
    BootstrapLeg legB4b { legParams[2].f1, legParams[2].f2, legParams[2].rE, legParams[2].rB, 2 };
    ReducedAmp amp4b;
    // IC4a inverter: 22k in, 22k || C018 fb, (+) at vBias
    ReducedCap c018;
    ReducedAmp amp4a;

    double gDist = 1.0 / 2.0e3;   // 1 / (rDist + R051): set from updatePots()
    double vk = 0.0;              // clipper node warm start
    double vm4aPrev = vBias;      // IC4a's inverting node one sample back: the 22K load on IC4b's `o`
    double dbgN27 = 0.0, dbgL = 0.0, dbgM2 = 0.0, dbgPrev2 = 0.0; // clipper internals, probed like the netlist's

    // probe mirrors of the netlist taps (vBias-centred, same picture)
    double ic3b = vBias, ic3a = vBias, ic4b = vBias, ic4a = vBias, ic3aPrev = vBias, ic3aP = vBias, ic3aQ = vBias;
    double jfetS = vBias + 0.9;   // Q011 source warm start (real solve below)

    void prepare (double fs) noexcept
    {
        const double v = vBias;
        c042.prepare (0.047e-6, fs);
        c039.prepare (1.0e-6, fs);
        c033.prepare (0.015e-6, fs);
        c032.prepare (100.0e-12, fs);
        leg3b.prepare (fs);
        amp3b.prepare (fs);
        c031.prepare (0.047e-6, fs);
        c029.prepare (0.033e-6, fs);
        c030.prepare (10.0e-6, fs);
        c028.prepare (47.0e-12, fs);
        amp3a.prepare (fs);
        c027.prepare (10.0e-6, fs);
        c023.prepare (0.015e-6, fs);
        c021.prepare (1.0e-6, fs);
        c022.prepare (47.0e-12, fs);
        legA4b.prepare (fs);
        legB4b.prepare (fs);
        const auto& luts = reducedLegLuts (fs);
        {
            leg3b.lut = &luts.t[0];
            legA4b.lut = &luts.t[1];
            legB4b.lut = &luts.t[2];
        }
        amp4b.prepare (fs);
        c018.prepare (10.0e-12, fs);
        amp4a.prepare (fs);
        // Seed the DC operating point directly (caps open, amps linear at vBias) -- running the
        // solver from zero falls into a parasitic fixed point where an amp sits at its rail.
        c042.vp = -v;                     // gate at vBias, nR at vin = 0
        c039.vp = -jfetDrop;              // s at vBias - drop, nA at vBias
        c033.vp = 0.0;                    // nB = nC = vBias
        c032.vp = 0.0;                    // m = o = vBias
        leg3b.c1.vp = followerDrop;       // m at vBias, v1 at vBias - drop
        leg3b.c2.vp = -followerDrop;      // v1 at vBias - drop, base at vBias
        c031.vp = 0.0;
        c029.vp = 0.0;
        c030.vp = v;                      // nR at vm = vBias (no DC path to ground)
        c028.vp = 0.0;
        c027.vp = v;                      // prev at vBias, n27 pulled to ~0 by the diodes
        c023.vp = 0.0;
        c021.vp = -v;                     // l at ~0, m2 at vBias
        c022.vp = 0.0;
        legA4b.c1.vp = followerDrop;
        legA4b.c2.vp = -followerDrop;
        legB4b.c1.vp = followerDrop;
        legB4b.c2.vp = -followerDrop;
        c018.vp = 0.0;
        amp3b.cpx.vp = v;
        amp3a.cpx.vp = v;
        amp4b.cpx.vp = v;
        amp4a.cpx.vp = v;
        vk = 0.0;
        vm4aPrev = v;
        ic3b = ic3a = ic4b = ic4a = ic3aPrev = ic3aP = ic3aQ = v;
        // Settle until the probe nodes stop moving: the clipper's 10 uF input cap discharges
        // through ~2.3 kOhm (tau ~20 ms = ~400-800 sample steps), so a fixed 300-step budget
        // leaves ic4b/ic4a visibly off-bias. Adaptive exit is cheaper than a bigger fixed count.
        double last[8] = {};
        for (int i = 0; i < 20000; ++i)
        {
            step (0.0);
            const double now[8] = { ic3b, ic3a, ic4b, ic4a, vk, dbgL, dbgM2, dbgPrev2 };
            double maxDelta = 0.0;
            for (int j = 0; j < 8; ++j)
                maxDelta = juce::jmax (maxDelta, std::abs (now[j] - last[j]));
            if (i >= 64 && maxDelta < 1.0e-7)
                break;
            std::memcpy (last, now, sizeof (now));
        }
    }

    /** One feedback-amp macro stage: the m-node KCL solved by a scalar Newton on v_m, so each
        BJT leg is evaluated EXACTLY at the trial voltage (BootstrapLeg::eval) instead of being
        folded into a fixed admittance. v_out is a function of v_m through the integrator node:
        linear inside the rails, the clamp-diode solve outside -- same two cases the old closed
        form split on. F(v_m) is monotonically decreasing (feedback + leg conductances all pull
        down), which is why the Newton is safe here where the output-iterate loop was not.
        kFb = feedback conductance sum; gSum = kFb + the AFFINE leg admittances only (any real
        legs come through `legs`); hm = the m-node history/drive terms.
        fbIeq/gOutExtra/vOutExtraSum model the op-amp's `o` node: in the netlist the stage output
        sits behind the macro's outputOhms and droops under the block's own loads --
        o = ob - rOut*(kFb*(o - vm) + fbIeq + gOutExtra*(o - vX)), solved in closed form below.
        Every m-KCL element and every downstream tap sees `o`, not the rail-limited ideal `ob`.
        For IC4b the load that matters is R028 22K into IC4a's inverting node (~0.18 V of sag at
        the rail); the rest is millivolts. */
    static double stageNewton (double vPlus, double kFb, double gSum, double hm, double fbIeq,
                               double gOutExtra, double vOutExtraSum, ReducedAmp& amp,
                               BootstrapLeg* const* legs, int numLegs) noexcept
    {
        const double gRc = 1.0e-6, gain = m5218.dcGain;
        const double oDenom = 1.0 + m5218.outputOhms * (kFb + gOutExtra);
        // F(vm) = kFb*vo(vm) - gSum*vm + hm - iLegs(vm) is continuous and monotone decreasing
        // (vo falls with vm through the ~196x integrator slope, then pins at a rail). An
        // unbracketed Newton never sees the razor-thin linear window: the iterate slams between
        // the two railed equilibria forever (the documented "Newton rails cycle"). Keep a sign
        // bracket on vm; Newton inside it, bisection when Newton would leave it — converges
        // unconditionally, and quadratically once the iterate lands in the linear window.
        if (numLegs == 0)
        {
            // With no legs F is piecewise-AFFINE in vm: in the linear band ob = pxLin
            // (affine in vm), deep past a rail ob = the rail constant. The only non-affine
            // stretch is the ~0.5 V clamp-diode transition hugging each rail. Solve each
            // region's affine root and keep the one that lands inside its own region;
            // only fall through to the Newton when the root sits in a transition band.
            const double k1 = gRc * gain / (gRc + amp.cpx.g); // d pxLin/d vm ~ -196
            const double k0 = (gRc * gain * vPlus + amp.cpx.ieq) / (gRc + amp.cpx.g);
            const double c = m5218.outputOhms * (vOutExtraSum - fbIeq);
            auto affineRoot = [&] (double obConst, bool linearOb) -> double
            {
                const double a = (obConst + (linearOb ? k0 : 0.0) + c) / oDenom;
                const double bVo = ((linearOb ? -k1 : 0.0) + m5218.outputOhms * kFb) / oDenom;
                return (kFb * a + hm) / (gSum - kFb * bVo);
            };
            double vmA = affineRoot (0.0, true);
            double pxA = k0 - k1 * vmA;
            if (pxA > m5218.lowRail && pxA < m5218.highRail)
            {
                const double voA = (pxA + m5218.outputOhms * (kFb * vmA - fbIeq + vOutExtraSum)) / oDenom;
                amp.vm = vmA;
                amp.px = pxA;
                amp.out = voA;
                amp.cpx.commit (pxA);
                return voA;
            }
            const bool highSide = pxA >= m5218.highRail;
            const double rail = highSide ? m5218.highRail : m5218.lowRail;
            vmA = affineRoot (rail, false);
            pxA = k0 - k1 * vmA;
            if (highSide ? pxA >= m5218.highRail + 0.5 : pxA <= m5218.lowRail - 0.5)
            {
                const double voA = (rail + m5218.outputOhms * (kFb * vmA - fbIeq + vOutExtraSum)) / oDenom;
                amp.vm = vmA;
                amp.px = amp.solvePx (gain * (vPlus - vmA));
                amp.out = voA;
                amp.cpx.commit (amp.px);
                return voA;
            }
        }
        double lo = -30.0, hi = 30.0;
        double vm = juce::jlimit (lo, hi, amp.vm); // warm start on last sample's inverting node
        double vo = amp.out, px = amp.px;
        for (int it = 0; it < 40; ++it)
        {
            const double pxLin = (gRc * gain * (vPlus - vm) + amp.cpx.ieq) / (gRc + amp.cpx.g);
            double ob, dOb;
            if (pxLin > m5218.lowRail && pxLin < m5218.highRail)
            {
                ob = pxLin;
                px = pxLin;
                dOb = -gRc * gain / (gRc + amp.cpx.g);
            }
            else if (pxLin >= m5218.highRail + 0.5 || pxLin <= m5218.lowRail - 0.5)
            {
                // Deep in saturation the integrator node lands ~0.65 V past the rail, so the
                // follower output is exactly the rail -- no need for the px Newton per iteration.
                // The committed px is resolved once below (the cap voltage does matter).
                ob = pxLin >= m5218.highRail + 0.5 ? m5218.highRail : m5218.lowRail;
                dOb = 0.0;
            }
            else
            {
                px = amp.solvePx (gain * (vPlus - vm));
                ob = juce::jlimit (m5218.lowRail, m5218.highRail, px);
                dOb = 0.0; // output pinned: only the legs move F from here
            }
            // o = (ob + rOut*(kFb*vm - fbIeq + gOutExtra*vX)) / (1 + rOut*(kFb + gOutExtra))
            vo = (ob + m5218.outputOhms * (kFb * vm - fbIeq + vOutExtraSum)) / oDenom;
            const double dVo = (dOb + m5218.outputOhms * kFb) / oDenom;
            // m-node KCL: kFb*(vo - vm) + (history/drive) - (affine-leg currents) - (real-leg currents) = 0,
            // i.e. kFb*vo - gSum*vm + hm - iLegs, where gSum = kFb + affine legs and hm folds their offsets.
            double f = kFb * vo - gSum * vm + hm;
            double df = kFb * dVo - gSum;
            for (int l = 0; l < numLegs; ++l)
            {
                double di = 0.0;
                f -= legs[l]->eval (vm, di);
                df -= di;
            }
            // F decreasing: f > 0 means the root is right of vm, f < 0 means left.
            if (f > 0.0)
                lo = juce::jmax (lo, vm);
            else
                hi = juce::jmin (hi, vm);
            if (mzProfiling) ++mzProf.newton;
            double next = vm - f / df;
            if (! (next > lo && next < hi))
                next = 0.5 * (lo + hi);
            if (std::abs (next - vm) < 1.0e-10)
            {
                vm = next;
                break;
            }
            vm = next;
        }
        // Resolve the integrator node exactly once for the committed cap voltage when the
        // converged iterate skipped the per-iteration solve (deep-rail fast path above).
        const double pxLinF = (gRc * gain * (vPlus - vm) + amp.cpx.ieq) / (gRc + amp.cpx.g);
        px = pxLinF <= m5218.lowRail || pxLinF >= m5218.highRail ? amp.solvePx (gain * (vPlus - vm))
                                                                : pxLinF;
        amp.vm = vm;
        amp.px = px;
        amp.out = vo;
        amp.cpx.commit (px);
        return vo;
    }

    double step (double vin) noexcept
    {
        if (mzProfiling) ++mzProf.samples;
        c042.begin(); c039.begin(); c033.begin(); c032.begin();
        leg3b.c1.begin(); leg3b.c2.begin();
        c031.begin(); c029.begin(); c030.begin(); c028.begin();
        c027.begin(); c023.begin(); c021.begin(); c022.begin();
        legA4b.c1.begin(); legA4b.c2.begin();
        legB4b.c1.begin(); legB4b.c2.begin();
        c018.begin();
        amp3b.cpx.begin(); amp3a.cpx.begin(); amp4b.cpx.begin(); amp4a.cpx.begin();

        // ---- input ladder: in ->(10k+C042)-> gate ->[JFET follower]-> C039 -> nA ->200R-> nB ->C033-> nC
        const double gE1 = c042.g / (1.0 + c042.g * 10.0e3), hE1 = c042.ieq / (1.0 + c042.g * 10.0e3);
        const double vg = (gE1 * vin - hE1 + 1.0e-6 * vBias) / (gE1 + 1.0e-6); // gate is b-side of C042
        // Q011's 2SK184GR source follower, drain pinned to the 9 V rail, source -> 10K to ground.
        // It sits near pinch-off (vgs ~= -0.9 of -1.2) where the transconductance swings hard, so a
        // fixed-gain follower misses the 2nd-order sag the real part adds: solve iD(vgs,vds) = vs/10K.
        double vs = jfetS;
        for (int i = 0; i < 6; ++i)
        {
            const double vds = 9.0 - vs;
            const double u = juce::jmax (0.0, 1.0 - (vg - vs) / jfetPinch);
            const double iD = jfetIdss * u * u * (1.0 + jfetLambda * vds);
            const double f = iD - vs * 1.0e-4;
            const double df = -jfetIdss * u * (2.0 * (1.0 + jfetLambda * vds) / -jfetPinch + jfetLambda * u) - 1.0e-4;
            vs = juce::jlimit (0.0, 9.0, vs - f / df);
            if (std::abs (f / df) < 1.0e-9)
                break;
        }
        jfetS = vs;
        const double gA = 1.0e-5, gSw = 1.0 / jfetSwitchOhms, gM = 1.0e-6;
        const double aC = c033.g / (c033.g + gA), bC = (-c033.ieq + gA * vBias) / (c033.g + gA); // nC = aC*nB + bC
        const double dB = gSw + gM + c033.g * (1.0 - aC);
        const double aB = gSw / dB, bB = (gM * vBias + c033.ieq + c033.g * bC) / dB;           // nB = aB*nA + bB
        const double vnA = (c039.g * vs - c039.ieq + gA * vBias + gSw * bB) / (c039.g + gA + gSw - gSw * aB); // nA is b-side of C039
        const double vnB = aB * vnA + bB;
        const double vnC = aC * vnB + bC;

        // ---- IC3b ----
        const double kFb3b = 1.0 / 220.0e3 + c032.g;
        BootstrapLeg* const legs3b[] = { &leg3b };
        const double vo3b = stageNewton (vnC, kFb3b, kFb3b, c032.ieq, c032.ieq, 0.0, 0.0, amp3b, legs3b, 1);
        const double vm3b = amp3b.vm;
        ic3b = vo3b;

        // ---- interstage + IC3a ----
        // The block boundary is src ->rOut(75)-> prev ->R045 10K-> p, so the series path is 10K+75.
        const double g45 = 1.0 / 10075.0, g42 = 1.0e-4, g40 = 1.0e-5;
        const double aQ = c029.g / (c029.g + g40), bQ = (-c029.ieq + g40 * vBias) / (c029.g + g40); // q = aQ*p + bQ
        const double vp = (g45 * vo3b + g42 * vBias + c031.g * vBias + c031.ieq + c029.ieq + c029.g * bQ)
                          / (g45 + g42 + c031.g + c029.g * (1.0 - aQ));
        const double vq = aQ * vp + bQ;
        const double g41 = 1.0e-3, kLeg3a = g41 * c030.g / (g41 + c030.g), cLeg3a = -g41 * c030.ieq / (g41 + c030.g);
        const double kFb3a = gDist + c028.g;
        const double g3a = kFb3a + kLeg3a;
        const double hm3a = c028.ieq - cLeg3a;
        const double vo3a = stageNewton (vq, kFb3a, g3a, hm3a, c028.ieq, 0.0, 0.0, amp3a, nullptr, 0);
        const double vm3a = amp3a.vm;
        const double vnR3a = (g41 * vm3a + c030.ieq) / (g41 + c030.g);
        ic3a = vo3a;
        ic3aPrev = vo3b - m5218.outputOhms * g45 * (vo3b - vp); // prev = src - rOut*i45
        ic3aP = vp;
        ic3aQ = vq;

        // ---- clipper: (boundary 75R + C027) -> n27 -> 2k2 -> k {diode pair to gnd, 10k -> l}; l: 4k7 -> n31 {C023}, C021 -> m2 {100k}
        const double gE27 = c027.g / (1.0 + c027.g * m5218.outputOhms), hE27 = c027.ieq / (1.0 + c027.g * m5218.outputOhms);
        const double g28 = 1.0 / 2.2e3, g32 = 1.0e-4, g47 = 1.0 / 4.7e3, g29 = 1.0e-5;
        const double a31 = g47 / (g47 + c023.g), b31 = c023.ieq / (g47 + c023.g);
        const double a2 = c021.g / (c021.g + g29), b2 = (-c021.ieq + g29 * vBias) / (c021.g + g29);
        const double dL = g32 + g47 * (1.0 - a31) + c021.g * (1.0 - a2);
        const double aL = g32 / dL, bL = (c021.ieq + g47 * b31 + c021.g * b2) / dL;
        const double aK = g28 * gE27 / (gE27 + g28) + g32 * (1.0 - aL);
        const double bK = g28 * (gE27 * vo3a - hE27) / (gE27 + g28) + g32 * bL;
        // pnjlim on whichever diode of the pair the step pushes forward: without it a hard drive
        // overshoots past the knee and the Newton sheds ~siNVt/iteration -- vk never reaches the
        // clamp inside the budget and the clip node runs unclamped (was the top residual).
        const double vcritD = siNVt * std::log (siNVt / (1.4142135623730951 * siIs));
        for (int i = 0; i < 12; ++i)
        {
            const double ep = std::exp (std::min (vk / siNVt, 40.0)), em = std::exp (std::min (-vk / siNVt, 40.0));
            const double f = aK * vk + siIs * (ep - em) - bK;
            const double d = aK + siIs / siNVt * (ep + em);
            const double cand = vk - f / d;
            if (mzProfiling) ++mzProf.clipIter;
            const double next = cand >= 0.0 ? limitJunction (cand, juce::jmax (vk, 0.0), siNVt, vcritD)
                                            : -limitJunction (-cand, juce::jmax (-vk, 0.0), siNVt, vcritD);
            vk = next;
            if (std::abs (f / d) < 1.0e-10)
                break;
        }
        const double vn27 = (gE27 * vo3a - hE27 + g28 * vk) / (gE27 + g28);
        const double vl = aL * vk + bL;
        const double vn31 = a31 * vl + b31;
        const double vm2 = a2 * vl + b2;
        dbgN27 = vn27; dbgL = vl; dbgM2 = vm2;

        // ---- IC4b ----
        const double kFb4b = 1.0 / 3.3e3 + c022.g, g22 = 1.0 / 22.0e3;
        BootstrapLeg* const legs4b[] = { &legA4b, &legB4b };
        // the 22K into IC4a's (-) is an in-block load on `o` -- vX is last sample's m4a (quasi-static)
        const double vo4b = stageNewton (vm2, kFb4b, kFb4b, c022.ieq, c022.ieq, g22, g22 * vm4aPrev, amp4b, legs4b, 2);
        const double vm4b = amp4b.vm;
        ic4b = vo4b;

        // ---- IC4a inverter: m sees g22->vo4b and (g22||C018)->out; same stage() solve, the
        // input conductance folded into the history term. An iterate-the-output loop period-2
        // oscillates between the rails here, so it cannot be used. ----
        const double vo4a = stageNewton (vBias, g22 + c018.g, g22 + g22 + c018.g, g22 * vo4b + c018.ieq,
                                         c018.ieq, 0.0, 0.0, amp4a, nullptr, 0);
        const double vm4a = amp4a.vm;
        vm4aPrev = vm4a;
        ic4a = vo4a;

        // ---- commit every capacitor's terminal voltage ----
        const double iE1 = gE1 * (vin - vg) - hE1;
        c042.commit (vin - iE1 * 10.0e3 - vg);
        c039.commit (vs - vnA);
        c033.commit (vnB - vnC);
        c032.commit (vm3b - vo3b);
        leg3b.commit (vm3b);
        c031.commit (vp - vBias);
        c029.commit (vp - vq);
        c030.commit (vnR3a);
        c028.commit (vm3a - vo3a);
        const double iE27 = gE27 * (vo3a - vn27) - hE27;
        dbgPrev2 = vo3a - iE27 * m5218.outputOhms;   // boundary-node equivalent: drive behind rOut
        c027.commit (vo3a - iE27 * m5218.outputOhms - vn27);
        c023.commit (vn31);
        c021.commit (vl - vm2);
        c022.commit (vm4b - vo4b);
        legA4b.commit (vm4b);
        legB4b.commit (vm4b);
        c018.commit (vm4a - vo4a);

        return vo4a;
    }
};

auto MetalZoneStyleDistortionProcessor::Channel::operator= (const Channel& o) -> Channel&
{
    if (this == &o)
        return *this;
    blk = o.blk;
    srcIn = o.srcIn;
    srcPrev = o.srcPrev;
    out = o.out;
    nOut = o.nOut;
    dbgPrev = o.dbgPrev;
    dbgP = o.dbgP;
    dbgQ = o.dbgQ;
    dbgIc4b = o.dbgIc4b;
    rDist = o.rDist;
    rHighA = o.rHighA;
    rHighB = o.rHighB;
    rLowA = o.rLowA;
    rLowB = o.rLowB;
    rMidA = o.rMidA;
    rMidB = o.rMidB;
    rFreqA = o.rFreqA;
    rFreqB = o.rFreqB;
    rLevTop = o.rLevTop;
    rLevBottom = o.rLevBottom;
    if (o.reduced != nullptr)
    {
        if (reduced == nullptr)
            reduced = std::make_unique<ReducedFrontEnd> (*o.reduced); // prepare() only -- never after
        else
            *reduced = *o.reduced;                                  // DualMono's per-block state sync
    }
    else
        reduced.reset();
    return *this;
}

MetalZoneStyleDistortionProcessor::~MetalZoneStyleDistortionProcessor() = default;

MetalZoneStyleDistortionProcessor::MetalZoneStyleDistortionProcessor()
{
    auto make = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (0.0f, 1.0f), def);
    };

    auto dist = make ("mt2_dist", "Dist", 0.5f);
    auto high = make ("mt2_high", "High", 0.5f);
    auto middle = make ("mt2_middle", "Middle", 0.5f);
    auto midFreq = make ("mt2_midfreq", "Mid Freq", 0.5f);
    auto low = make ("mt2_low", "Low", 0.5f);
    auto level = make ("mt2_level", "Level", 0.5f);
    distParam = dist.get();
    highParam = high.get();
    middleParam = middle.get();
    midFreqParam = midFreq.get();
    lowParam = low.get();
    levelParam = level.get();

    auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
        "metalzone", "Metal Zone-Style Distortion", "|", std::move (dist));
    group->addChild (std::move (high));
    group->addChild (std::move (middle));
    group->addChild (std::move (midFreq));
    group->addChild (std::move (low));
    group->addChild (std::move (level));
    parameters = std::move (group);
}

void MetalZoneStyleDistortionProcessor::buildChannel (Channel& ch)
{
    const auto gnd = NodalCircuit::ground;
    const double rOut = m5218.outputOhms;

    // A block's own rail, reference and (for blocks 1..5) the previous block's op-amp output as a source.
    auto rails = [&] (NodalCircuit& c, bool withPrev, int blockIndex, NodalCircuit::Node& rail, NodalCircuit::Node& vb, NodalCircuit::Node& prev)
    {
        rail = c.addNode();
        vb = c.addNode();
        c.addSource (rail, supplyVolts);
        c.addSource (vb, vBias);
        if (withPrev)
        {
            const auto src = c.addNode();
            ch.srcPrev[(size_t) blockIndex] = c.addSource (src, vBias);
            prev = c.addNode();
            c.addResistor (src, prev, rOut);
        }
    };

    if (! reducedOrder)
    {
    // ================================================================ a: input JFET, bypass switch, IC3b
    {
        auto& c = ch.blk[0];
        NodalCircuit::Node rail, vb, unused;
        rails (c, false, 0, rail, vb, unused);
        const auto in = c.addNode();
        ch.srcIn = c.addSource (in, 0.0);

        // R059 10K, C042 0.047 uF into Q011's gate (R058 1M to the reference); Q011 a JFET follower, R060 10K to ground
        const auto nR = c.addNode(), g = c.addNode(), s = c.addNode();
        c.addResistor (in, nR, 10.0e3);
        c.addCapacitor (nR, g, 0.047e-6);
        c.addResistor (g, vb, 1.0e6);
        c.addJfet (rail, g, s, jfet2SK184, 5.0);
        c.addResistor (s, gnd, 10.0e3);

        // C039 1 uF, (R052 100K to the reference), Q009 the series switch (on), (R047 1M), C033 0.015 uF, R043 100K into IC3b's (+)
        const auto nA = c.addNode(), nB = c.addNode(), nC = c.addNode();
        c.addCapacitor (s, nA, 1.0e-6);
        c.addResistor (nA, vb, 100.0e3);
        c.addResistor (nA, nB, jfetSwitchOhms);
        c.addResistor (nB, vb, 1.0e6);
        c.addCapacitor (nB, nC, 0.015e-6);
        c.addResistor (nC, vb, 100.0e3);

        // IC3b: (+) = nC; (-) = m; feedback R044 220K || C032 100 pF; the (-) leg is C034 0.027 uF into Q010's bootstrapped branch
        const auto m = c.addNode(), o = c.addNode();
        c.addOpAmpMacro (nC, m, o, m5218);
        c.addResistor (m, o, 220.0e3);
        c.addCapacitor (m, o, 100.0e-12);
        addBootstrapBranch (c, m, rail, vb, 0.027e-6, 2.2e3, 0.01e-6, 47.0e3);

        for (auto n : { g, nA, nB, nC, m, o })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (s, 3.4);
        ch.out[0] = o;
    }

    // ================================================================ b: IC3a (Dist)
    {
        auto& c = ch.blk[1];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 1, rail, vb, prev);

        // R045 10K; R042 10K and C031 0.047 uF to the reference; C029 0.033 uF; R040 100K into IC3a's (+)
        const auto p = c.addNode(), q = c.addNode();
        c.addResistor (prev, p, 10.0e3);
        c.addResistor (p, vb, 10.0e3);
        c.addCapacitor (p, vb, 0.047e-6);
        c.addCapacitor (p, q, 0.033e-6);
        c.addResistor (q, vb, 100.0e3);

        // IC3a: (-) = m with R041 1K + C030 10 uF to ground; feedback C028 47 pF || (the Dist rheostat + R051 1K)
        const auto m = c.addNode(), o = c.addNode(), mid = c.addNode(), nR = c.addNode();
        c.addOpAmpMacro (q, m, o, m5218);
        c.addResistor (m, nR, 1.0e3);
        c.addCapacitor (nR, gnd, 10.0e-6);
        c.addCapacitor (m, o, 47.0e-12);
        ch.rDist = c.addResistor (m, mid, 1.0e3);
        c.addResistor (mid, o, 1.0e3);

        ch.dbgPrev = prev;
        ch.dbgP = p;
        ch.dbgQ = q;
        for (auto n : { p, q, m, o, mid, prev })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nR, 0.0);
        ch.out[1] = o;
    }

    // ================================================================ c: the clipper and IC4b
    {
        auto& c = ch.blk[2];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 2, rail, vb, prev);

        // C027 10 uF, R033 2K2, two 1SS133 back to back to ground, R032 10K, R031 4K7 + C023 0.015 uF to ground, C021 1 uF, R029 100K
        const auto n27 = c.addNode(), k = c.addNode(), l = c.addNode(), n31 = c.addNode(), m2 = c.addNode();
        c.addCapacitor (prev, n27, 10.0e-6);
        c.addResistor (n27, k, 2.2e3);
        c.addDiode (k, gnd, siIs, siNVt, 4.0e-9);
        c.addDiode (gnd, k, siIs, siNVt, 4.0e-9);
        c.addResistor (k, l, 10.0e3);
        c.addResistor (l, n31, 4.7e3);
        c.addCapacitor (n31, gnd, 0.015e-6);
        c.addCapacitor (l, m2, 1.0e-6);
        c.addResistor (m2, vb, 100.0e3);

        // IC4b: (-) = m; feedback R030 3K3 || C022 47 pF; two bootstrapped branches: (C024 0.015 uF, R034 1K, C025 0.0015 uF, R036 47K)
        // and (C020 0.22 uF, R027 470, C017 0.047 uF, R025 470K)
        const auto m = c.addNode(), o = c.addNode();
        c.addOpAmpMacro (m2, m, o, m5218);
        c.addResistor (m, o, 3.3e3);
        c.addCapacitor (m, o, 47.0e-12);
        addBootstrapBranch (c, m, rail, vb, 0.015e-6, 1.0e3, 0.0015e-6, 47.0e3);
        addBootstrapBranch (c, m, rail, vb, 0.22e-6, 470.0, 0.047e-6, 470.0e3);

        // IC4a: a unity-gain inverter, R028 22K in, R026 22K || C018 10 pF feedback, (+) at the reference. Its input is IC4b's output, which the
        // rails already limit, so it cannot clip in turn and shares this block as a linear op-amp.
        const auto m4a = c.addNode(), o4a = c.addNode();
        c.addResistor (o, m4a, 22.0e3);
        addLinearOpAmp (c, vb, m4a, o4a, m5218);
        c.addResistor (m4a, o4a, 22.0e3);
        c.addCapacitor (m4a, o4a, 10.0e-12);

        for (auto n : { m2, m, o, prev, m4a, o4a })
            c.setInitialGuess (n, vBias);
        for (auto n : { n27, k, l, n31 })
            c.setInitialGuess (n, 0.0);
        ch.dbgIc4b = o;
        ch.dbgPrev2 = prev; ch.dbgK = k; ch.dbgL = l; ch.dbgM2 = m2;
        ch.out[2] = o4a;
    }
    }

    // ================================================================ e: IC1a with High and Low
    {
        auto& c = ch.blk[3];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 3, rail, vb, prev);

        // R014 22K into node 7 (IC1a's (+)); feedback R015 22K || C010 10 pF from the output to node 8 (IC1a's (-))
        const auto n7 = c.addNode(), n8 = c.addNode(), o = c.addNode();
        c.addResistor (prev, n7, 22.0e3);
        c.addOpAmpMacro (n7, n8, o, m5218);
        c.addResistor (n8, o, 22.0e3);
        c.addCapacitor (n8, o, 10.0e-12);

        // High (VR03b 100K): node 7 -> wiper -> node 8, the wiper to ground through R061 2K2 and C044 0.01 uF
        const auto wH = c.addNode(), tH = c.addNode();
        ch.rHighA = c.addResistor (n7, wH, 1.0e3);
        ch.rHighB = c.addResistor (wH, n8, 1.0e3);
        c.addResistor (wH, tH, 2.2e3);
        c.addCapacitor (tH, gnd, 0.01e-6);

        // Low (VR03a 100K): node 7 -> wiper -> node 8; the wiper into C008 0.22 uF, then R012 2K2 / C009 0.047 uF around IC1b (a follower):
        // node X -> R012 -> IC1b's output; X -> C009 -> IC1b's (+) (R013 100K to the reference)
        const auto wL = c.addNode(), x = c.addNode(), p5 = c.addNode(), vf = c.addNode();
        ch.rLowA = c.addResistor (n7, wL, 1.0e3);
        ch.rLowB = c.addResistor (wL, n8, 1.0e3);
        c.addCapacitor (wL, x, 0.22e-6);
        c.addResistor (x, vf, 2.2e3);
        c.addCapacitor (x, p5, 0.047e-6);
        c.addResistor (p5, vb, 100.0e3);
        c.addFollower (p5, vf, 0.0);

        for (auto n : { n7, n8, o, wH, wL, x, p5, vf, prev })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (tH, 0.0);
        ch.out[3] = o;
    }

    // ================================================================ f: IC2a, Middle / Mid Freq, Level, output stage
    {
        auto& c = ch.blk[4];
        NodalCircuit::Node rail, vb, prev;
        rails (c, true, 4, rail, vb, prev);

        // C011 1 uF into node Y: R050 330 to node 12, R038 47K into IC2a's (-); IC2a's (+) = Z (R039 1M to the reference)
        const auto y = c.addNode(), n12 = c.addNode(), m = c.addNode(), z = c.addNode(), o = c.addNode();
        c.addCapacitor (prev, y, 1.0e-6);
        c.addResistor (y, n12, 330.0);
        c.addResistor (y, m, 47.0e3);
        c.addResistor (z, vb, 1.0e6);
        c.addOpAmpMacro (z, m, o, m5218);
        c.addResistor (m, o, 47.0e3);
        c.addCapacitor (m, o, 100.0e-12);

        // IC2a's output: C037 1 uF and R049 330 to node 11. Middle (VR02b 100K): node 12 -> wiper -> node 11; the wiper into IC2b (a follower)
        const auto n37 = c.addNode(), n11 = c.addNode(), wM = c.addNode(), fb = c.addNode();
        c.addCapacitor (o, n37, 1.0e-6);
        c.addResistor (n37, n11, 330.0);
        ch.rMidA = c.addResistor (n12, wM, 1.0e3);
        ch.rMidB = c.addResistor (wM, n11, 1.0e3);
        c.addFollower (wM, fb, 0.0);

        // IC2b's output through C036 0.022 uF and R048 2K2 to node 9. Mid Freq (VR02a, two sections, both a rheostat): node 10 -> node 9 and
        // node 10 -> R062 2K2 -> ground; C043 0.0082 uF from node 10 to ground; C038 0.1 uF from node 10 to Z
        const auto n36 = c.addNode(), n9 = c.addNode(), n10 = c.addNode(), n62 = c.addNode();
        c.addCapacitor (fb, n36, 0.022e-6);
        c.addResistor (n36, n9, 2.2e3);
        ch.rFreqA = c.addResistor (n10, n9, 1.0e3);
        ch.rFreqB = c.addResistor (n10, n62, 1.0e3);
        c.addResistor (n62, gnd, 2.2e3);
        c.addCapacitor (n10, gnd, 0.0082e-6);
        c.addCapacitor (n10, z, 0.1e-6);

        // C005 10 uF into the Level pot (VR04 50KA), C004 1 uF, R006 1M to the reference, Q002 (on), C002 10 uF, R004 100K, Q001 an emitter
        // follower (R002 10K), C001 10 uF, R001 1K to the jack (R003 100K across it)
        const auto n4 = c.addNode(), n3 = c.addNode(), nQ = c.addNode(), nJ = c.addNode(), nBase = c.addNode(), nE = c.addNode(), nOutJack = c.addNode(), nJack = c.addNode();
        c.addCapacitor (o, n4, 10.0e-6);
        ch.rLevTop = c.addResistor (n4, n3, 1.0e3);
        ch.rLevBottom = c.addResistor (n3, gnd, 1.0e3);
        c.addCapacitor (n3, nQ, 1.0e-6);
        c.addResistor (nQ, vb, 1.0e6);
        c.addResistor (nQ, nJ, jfetSwitchOhms);
        c.addCapacitor (nJ, nBase, 10.0e-6);
        c.addResistor (nBase, vb, 100.0e3);
        c.addFollower (nBase, nE, 0.65);
        c.addResistor (nE, gnd, 10.0e3);
        c.addCapacitor (nE, nOutJack, 10.0e-6);
        c.addResistor (nOutJack, gnd, 100.0e3);
        c.addResistor (nOutJack, nJack, 1.0e3);
        c.addResistor (nJack, gnd, downstreamLoad);

        for (auto n : { y, n12, m, z, o, n37, n11, wM, fb, nQ, nJ, nBase })
            c.setInitialGuess (n, vBias);
        c.setInitialGuess (nE, vBias - 0.65);
        c.setInitialGuess (prev, vBias);
        ch.out[4] = o;
        ch.nOut = nJack;
    }
}

void MetalZoneStyleDistortionProcessor::updatePots (const Knobs& k)
{
    // Dist: VR01 250kA as a rheostat in IC3a's feedback. Clockwise = more resistance = more gain.
    const double rDist = juce::jmax (1.0, distPotMax * pots::audio (k.dist));

    // High / Low: 100K linear between node 7 (input) and node 8 (IC1a's (-)); the wiper goes to a shunt to ground. The wiper toward node 8
    // (a large "A" segment) = boost, measured on the network. Middle: node 12 -> wiper -> node 11, the wiper toward 11 (IC2a's output) = boost.
    auto split = [] (double fraction, double& a, double& b)
    {
        a = juce::jmax (1.0, toneA * fraction);
        b = juce::jmax (1.0, toneA - a);
    };
    double highA, highB, lowA, lowB, midA, midB;
    split (k.high, highA, highB);
    split (k.low, lowA, lowB);
    split (k.middle, midA, midB);

    // Mid Freq: VR02a "C" taper, both sections rheostats that move together. Measured on the network: MORE resistance moves the
    // mid boost/cut LOWER (about 200-400 Hz at 50K, above 1.5 kHz at ~0), so clockwise = higher frequency = LESS resistance
    // (an assumption about how the real knob is wired), on the reverse-log law of a "C" pot.
    const double rFreq = juce::jmax (1.0, midFreqPotMax * pots::law (1.0 - k.midFreq, 0.85));

    // Level: VR04 50KA, wiper-to-ground segment
    const double levBottom = juce::jmax (1.0, levelPotMax * pots::audio (k.level));
    const double levTop = juce::jmax (1.0, levelPotMax - levBottom);

    for (auto& ch : channels)
    {
        if (ch.reduced != nullptr)
            ch.reduced->gDist = 1.0 / (rDist + 1.0e3);
        else
            ch.blk[1].setResistance (ch.rDist, rDist);
        ch.blk[3].setResistance (ch.rHighA, highA);
        ch.blk[3].setResistance (ch.rHighB, highB);
        ch.blk[3].setResistance (ch.rLowA, lowA);
        ch.blk[3].setResistance (ch.rLowB, lowB);
        ch.blk[4].setResistance (ch.rMidA, midA);
        ch.blk[4].setResistance (ch.rMidB, midB);
        ch.blk[4].setResistance (ch.rFreqA, rFreq);
        ch.blk[4].setResistance (ch.rFreqB, rFreq);
        ch.blk[4].setResistance (ch.rLevTop, levTop);
        ch.blk[4].setResistance (ch.rLevBottom, levBottom);
    }
}

double MetalZoneStyleDistortionProcessor::debugVoltage (Probe p) const noexcept
{
    const auto& ch = channels[0];
    if (ch.reduced != nullptr)
    {
        switch (p)
        {
            case Probe::ic3b:   return ch.reduced->ic3b;
            case Probe::ic3a:   return ch.reduced->ic3a;
            case Probe::ic4b:   return ch.reduced->ic4b;
            case Probe::ic4a:   return ch.reduced->ic4a;
            case Probe::ic1a:   return ch.blk[3].voltage (ch.out[3]);
            case Probe::ic2a:   return ch.blk[4].voltage (ch.out[4]);
            case Probe::output: return ch.blk[4].voltage (ch.nOut);
            case Probe::ic3aPrev: return ch.reduced->ic3aPrev;
            case Probe::ic3aP: return ch.reduced->ic3aP;
            case Probe::ic3aQ: return ch.reduced->ic3aQ;
            case Probe::prev2:  return ch.reduced->dbgPrev2;
            case Probe::clipK:  return ch.reduced->vk;
            case Probe::clipL:  return ch.reduced->dbgL;
            case Probe::clipM2: return ch.reduced->dbgM2;
        }
    }
    switch (p)
    {
        case Probe::ic3b:   return ch.blk[0].voltage (ch.out[0]);
        case Probe::ic3a:   return ch.blk[1].voltage (ch.out[1]);
        case Probe::ic4b:   return ch.blk[2].voltage (ch.dbgIc4b);
        case Probe::ic4a:   return ch.blk[2].voltage (ch.out[2]);
        case Probe::ic1a:   return ch.blk[3].voltage (ch.out[3]);
        case Probe::ic2a:   return ch.blk[4].voltage (ch.out[4]);
        case Probe::output: return ch.blk[4].voltage (ch.nOut);
        case Probe::ic3aPrev: return ch.blk[1].voltage (ch.dbgPrev);
        case Probe::ic3aP: return ch.blk[1].voltage (ch.dbgP);
        case Probe::ic3aQ: return ch.blk[1].voltage (ch.dbgQ);
        case Probe::prev2:  return ch.blk[2].voltage (ch.dbgPrev2);
        case Probe::clipK:  return ch.blk[2].voltage (ch.dbgK);
        case Probe::clipL:  return ch.blk[2].voltage (ch.dbgL);
        case Probe::clipM2: return ch.blk[2].voltage (ch.dbgM2);
    }
    return 0.0;
}

double MetalZoneStyleDistortionProcessor::debugIterations() const noexcept
{
    double total = 0.0;
    const auto& ch = channels[0];
    if (ch.reduced != nullptr)
        return ch.blk[3].averageIterations() + ch.blk[4].averageIterations();
    for (const auto& b : ch.blk)
        total += b.averageIterations();
    return total;
}

void MetalZoneStyleDistortionProcessor::prepare (double newSampleRate, int, int)
{
    if (juce::exactlyEqual (sampleRate, newSampleRate) && sampleRate > 0.0)
        return;
    sampleRate = newSampleRate;

    for (auto& ch : channels)
    {
        ch = Channel {};
        buildChannel (ch);
        if (reducedOrder)
        {
            ch.reduced = std::make_unique<ReducedFrontEnd>();
            ch.reduced->prepare (newSampleRate);
        }
    }

    auto setup = [&] (juce::SmoothedValue<float>& s, juce::AudioParameterFloat* p)
    {
        s.reset (newSampleRate, 0.02);
        s.setCurrentAndTargetValue (p->get());
    };
    setup (smoothedDist, distParam);
    setup (smoothedHigh, highParam);
    setup (smoothedMiddle, middleParam);
    setup (smoothedMidFreq, midFreqParam);
    setup (smoothedLow, lowParam);
    setup (smoothedLevel, levelParam);

    updatePots ({ distParam->get(), highParam->get(), middleParam->get(), midFreqParam->get(), lowParam->get(), levelParam->get() });

    dcOk = true;
    for (auto& ch : channels)
    {
        if (ch.reduced != nullptr)
        {
            ch.blk[3].setSource (ch.srcPrev[3], ch.reduced->ic4a);
            dcOk = ch.blk[3].prepare (newSampleRate) && dcOk;
            ch.blk[4].setSource (ch.srcPrev[4], ch.blk[3].voltage (ch.out[3]));
            dcOk = ch.blk[4].prepare (newSampleRate) && dcOk;
        }
        else
            for (int b = 0; b < numBlocks; ++b)
            {
                if (b > 0)
                    ch.blk[(size_t) b].setSource (ch.srcPrev[(size_t) b], ch.blk[(size_t) b - 1].voltage (ch.out[(size_t) b - 1]));
                dcOk = ch.blk[(size_t) b].prepare (newSampleRate) && dcOk;
            }
    }

    controlCounter = 0;
    sampleCount = 0;
    failureCount = 0;
    shortcut.reset();
}

void MetalZoneStyleDistortionProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int solveChannels = shortcut.begin (channels, buffer, sampleRate);

    smoothedDist.setTargetValue (distParam->get());
    smoothedHigh.setTargetValue (highParam->get());
    smoothedMiddle.setTargetValue (middleParam->get());
    smoothedMidFreq.setTargetValue (midFreqParam->get());
    smoothedLow.setTargetValue (lowParam->get());
    smoothedLevel.setTargetValue (levelParam->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const Knobs k { smoothedDist.getNextValue(), smoothedHigh.getNextValue(), smoothedMiddle.getNextValue(),
                        smoothedMidFreq.getNextValue(), smoothedLow.getNextValue(), smoothedLevel.getNextValue() };

        if (++controlCounter >= controlInterval)
        {
            controlCounter = 0;
            updatePots (k);
        }

        for (int chIdx = 0; chIdx < solveChannels; ++chIdx)
        {
            auto& ch = channels[(size_t) chIdx];
            auto* data = buffer.getWritePointer (chIdx);

            bool ok = true;
            if (ch.reduced != nullptr)
            {
                ch.blk[3].setSource (ch.srcPrev[3], ch.reduced->step ((double) data[i]));
                ok = ch.blk[3].solveSample();
                ch.blk[4].setSource (ch.srcPrev[4], ch.blk[3].voltage (ch.out[3]));
                ok = ch.blk[4].solveSample() && ok;
            }
            else
            {
                ch.blk[0].setSource (ch.srcIn, (double) data[i]);
                ok = ch.blk[0].solveSample();
                for (int b = 1; b < numBlocks; ++b)
                {
                    ch.blk[(size_t) b].setSource (ch.srcPrev[(size_t) b], ch.blk[(size_t) b - 1].voltage (ch.out[(size_t) b - 1]));
                    ok = ch.blk[(size_t) b].solveSample() && ok;
                }
            }

            data[i] = (float) ch.blk[4].voltage (ch.nOut);

            if (chIdx == 0)
            {
                ++sampleCount;
                if (! ok)
                    ++failureCount;
            }
        }
    }

    shortcut.end (buffer);
}

void MetalZoneStyleDistortionProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::distortion_svg, IconData::distortion_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
