#pragma once
#include "EbersMollBJT.h"
#include "ShichmanHodgesJFET.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>
#ifdef NODAL_DEBUG
#include <cstdio>

#endif

namespace openguitarmultifx
{

/**
    A small general-purpose circuit solver (modified nodal analysis with
    trapezoidal capacitors and Newton-Raphson for the nonlinear devices) --
    the same technique SPICE uses, sized for one stompbox stage at audio
    rate. A pedal is described once as a netlist (addResistor(), addBjt(),
    ...) instead of hand-deriving a Thevenin-reduction chain per stage,
    which is what the earlier per-pedal processors did and what stops
    scaling once stages interact (feed-forward networks, multiple-feedback
    filters, transistor stages with shared feedback).

    ## What it models
    - Resistors, capacitors (trapezoidal companion model, identical to
      TrapezoidalCapacitor's), fixed/driven nodes (supply rails, the input
      signal, a bias rail).
    - **Ideal op-amps** (V+ = V-, output current unconstrained), solved
      exactly by replacing the output node's KCL row with the virtual-short
      constraint -- no extra unknown. Same "ideal op-amp" fidelity level as
      every other circuit in this project (no gain-bandwidth, slew or rail
      clipping).
    - Diodes (Shockley), BJTs (Ebers-Moll, NPN/PNP) and N-channel JFETs
      (Shichman-Hodges). The BJT and JFET equations are EbersMollBJT's and
      ShichmanHodgesJFET's own `evaluate()` -- one home for each device
      model.

    ## How a sample is solved (the "DK method")
    The linear part of the netlist (resistors, capacitor conductances,
    op-amp constraints) is LU-factored once -- and again only when a
    resistance changes (`setResistance()`; update pots at control rate, a
    few samples apart, smoothed). Each nonlinear device exposes a few
    "ports" (a diode: 1 branch voltage; a BJT or JFET: 2). Per sample the
    linear network is back-substituted once for the no-device response, and
    Newton-Raphson then runs only over the port voltages (a handful of
    unknowns), never over the whole matrix -- the solution is identical to a
    full Newton solve, at a fraction of the cost (a 21-unknown, 2-BJT,
    2-diode block runs in ~1 us/sample instead of ~5.6). DC operating
    point uses the plain full-matrix Newton, once, in prepare().

    ## Partitioning
    Blocks up to `maxUnknowns` are supported, but it is still worth
    splitting a pedal where an ideal op-amp output (an ideal voltage source
    that nothing downstream can load back) feeds the next stage, and driving
    the next block's input from this block's output node.

    ## DC operating point
    prepare() solves the true DC operating point directly (capacitors open,
    source-stepping Newton) and initialises every capacitor to it, so a
    circuit starts settled instead of needing seconds of silent audio.
*/
class NodalCircuit
{
public:
    static constexpr int maxUnknowns = 24;

    using Node = int;
    static constexpr Node ground = 0;

    /** Capacitor integration: the theta-method. 0.5 is the trapezoidal rule (exact frequency response shape, but it
        does not damp stiff modes -- every diode switch or transistor saturation rings at Nyquist); 1 is backward
        Euler (heavily damped). 0.6 cuts the ringing energy above 12 kHz by ~9 dB (HM-2 clipping op-amp) for a 0.03 dB
        change in the audible-band response; going to 1.0 gains only 4 dB more and costs 1.3 dB at 8 kHz. */
    static inline double defaultTheta = 0.6;
    /** Newton stops when the leftover error, mapped to any node voltage, is below this (V); see solveDevices(). */
    static inline double defaultNodeTolerance = 1.0e-5;
    void setIntegrationTheta (double newTheta) noexcept { theta = newTheta; }

    struct BjtParams { double Is, Vt, betaF, betaR; };
    struct JfetParams { double idss, pinchOff, lambda; };

    // ---- Construction (allocates; never call from the audio thread) ----

    Node addNode() { return ++nodeCount; }

    /** Adds `count` nodes and returns the first one's number. */
    Node addNodes (int count)
    {
        const Node first = nodeCount + 1;
        nodeCount += count;
        return first;
    }

    int addResistor (Node a, Node b, double ohms)
    {
        resistors.push_back ({ a, b, 1.0 / ohms });
        return (int) resistors.size() - 1;
    }

    int addCapacitor (Node a, Node b, double farads)
    {
        capacitors.push_back ({ a, b, farads, 0.0, 0.0, 0.0, 0.0, -1, -1 });
        return (int) capacitors.size() - 1;
    }

    /** Forces `node` to a fixed/driven voltage (a supply rail, a bias rail,
        or the input signal). Returns a handle for setSource(). */
    int addSource (Node node, double volts)
    {
        sources.push_back ({ node, volts });
        return (int) sources.size() - 1;
    }

    /** Ideal op-amp: drives `out` so that V(inPlus) == V(inMinus). */
    void addOpAmp (Node inPlus, Node inMinus, Node out) { opAmps.push_back ({ inPlus, inMinus, out, 0.0 }); }

    /** Emitter/source follower without the transistor: `out` = `in` - `drop` exactly, `in` draws no current. For a
        follower whose collector sits on a rail and whose job is to buffer (unity gain 0.95-0.99 in the real part, and
        nothing that clips) this is the same sound with no Newton port at all; the DC level shift is kept so the
        operating point stays put. */
    void addFollower (Node in, Node out, double drop) { opAmps.push_back ({ in, out, out, drop }); }

    /** A real op-amp's output limits (see addSaturatingOpAmp). */
    struct OpAmpSpec
    {
        double lowRail = 1.5;  // lowest voltage the output can reach
        double highRail = 7.5; // highest
    };

    /** Ideal op-amp that runs out of swing like the real one does: V(+) == V(-) while the output is inside
        [lowRail, highRail]; outside, the output sits AT the rail and the constraint is dropped (the loop is really
        broken -- the inverting input stops following the non-inverting one and the capacitors in the feedback
        network keep charging as they would -- which clamping an ideal output afterwards does not do).

        It is a three-state switch (ideal / held high / held low), solved exactly: each state has its own
        factorisation (built lazily), the previous sample's state is tried first, and a state is consistent when
        the ideal answer is inside the rails (state 0), or when the (+)/(-) difference has the sign that keeps
        pushing into the rail (states 1, 2). No extra Newton port, no finite-gain error. At most ONE per
        NodalCircuit (every block that needs one has exactly one). */
    void addSaturatingOpAmp (Node inPlus, Node inMinus, Node out, const OpAmpSpec& spec)
    {
        satOpIndex = (int) opAmps.size();
        satSpec = spec;
        opAmps.push_back ({ inPlus, inMinus, out, 0.0 });
    }

    void addDiode (Node anode, Node cathode, double saturationCurrent, double nTimesVt)
    {
        diodes.push_back ({ anode, cathode, saturationCurrent, nTimesVt });
    }

    void addBjt (Node collector, Node base, Node emitter, bool pnp, const BjtParams& p)
    {
        Bjt q { collector, base, emitter, pnp, {} };
        q.model.setParameters (p.Is, p.Vt, p.betaF, p.betaR);
        bjts.push_back (q);
    }

    /** N-channel JFET. Gate current is taken as zero. `assumedVds` > 0 marks a JFET that stays in saturation (a
        long-tailed pair's device with its drain on the rail, a source follower): its current then depends on vgs
        alone (channel-length modulation taken at that vds, ~1% for lambda = 0.02), which makes it a ONE-port device. */
    void addJfet (Node drain, Node gate, Node source, const JfetParams& p, double assumedVds = 0.0)
    {
        Jfet j { drain, gate, source, {}, assumedVds };
        j.model.setParameters (p.idss, p.pinchOff, p.lambda);
        jfets.push_back (j);
    }

    /** Optional starting point for the DC solve (defaults to 0 V). */
    void setInitialGuess (Node node, double volts)
    {
        if (node >= 1 && node <= nodeCount)
        {
            if ((int) guess.size() <= nodeCount)
                guess.resize ((size_t) nodeCount + 1, 0.0);
            guess[(size_t) node] = volts;
        }
    }

    /** Finalises the netlist, computes the DC operating point and
        initialises every capacitor to it. Returns false if the DC solve
        did not converge (the circuit still runs, from its best guess). */
    bool prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        known.assign ((size_t) nodeCount + 1, false);
        knownVoltage.assign ((size_t) nodeCount + 1, 0.0);
        known[0] = true;
        for (const auto& s : sources)
        {
            known[(size_t) s.node] = true;
            knownVoltage[(size_t) s.node] = s.volts;
        }

        indexOf.assign ((size_t) nodeCount + 1, -1);
        unknownCount = 0;
        for (Node n = 1; n <= nodeCount; ++n)
            if (! known[(size_t) n])
                indexOf[(size_t) n] = unknownCount++;

        if (unknownCount > maxUnknowns)
            return false; // netlist too large for one block -- split it, see the class doc
        if ((int) (diodes.size() + 2 * bjts.size() + 2 * jfets.size()) > maxPortsV)
            return false; // too many nonlinear devices for one block

        isOpAmpRow.assign ((size_t) unknownCount, false);
        for (const auto& o : opAmps)
            if (indexOf[(size_t) o.out] >= 0)
                isOpAmpRow[(size_t) indexOf[(size_t) o.out]] = true;

        for (auto& c : capacitors)
        {
            c.g = c.farads * sampleRate / theta;
            const int ra = c.a > 0 ? indexOf[(size_t) c.a] : -1;
            const int rb = c.b > 0 ? indexOf[(size_t) c.b] : -1;
            c.ia = (ra >= 0 && ! isOpAmpRow[(size_t) ra]) ? ra : -1;
            c.ib = (rb >= 0 && ! isOpAmpRow[(size_t) rb]) ? rb : -1;
        }

        opTerms.clear();
        for (size_t oi = 0; oi < opAmps.size(); ++oi)
        {
            const auto& o = opAmps[oi];
            const int r = indexOf[(size_t) o.out];
            if (r >= 0)
                opTerms.push_back ({ r, o.plus, o.minus, (int) oi == satOpIndex, o.offset });
        }
        satMode = 0;
        parked.assign (satOpIndex >= 0 ? 3 : 0, Parked {});

        x.assign ((size_t) unknownCount, 0.0);
        for (int n = 1; n <= nodeCount; ++n)
            if (indexOf[(size_t) n] >= 0 && n < (int) guess.size())
                x[(size_t) indexOf[(size_t) n]] = guess[(size_t) n];

        matrixDirty = true;

        bool ok = false;
        if (! capacitors.empty())
            ok = relaxToDc();
        if (! ok)
        {
            // Static solve (capacitors open, sources stepped up) as a second opinion / for capacitor-free netlists.
            x.assign ((size_t) unknownCount, 0.0);
            for (int n = 1; n <= nodeCount; ++n)
                if (indexOf[(size_t) n] >= 0 && n < (int) guess.size())
                    x[(size_t) indexOf[(size_t) n]] = guess[(size_t) n];
            ok = solveDc();
        }

        for (auto& c : capacitors)
        {
            c.g = c.farads * sampleRate / theta;
            c.vPrev = voltage (c.a) - voltage (c.b);
            c.iPrev = 0.0;
        }
        beDt = 0.0;
        matrixDirty = true;
        satMode = 0;
        for (auto& p : parked)
            p.valid = false;

        return ok;
    }

    // ---- Run time (allocation-free) ----

    void setSource (int handle, double volts) noexcept
    {
        sources[(size_t) handle].volts = volts;
        const auto n = (size_t) sources[(size_t) handle].node;
        if (n < knownVoltage.size()) // before prepare() only the stored value matters
            knownVoltage[n] = volts;
    }

    void setResistance (int handle, double ohms) noexcept
    {
        const double g = 1.0 / ohms;
        auto& r = resistors[(size_t) handle];
        if (! (g < r.g || g > r.g)) // unchanged (knobs at rest): keep the factorisation, the LU refactor is the costly part
            return;
        r.g = g;
        matrixDirty = true;
        for (auto& p : parked)
            p.valid = false;
    }

    /** Advances one sample. Returns false if Newton failed to converge (the
        previous sample's values are then held for this sample). */
    bool solveSample() noexcept
    {
        const double ieqHistoryWeight = (1.0 - theta) / theta;
        bool ok = false;

        // A saturating op-amp can need up to a few passes (ideal -> held at a rail -> back); everything else, one.
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (matrixDirty)
                rebuildLinearMatrix (true);
            if (! luValid)
                factorAndBuildReducedModel();
            double* rhs = rhsLinear.data();
            for (int i = 0; i < unknownCount; ++i)
                rhs[i] = 0.0;

            for (const auto& t : knownTerms)
                rhs[t.row] += t.g * knownVoltage[(size_t) t.knownNode];

            for (auto& c : capacitors)
            {
                c.ieq = c.g * c.vPrev + ieqHistoryWeight * c.iPrev;
                if (c.ia >= 0) rhs[c.ia] += c.ieq;
                if (c.ib >= 0) rhs[c.ib] -= c.ieq;
            }

            for (const auto& o : opTerms)
                rhs[o.row] = opRhs (o);
            // No-device response of the linear network.
            for (int i = 0; i < unknownCount; ++i)
                xLinear[i] = rhsLinear[(size_t) i];
            luBackSubstitute (xLinear);

            ok = solveDevices();
            if (! ok || satOpIndex < 0)
                break;

            const int wanted = wantedSatMode();
            if (wanted == satMode)
                break;
            setSatMode (wanted);
        }

        if (ok)
        {
            for (auto& c : capacitors)
            {
                const double v = voltage (c.a) - voltage (c.b);
                c.iPrev = c.g * v - c.ieq;
                c.vPrev = v;
            }
            xPrev = x;
        }
        else
        {
            x = xPrev;
        }

        return ok;
    }

    /** Which state the saturating op-amp should be in given the solution just computed in `satMode`: ideal is right
        while its output is inside the rails; held at a rail is right while (+) - (-) keeps pushing into it. */
    int wantedSatMode() const noexcept
    {
        const auto& o = opAmps[(size_t) satOpIndex];
        const double vout = voltage (o.out);
        const double vd = voltage (o.plus) - voltage (o.minus);
        if (satMode == 0)
            return vout > satSpec.highRail ? 1 : (vout < satSpec.lowRail ? 2 : 0);
        if (satMode == 1)
            return vd < 0.0 ? 0 : 1;
        return vd > 0.0 ? 0 : 2;
    }

    void setSatMode (int mode) noexcept
    {
        if (mode == satMode)
            return;

        Parked& leaving = parked[(size_t) satMode];
        std::memcpy (leaving.lu, lu, sizeof lu);
        std::memcpy (leaving.pivot, pivot, sizeof pivot);
        std::memcpy (leaving.lStart, lStart, sizeof lStart);
        std::memcpy (leaving.lRow, lRow, sizeof lRow);
        std::memcpy (leaving.uStart, uStart, sizeof uStart);
        std::memcpy (leaving.uCol, uCol, sizeof uCol);
        std::memcpy (leaving.lVal, lVal, sizeof lVal);
        std::memcpy (leaving.uVal, uVal, sizeof uVal);
        std::memcpy (leaving.invDiag, invDiag, sizeof invDiag);
        std::memcpy (leaving.W, W, sizeof W);
        std::memcpy (leaving.K, K, sizeof K);
        leaving.lCount = lCount;
        leaving.uCount = uCount;
        leaving.valid = luValid && ! matrixDirty;

        satMode = mode;
        const Parked& entering = parked[(size_t) mode];
        if (entering.valid && ! matrixDirty)
        {
            std::memcpy (lu, entering.lu, sizeof lu);
            std::memcpy (pivot, entering.pivot, sizeof pivot);
            std::memcpy (lStart, entering.lStart, sizeof lStart);
            std::memcpy (lRow, entering.lRow, sizeof lRow);
            std::memcpy (uStart, entering.uStart, sizeof uStart);
            std::memcpy (uCol, entering.uCol, sizeof uCol);
            std::memcpy (lVal, entering.lVal, sizeof lVal);
            std::memcpy (uVal, entering.uVal, sizeof uVal);
            std::memcpy (invDiag, entering.invDiag, sizeof invDiag);
            std::memcpy (W, entering.W, sizeof W);
            std::memcpy (K, entering.K, sizeof K);
            lCount = entering.lCount;
            uCount = entering.uCount;
            luValid = true;
        }
        else
        {
            matrixDirty = true; // rebuild the matrix (its op-amp row differs) and factor it on demand
            luValid = false;
        }
    }

    double voltage (Node node) const noexcept
    {
        if (node <= 0)
            return 0.0;
        const int i = indexOf[(size_t) node];
        return i >= 0 ? x[(size_t) i] : knownVoltage[(size_t) node];
    }

    /** The current a fixed-voltage node (rail/input) delivers into the
        circuit -- useful to read supply current. Not needed by pedals. */
    int unknowns() const noexcept { return unknownCount; }
    int nodes() const noexcept { return nodeCount; }

    /** Newton iterations per solved sample so far (diagnostic). */
    double averageIterations() const noexcept { return samplesSolved > 0 ? (double) iterationsTotal / (double) samplesSolved : 0.0; }

private:
    struct Res { Node a, b; double g; };
    struct Cap { Node a, b; double farads, g, vPrev, iPrev, ieq; int ia = -1, ib = -1; };
    struct Src { Node node; double volts; };
    struct Op { Node plus, minus, out; double offset; }; // V(plus) - V(minus) = offset
    struct Dio { Node a, k; double Is, nVt; };
    struct Bjt { Node c, b, e; bool pnp; EbersMollBJT model; };
    struct Jfet { Node d, g, s; ShichmanHodgesJFET model; double assumedVds = 0.0; };

    // -- element storage --
    std::vector<Res> resistors;
    std::vector<Cap> capacitors;
    std::vector<Src> sources;
    std::vector<Op> opAmps;
    std::vector<Dio> diodes;
    std::vector<Bjt> bjts;
    std::vector<Jfet> jfets;
    std::vector<std::pair<int, int>> diodePairs; // built by buildPorts(): indices of the two antiparallel diodes
    std::vector<double> guess;

    int nodeCount = 0;
    double sampleRate = 48000.0;

    // -- resolved topology --
    std::vector<bool> known;
    std::vector<double> knownVoltage;
    std::vector<int> indexOf;
    std::vector<bool> isOpAmpRow;
    int unknownCount = 0;

    // -- solver workspace (sized once in prepare(); fixed-size arrays so the
    //    hot path never allocates) --
    double linearMatrix[maxUnknowns][maxUnknowns] {};
    double workMatrix[maxUnknowns][maxUnknowns] {};
    std::vector<double> rhsLinear = std::vector<double> (maxUnknowns, 0.0);
    std::vector<double> rhsWork = std::vector<double> (maxUnknowns, 0.0);
    std::vector<double> x, xPrev;
    bool matrixDirty = true;
    bool dcMode = false;
    long long iterationsTotal = 0, samplesSolved = 0;
    double theta = defaultTheta;
    double beDt = 0.0; // > 0 only during the pseudo-transient DC relaxation (backward Euler with this step)

    struct KnownTerm { int row; Node knownNode; double g; };
    std::vector<KnownTerm> knownTerms; // g * V(known) added to rhs rows, for elements touching a rail/input
    struct OpTerm { int row; Node plus, minus; bool saturating; double offset; };
    std::vector<OpTerm> opTerms;

    // -- reduced (DK-method) transient solver state --
    double lu[maxUnknowns][maxUnknowns] {};
    int pivot[maxUnknowns] {};
    static constexpr int maxLuEntries = maxUnknowns * maxUnknowns;
    int lStart[maxUnknowns + 1] {}, lRow[maxLuEntries] {}, lCount = 0;   // L, column-wise nonzeros
    int uStart[maxUnknowns + 1] {}, uCol[maxLuEntries] {}, uCount = 0;   // U, row-wise nonzeros
    double lVal[maxLuEntries] {}, uVal[maxLuEntries] {}, invDiag[maxUnknowns] {};
    bool luValid = false;

    // -- saturating op-amp (at most one per circuit): 0 ideal, 1 held at the high rail, 2 held at the low rail.
    // The active mode's factorisation lives in the members above; the others are parked here (valid only until a
    // resistance changes).
    struct Parked
    {
        double lu[maxUnknowns][maxUnknowns];
        int pivot[maxUnknowns];
        int lStart[maxUnknowns + 1], lRow[maxLuEntries], lCount;
        int uStart[maxUnknowns + 1], uCol[maxLuEntries], uCount;
        double lVal[maxLuEntries], uVal[maxLuEntries], invDiag[maxUnknowns];
        double W[16][maxUnknowns];
        double K[16][16];
        bool valid = false;
    };
    int satOpIndex = -1;
    OpAmpSpec satSpec;
    int satMode = 0;
    std::vector<Parked> parked; // 3 entries when a saturating op-amp exists

    static constexpr int maxPortsV = 16;  // independent branch voltages across all devices
    static constexpr int maxPortsI = 16;  // independent device currents across all devices
    struct CurrentPort { Node plus, minus; double sign; };
    struct VoltagePort { Node plus, minus; };
    struct DeviceMap { int kind; int v0, i0; int index; int nv, ni; }; // kind: 0 diode, 1 bjt, 2 jfet
    std::vector<CurrentPort> currentPorts;
    std::vector<VoltagePort> voltagePorts;
    std::vector<DeviceMap> deviceMaps;
    double W[maxPortsI][maxUnknowns] {};       // A^-1 * e_k
    double K[maxPortsV][maxPortsI] {};         // P_j . W_k
    double uState[maxPortsV] {};               // last converged port voltages (Newton warm start)
    double uStatePrev[maxPortsV] {};           // the one before (for the linear-extrapolation predictor)
    double nodeTolerance = defaultNodeTolerance;             // Newton stops when the leftover node error is below this (V)
    static inline double predictWeight = 1.0;
    static inline bool predictorEnabled = true;
    struct JunctionInfo { bool isJunction; double vt, vcrit; bool pair = false; double vt2 = 0.0, vcrit2 = 0.0; }; // pair: antiparallel diodes, forward limits on both sides
    JunctionInfo junction[maxPortsV] {};       // per voltage port: p-n junction limiting data (SPICE pnjlim)
    double xLinear[maxUnknowns] {};
#ifdef NODAL_DEBUG
    double dbgTrace[100][12] {};
#endif
    double Dscratch[maxPortsI][maxPortsV] {};
    double DscratchNext[maxPortsI][maxPortsV] {};

    static constexpr double gmin = 1.0e-12;

    // ---------------------------------------------------------------- stamps

    /** Adds `val` to the coefficient of node `col`'s voltage in node
        `row`'s KCL equation (moving known-node terms to the right-hand
        side). Rows owned by an op-amp output are constraint rows and take
        no KCL contributions. */
    void addCoeff (Node row, Node col, double val, double (&A)[maxUnknowns][maxUnknowns], double* rhs) const noexcept
    {
        const int r = row <= 0 ? -1 : indexOf[(size_t) row];
        if (r < 0 || isOpAmpRow[(size_t) r])
            return;

        const int c = col <= 0 ? -1 : indexOf[(size_t) col];
        if (c >= 0)
            A[r][c] += val;
        else if (col > 0)
            rhs[r] -= val * knownVoltage[(size_t) col];
    }

    void inject (Node row, double amps, double* rhs) const noexcept
    {
        const int r = row <= 0 ? -1 : indexOf[(size_t) row];
        if (r >= 0 && ! isOpAmpRow[(size_t) r])
            rhs[r] += amps;
    }

    /** Known-node terms of a conductance g between a and b (the matrix
        part is in rebuildLinearMatrix). */
    void stampKnownContribution (Node a, Node b, double g, double* rhs) const noexcept
    {
        const int ia = a <= 0 ? -1 : indexOf[(size_t) a];
        const int ib = b <= 0 ? -1 : indexOf[(size_t) b];

        if (ia >= 0 && ! isOpAmpRow[(size_t) ia] && ib < 0 && b > 0)
            rhs[ia] += g * knownVoltage[(size_t) b];
        if (ib >= 0 && ! isOpAmpRow[(size_t) ib] && ia < 0 && a > 0)
            rhs[ib] += g * knownVoltage[(size_t) a];
    }

    void stampConductance (Node a, Node b, double g, double (&A)[maxUnknowns][maxUnknowns]) const noexcept
    {
        double dummy[maxUnknowns] {};
        addCoeff (a, a, g, A, dummy);
        addCoeff (a, b, -g, A, dummy);
        addCoeff (b, b, g, A, dummy);
        addCoeff (b, a, -g, A, dummy);
    }

    /** Right-hand side of an op-amp's row: the ideal constraint V+ - V- = 0 (any known input pin moved across), or,
        for a saturating op-amp held at a rail, that rail. */
    double opRhs (const OpTerm& o) const noexcept
    {
        if (o.saturating && satMode == 1)
            return satSpec.highRail;
        if (o.saturating && satMode == 2)
            return satSpec.lowRail;
        const double kp = (o.plus > 0 && indexOf[(size_t) o.plus] < 0) ? knownVoltage[(size_t) o.plus] : 0.0;
        const double km = (o.minus > 0 && indexOf[(size_t) o.minus] < 0) ? knownVoltage[(size_t) o.minus] : 0.0;
        return o.offset - (kp - km);
    }

    void constraintRhs (const Op& o, double* rhs) const noexcept
    {
        const int r = indexOf[(size_t) o.out];
        if (r < 0)
            return;
        const double kp = (o.plus > 0 && indexOf[(size_t) o.plus] < 0) ? knownVoltage[(size_t) o.plus] : 0.0;
        const double km = (o.minus > 0 && indexOf[(size_t) o.minus] < 0) ? knownVoltage[(size_t) o.minus] : 0.0;
        rhs[r] = o.offset - (kp - km);
    }

    void rebuildLinearMatrix (bool includeCaps) noexcept
    {
        for (int i = 0; i < unknownCount; ++i)
            for (int j = 0; j < unknownCount; ++j)
                linearMatrix[i][j] = 0.0;

        for (const auto& r : resistors)
            stampConductance (r.a, r.b, r.g, linearMatrix);

        if (includeCaps)
            for (const auto& c : capacitors)
                stampConductance (c.a, c.b, c.g, linearMatrix);

        // gmin from every unknown node to ground keeps nodes that only
        // connect through capacitors (or float in DC) well-posed.
        for (int i = 0; i < unknownCount; ++i)
            if (! isOpAmpRow[(size_t) i])
                linearMatrix[i][i] += gmin;

        // Op-amp constraint rows: V+ - V- = 0 replaces the output node's KCL.
        for (const auto& o : opAmps)
        {
            const int r = indexOf[(size_t) o.out];
            if (r < 0)
                continue;
            const int ip = o.plus > 0 ? indexOf[(size_t) o.plus] : -1;
            const int im = o.minus > 0 ? indexOf[(size_t) o.minus] : -1;
            if (&o == &opAmps[(size_t) std::max (satOpIndex, 0)] && satOpIndex >= 0 && satMode != 0)
            {
                linearMatrix[r][r] = 1.0; // held at a rail: V(out) = rail, the constraint is dropped
                continue;
            }
            if (ip >= 0) linearMatrix[r][ip] += 1.0;
            if (im >= 0) linearMatrix[r][im] -= 1.0;
        }

        knownTerms.clear();
        auto addKnown = [this] (Node a, Node b, double g)
        {
            const int ia = a <= 0 ? -1 : indexOf[(size_t) a];
            const int ib = b <= 0 ? -1 : indexOf[(size_t) b];
            if (ia >= 0 && ! isOpAmpRow[(size_t) ia] && ib < 0 && b > 0)
                knownTerms.push_back ({ ia, b, g });
            if (ib >= 0 && ! isOpAmpRow[(size_t) ib] && ia < 0 && a > 0)
                knownTerms.push_back ({ ib, a, g });
        };
        for (const auto& r : resistors)
            addKnown (r.a, r.b, r.g);
        if (includeCaps)
            for (const auto& c : capacitors)
                addKnown (c.a, c.b, c.g);

        matrixDirty = false;
        matrixHasCaps = includeCaps;
        luValid = false;
    }

    bool matrixHasCaps = true;

    // ------------------------------------------------------ nonlinear stamps

    double vAt (Node n, const std::vector<double>& xv) const noexcept
    {
        if (n <= 0)
            return 0.0;
        const int i = indexOf[(size_t) n];
        return i >= 0 ? xv[(size_t) i] : knownVoltage[(size_t) n];
    }

    /** Linearises a multi-terminal nonlinear device around `xv` and stamps
        it: i_t(v) ~ i0_t + sum_u J_tu (v_u - v0_u) leaving node t. */
    template <int N>
    void stampDevice (const Node (&nodes)[N], const double (&i0)[N], const double (&J)[N][N],
                      const double (&v0)[N]) noexcept
    {
        for (int t = 0; t < N; ++t)
        {
            double constant = i0[t];
            for (int u = 0; u < N; ++u)
            {
                addCoeff (nodes[t], nodes[u], J[t][u], workMatrix, rhsWork.data());
                constant -= J[t][u] * v0[u];
            }
            inject (nodes[t], -constant, rhsWork.data());
        }
    }

    void stampNonlinear (const std::vector<double>& xv) noexcept
    {
        for (const auto& d : diodes)
        {
            const double v = vAt (d.a, xv) - vAt (d.k, xv);
            const double arg = std::min (v / d.nVt, 40.0);
            const double e = std::exp (arg);
            const double i = d.Is * (e - 1.0);
            const double g = d.Is / d.nVt * e;

            const Node nodes[2] = { d.a, d.k };
            const double i0[2] = { i, -i };
            const double J[2][2] = { { g, -g }, { -g, g } };
            const double v0[2] = { vAt (d.a, xv), vAt (d.k, xv) };
            stampDevice<2> (nodes, i0, J, v0);
        }

        for (auto& q : bjts)
        {
            const double vc = vAt (q.c, xv), vb = vAt (q.b, xv), ve = vAt (q.e, xv);
            const double s = q.pnp ? -1.0 : 1.0; // PNP: mirror voltages in, currents out (see EbersMollBJT)
            const auto op = q.model.evaluate (s * vb, s * ve, s * vc);

            // d/dvb = d/dvbe + d/dvbc, d/dve = -d/dvbe, d/dvc = -d/dvbc. Terminal order: c, b, e.
            const double J[3][3] = {
                { -op.diC_dvbc, op.diC_dvbe + op.diC_dvbc, -op.diC_dvbe },
                { -op.diB_dvbc, op.diB_dvbe + op.diB_dvbc, -op.diB_dvbe },
                { -op.diE_dvbc, op.diE_dvbe + op.diE_dvbc, -op.diE_dvbe }
            };
            const double i0[3] = { s * op.iC, s * op.iB, s * op.iE };
            const Node nodes[3] = { q.c, q.b, q.e };
            const double v0[3] = { vc, vb, ve };
            stampDevice<3> (nodes, i0, J, v0);
        }

        for (auto& j : jfets)
        {
            const double vd = vAt (j.d, xv), vg = vAt (j.g, xv), vs = vAt (j.s, xv);
            double iD, dD, dS;
            j.model.evaluate (vg, vd, vs, iD, dD, dS);
            const double dG = -dD - dS;

            // Terminal order: d, g, s. Gate current is zero.
            const double J[3][3] = { { dD, dG, dS }, { 0.0, 0.0, 0.0 }, { -dD, -dG, -dS } };
            const double i0[3] = { iD, 0.0, -iD };
            const Node nodes[3] = { j.d, j.g, j.s };
            const double v0[3] = { vd, vg, vs };
            stampDevice<3> (nodes, i0, J, v0);
        }

    }

    bool hasNonlinear() const noexcept { return ! diodes.empty() || ! bjts.empty() || ! jfets.empty(); }

    // ---------------------------------------------------------------- solve

    /** Gaussian elimination with partial pivoting, in place. */
    bool luSolve (double (&A)[maxUnknowns][maxUnknowns], double* b, int n) const noexcept
    {
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            double best = std::abs (A[col][col]);
            for (int r = col + 1; r < n; ++r)
                if (std::abs (A[r][col]) > best)
                {
                    best = std::abs (A[r][col]);
                    pivot = r;
                }

            if (best < 1.0e-30)
                return false;

            if (pivot != col)
            {
                for (int c = col; c < n; ++c)
                    std::swap (A[col][c], A[pivot][c]);
                std::swap (b[col], b[pivot]);
            }

            const double inv = 1.0 / A[col][col];
            for (int r = col + 1; r < n; ++r)
            {
                const double f = A[r][col] * inv;
                if (f == 0.0)
                    continue;
                for (int c = col + 1; c < n; ++c)
                    A[r][c] -= f * A[col][c];
                b[r] -= f * b[col];
            }
        }

        for (int r = n - 1; r >= 0; --r)
        {
            double sum = b[r];
            for (int c = r + 1; c < n; ++c)
                sum -= A[r][c] * b[c];
            b[r] = sum / A[r][r];
        }

        return true;
    }

    /** Newton-Raphson on the assembled system. `rhsLinear` must be current.
        Limits per-iteration voltage steps once nonlinear devices are in
        play so an exponential can't be shot to infinity by a bad first
        step. */
    bool newton (bool withCaps, int maxIterations, double tolerance) noexcept
    {
        (void) withCaps;
        const int n = unknownCount;
        const bool nonlinear = hasNonlinear();

        for (int iter = 0; iter < maxIterations; ++iter)
        {
            for (int i = 0; i < n; ++i)
            {
                rhsWork[(size_t) i] = rhsLinear[(size_t) i];
                for (int j = 0; j < n; ++j)
                    workMatrix[i][j] = linearMatrix[i][j];
            }

            if (nonlinear)
                stampNonlinear (x);

            // Solve for the new x directly (not a delta): rhs already holds
            // the linearised constants.
            double solution[maxUnknowns];
            for (int i = 0; i < n; ++i)
                solution[i] = rhsWork[(size_t) i];

            if (! luSolve (workMatrix, solution, n))
                return false;

            double maxStep = 0.0;
            for (int i = 0; i < n; ++i)
                maxStep = std::max (maxStep, std::abs (solution[i] - x[(size_t) i]));

            double scale = 1.0;
            if (nonlinear && maxStep > 1.0)
                scale = 1.0 / maxStep;

            for (int i = 0; i < n; ++i)
                x[(size_t) i] += scale * (solution[i] - x[(size_t) i]);

#ifdef NODAL_DEBUG
            if (iter < 8 || iter % 20 == 0)
            {
                std::printf ("  newton it=%d maxStep=%.4g scale=%.3g x:", iter, maxStep, scale);
                for (int q = 0; q < n; ++q) std::printf (" %.3f", x[(size_t) q]);
                std::printf ("\n");
            }
#endif
            if (! nonlinear)
                return true;

            if (scale >= 1.0 && maxStep < tolerance)
                return true;
        }

        return false;
    }

    // ------------------------------------------------------ reduced (DK) model

    /** In-place LU of the linear matrix with partial pivoting; multipliers
        are kept in the lower triangle. */
    bool luFactor() noexcept
    {
        const int n = unknownCount;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                lu[i][j] = linearMatrix[i][j];

        for (int col = 0; col < n; ++col)
        {
            int best = col;
            double bestAbs = std::abs (lu[col][col]);
            for (int r = col + 1; r < n; ++r)
                if (std::abs (lu[r][col]) > bestAbs)
                {
                    bestAbs = std::abs (lu[r][col]);
                    best = r;
                }

            if (bestAbs < 1.0e-30)
                return false;

            pivot[col] = best;
            if (best != col)
                for (int c = 0; c < n; ++c)
                    std::swap (lu[col][c], lu[best][c]);

            const double inv = 1.0 / lu[col][col];
            for (int r = col + 1; r < n; ++r)
            {
                const double f = lu[r][col] * inv;
                lu[r][col] = f;
                if (f == 0.0)
                    continue;
                for (int c = col + 1; c < n; ++c)
                    lu[r][c] -= f * lu[col][c];
            }
        }
        buildSparsePattern();
        return true;
    }

    /** The circuit matrices are sparse (each node touches a handful of others), so the per-sample substitution
        walks only the nonzero multipliers / U entries, kept as compact index+value lists, and multiplies by the
        stored reciprocal of the diagonal instead of dividing. */
    void buildSparsePattern() noexcept
    {
        const int n = unknownCount;
        lCount = uCount = 0;
        for (int col = 0; col < n; ++col)
        {
            lStart[col] = lCount;
            for (int r = col + 1; r < n; ++r)
                if (lu[r][col] != 0.0)
                {
                    lRow[lCount] = r;
                    lVal[lCount++] = lu[r][col];
                }
        }
        lStart[n] = lCount;
        for (int r = 0; r < n; ++r)
        {
            uStart[r] = uCount;
            for (int c = r + 1; c < n; ++c)
                if (lu[r][c] != 0.0)
                {
                    uCol[uCount] = c;
                    uVal[uCount++] = lu[r][c];
                }
            invDiag[r] = 1.0 / lu[r][r];
        }
        uStart[n] = uCount;
    }

    /** Solves A z = b in place using the stored factorisation. */
    void luBackSubstitute (double* b) const noexcept
    {
        const int n = unknownCount;

        // The factorisation swapped whole rows (multipliers included), so
        // apply every interchange first, then forward-substitute with L.
        for (int col = 0; col < n; ++col)
            if (pivot[col] != col)
                std::swap (b[col], b[pivot[col]]);

        for (int col = 0; col < n; ++col)
        {
            const double bc = b[col];
            if (bc != 0.0)
                for (int k = lStart[col]; k < lStart[col + 1]; ++k)
                    b[lRow[k]] -= lVal[k] * bc;
        }

        for (int r = n - 1; r >= 0; --r)
        {
            double sum = b[r];
            for (int k = uStart[r]; k < uStart[r + 1]; ++k)
                sum -= uVal[k] * b[uCol[k]];
            b[r] = sum * invDiag[r];
        }
    }

    void buildPorts()
    {
        currentPorts.clear();
        voltagePorts.clear();
        deviceMaps.clear();
        for (auto& j : junction)
            j = { false, 0.0, 0.0 };
        diodePairs.clear();

        auto addJunction = [this] (double Is, double vt)
        {
            const size_t j = voltagePorts.size() - 1;
            if (j < (size_t) maxPortsV)
                junction[j] = { true, vt, vt * std::log (vt / (1.4142135623730951 * Is)) };
        };

        // Antiparallel diodes (a clipper) are ONE device with ONE port: i(v) = Is1 (e^{v/n1} - 1) - Is2 (e^{-v/n2} - 1).
        // Exactly the same circuit, one Newton dimension fewer per pair.
        std::vector<bool> merged (diodes.size(), false);
        for (size_t i = 0; i < diodes.size(); ++i)
        {
            if (merged[i])
                continue;
            int partner = -1;
            for (size_t j = i + 1; j < diodes.size() && partner < 0; ++j)
                if (! merged[j] && diodes[j].a == diodes[i].k && diodes[j].k == diodes[i].a)
                    partner = (int) j;

            if (partner >= 0)
            {
                merged[(size_t) partner] = true;
                diodePairs.push_back ({ (int) i, partner });
                deviceMaps.push_back ({ 5, (int) voltagePorts.size(), (int) currentPorts.size(), (int) diodePairs.size() - 1, 1, 1 });
                const size_t j = voltagePorts.size();
                voltagePorts.push_back ({ diodes[i].a, diodes[i].k });
                currentPorts.push_back ({ diodes[i].a, diodes[i].k, 1.0 });
                const auto& d1 = diodes[i];
                const auto& d2 = diodes[(size_t) partner];
                if (j < (size_t) maxPortsV)
                    junction[j] = { true, d1.nVt, d1.nVt * std::log (d1.nVt / (1.4142135623730951 * d1.Is)), true,
                                    d2.nVt, d2.nVt * std::log (d2.nVt / (1.4142135623730951 * d2.Is)) };
                continue;
            }

            deviceMaps.push_back ({ 0, (int) voltagePorts.size(), (int) currentPorts.size(), (int) i, 1, 1 });
            voltagePorts.push_back ({ diodes[i].a, diodes[i].k });
            addJunction (diodes[i].Is, diodes[i].nVt);
            currentPorts.push_back ({ diodes[i].a, diodes[i].k, 1.0 });
        }

        for (size_t i = 0; i < bjts.size(); ++i)
        {
            const auto& q = bjts[i];

            // An emitter follower (or any transistor whose collector sits on a fixed rail) never forward-biases its
            // collector junction, whose current is ~1e-15 A: it is a one-port device (vbe). Same answer, one port fewer.
            const bool collectorFixed = q.c <= 0 || indexOf[(size_t) q.c] < 0;
            if (collectorFixed)
            {
                deviceMaps.push_back ({ 4, (int) voltagePorts.size(), (int) currentPorts.size(), (int) i, 1, 2 });
                if (! q.pnp)
                {
                    voltagePorts.push_back ({ q.b, q.e });
                    addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                    currentPorts.push_back ({ q.c, q.e, 1.0 });
                    currentPorts.push_back ({ q.b, q.e, 1.0 });
                }
                else
                {
                    voltagePorts.push_back ({ q.e, q.b });
                    addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                    currentPorts.push_back ({ q.c, q.e, -1.0 });
                    currentPorts.push_back ({ q.b, q.e, -1.0 });
                }
                continue;
            }

            deviceMaps.push_back ({ 1, (int) voltagePorts.size(), (int) currentPorts.size(), (int) i, 2, 2 });
            if (! q.pnp)
            {
                voltagePorts.push_back ({ q.b, q.e }); // vbe
                addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                voltagePorts.push_back ({ q.b, q.c }); // vbc
                addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                currentPorts.push_back ({ q.c, q.e, 1.0 }); // iC
                currentPorts.push_back ({ q.b, q.e, 1.0 }); // iB
            }
            else
            {
                // Mirrored coordinates: ports are the negated junction voltages and the real
                // terminal currents are the negated mirrored ones (see EbersMollBJT's PNP note).
                voltagePorts.push_back ({ q.e, q.b });
                addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                voltagePorts.push_back ({ q.c, q.b });
                addJunction (q.model.saturationCurrentValue(), q.model.thermalVoltageValue());
                currentPorts.push_back ({ q.c, q.e, -1.0 });
                currentPorts.push_back ({ q.b, q.e, -1.0 });
            }
        }

        for (size_t i = 0; i < jfets.size(); ++i)
        {
            const auto& j = jfets[i];
            if (j.assumedVds > 0.0)
            {
                deviceMaps.push_back ({ 6, (int) voltagePorts.size(), (int) currentPorts.size(), (int) i, 1, 1 });
                voltagePorts.push_back ({ j.g, j.s }); // vgs only
                currentPorts.push_back ({ j.d, j.s, 1.0 }); // iD
                continue;
            }
            deviceMaps.push_back ({ 2, (int) voltagePorts.size(), (int) currentPorts.size(), (int) i, 2, 1 });
            voltagePorts.push_back ({ j.g, j.s }); // vgs
            voltagePorts.push_back ({ j.d, j.s }); // vds
            currentPorts.push_back ({ j.d, j.s, 1.0 }); // iD
        }

    }

    double portCoeff (const double* w, Node n) const noexcept
    {
        if (n <= 0)
            return 0.0;
        const int i = indexOf[(size_t) n];
        return i >= 0 ? w[i] : 0.0;
    }

    /** Factors the linear matrix and precomputes, for every device current
        port k, W_k = A^-1 e_k and the port-to-port coupling K. */
    void factorAndBuildReducedModel()
    {
        luValid = luFactor();
        if (! luValid)
            return;

        buildPorts();

        for (size_t k = 0; k < currentPorts.size(); ++k)
        {
            double e[maxUnknowns] {};
            const auto& cp = currentPorts[k];
            const int ip = cp.plus > 0 ? indexOf[(size_t) cp.plus] : -1;
            const int im = cp.minus > 0 ? indexOf[(size_t) cp.minus] : -1;
            if (ip >= 0 && ! isOpAmpRow[(size_t) ip]) e[ip] += cp.sign;
            if (im >= 0 && ! isOpAmpRow[(size_t) im]) e[im] -= cp.sign;

            luBackSubstitute (e);
            for (int i = 0; i < unknownCount; ++i)
                W[k][i] = e[i];

            for (size_t j = 0; j < voltagePorts.size(); ++j)
                K[j][k] = portCoeff (W[k], voltagePorts[j].plus) - portCoeff (W[k], voltagePorts[j].minus);
        }
    }

    /** Newton-Raphson over the device port voltages, then reconstruct the
        full node vector. */
    bool solveDevices() noexcept
    {
        const int nv = (int) voltagePorts.size();
        const int ni = (int) currentPorts.size();

        if (nv == 0)
        {
            for (int i = 0; i < unknownCount; ++i)
                x[(size_t) i] = xLinear[i];
            return true;
        }

        double u0[maxPortsV], u[maxPortsV];
        for (int j = 0; j < nv; ++j)
        {
            const auto& vp = voltagePorts[(size_t) j];
            u0[j] = nodeOf (vp.plus, xLinear) - nodeOf (vp.minus, xLinear);
            u[j] = uState[j];
            if (predictorEnabled)
            {
                // Warm start along the port voltage's recent trajectory; a junction is held to the same
                // logarithmic limit the Newton steps obey, so a fast edge cannot throw the start into an
                // exponential's overflow region.
                double guess = uState[j] + predictWeight * (uState[j] - uStatePrev[j]);
                if (junction[j].pair)
                    guess = guess >= 0.0 ? limitJunction (guess, std::max (uState[j], 0.0), junction[j].vt, junction[j].vcrit)
                                         : -limitJunction (-guess, std::max (-uState[j], 0.0), junction[j].vt2, junction[j].vcrit2);
                else if (junction[j].isJunction)
                    guess = limitJunction (guess, uState[j], junction[j].vt, junction[j].vcrit);
                u[j] = guess;
            }
        }

        double cur[maxPortsI], curNext[maxPortsI];
        bool converged = false;
        double previousStep = 1.0e9;
        double (*Dnow)[maxPortsV] = Dscratch;
        double (*Dnext)[maxPortsV] = DscratchNext;

        ++samplesSolved;
        evaluateDevices (u, cur, Dnow);

        for (int iter = 0; iter < 100 && ! converged; ++iter)
        {
            ++iterationsTotal;

            // J = I + K * D, exploiting that D is block-diagonal (each device
            // couples only its own currents to its own port voltages).
            double J[maxPortsV][maxPortsV + 1];
            for (int j = 0; j < nv; ++j)
            {
                for (int c = 0; c < nv; ++c)
                    J[j][c] = (j == c) ? 1.0 : 0.0;

                double f = u[j] - u0[j];
                for (int k = 0; k < ni; ++k)
                    f += K[j][k] * cur[k];
                J[j][nv] = f;
            }

            for (const auto& m : deviceMaps)
                for (int k = m.i0; k < m.i0 + m.ni; ++k)
                    for (int c = m.v0; c < m.v0 + m.nv; ++c)
                    {
                        const double dk = Dnow[k][c];
                        for (int j = 0; j < nv; ++j)
                            J[j][c] += K[j][k] * dk;
                    }
            // Solve J * delta = f (small dense system, partial pivoting; one reciprocal per pivot, not a division
            // per element).
            double invPivot[maxPortsV];
            for (int col = 0; col < nv; ++col)
            {
                int best = col;
                double bestAbs = std::abs (J[col][col]);
                for (int r = col + 1; r < nv; ++r)
                {
                    const double a = std::abs (J[r][col]);
                    if (a > bestAbs)
                    {
                        bestAbs = a;
                        best = r;
                    }
                }
                if (bestAbs < 1.0e-30)
                    return false;
                if (best != col)
                    for (int c = col; c <= nv; ++c)
                        std::swap (J[col][c], J[best][c]);
                const double inv = 1.0 / J[col][col];
                invPivot[col] = inv;
                for (int r = col + 1; r < nv; ++r)
                {
                    const double fct = J[r][col] * inv;
                    if (! (fct < 0.0 || fct > 0.0))
                        continue;
                    for (int c = col + 1; c <= nv; ++c)
                        J[r][c] -= fct * J[col][c];
                }
            }

            double delta[maxPortsV];
            for (int r = nv - 1; r >= 0; --r)
            {
                double sum = J[r][nv];
                for (int c = r + 1; c < nv; ++c)
                    sum -= J[r][c] * delta[c];
                delta[r] = sum * invPivot[r];
            }

            // Proposed update, with SPICE-style logarithmic limiting on p-n junction ports: an exponential's
            // plain Newton step from a low-current start overshoots by orders of magnitude, then sheds only
            // ~Vt per iteration on the way back.
            double proposed[maxPortsV];
            bool limited = false;
            double maxStep = 0.0;
            for (int j = 0; j < nv; ++j)
            {
                double unew = u[j] - delta[j];
                if (junction[j].pair)
                {
                    // Antiparallel pair: the forward limit of whichever diode is (about to be) conducting.
                    const double limitedValue = unew >= 0.0
                        ? limitJunction (unew, std::max (u[j], 0.0), junction[j].vt, junction[j].vcrit)
                        : -limitJunction (-unew, std::max (-u[j], 0.0), junction[j].vt2, junction[j].vcrit2);
                    if (limitedValue != unew)
                        limited = true;
                    unew = limitedValue;
                }
                else if (junction[j].isJunction)
                {
                    const double limitedValue = limitJunction (unew, u[j], junction[j].vt, junction[j].vcrit);
                    if (limitedValue != unew)
                        limited = true;
                    unew = limitedValue;
                }
                proposed[j] = unew;
                maxStep = std::max (maxStep, std::abs (unew - u[j]));
            }

            const bool fullStep = maxStep <= 1.0;
            const double scale = fullStep ? 1.0 : 1.0 / maxStep;
            double step[maxPortsV];
            for (int j = 0; j < nv; ++j)
            {
                step[j] = scale * (proposed[j] - u[j]);
                u[j] += step[j];
            }

#ifdef NODAL_DEBUG
            dbgTrace[iter % 100][0] = maxStep;
            dbgTrace[iter % 100][1] = limited ? 1.0 : 0.0;
            for (int q = 0; q < nv && q < 8; ++q) dbgTrace[iter % 100][2 + q] = u[q];
#endif
            // What the step predicted the device currents would do (D * step), then what they actually do at the
            // new point. The Newton step solves the LINEARISED equations exactly, so the whole leftover error is
            // the difference between those two -- push it through W and it is the error in every node voltage,
            // the thing the audio hears. No estimate about where a linearisation holds: it is measured.
            double predictedDelta[maxPortsI];
            for (int k = 0; k < ni; ++k)
                predictedDelta[k] = 0.0;
            for (const auto& m : deviceMaps)
                for (int k = m.i0; k < m.i0 + m.ni; ++k)
                    for (int c = m.v0; c < m.v0 + m.nv; ++c)
                        predictedDelta[k] += Dnow[k][c] * step[c];
            evaluateDevices (u, curNext, Dnext);

            double nodeErrorVec[maxUnknowns];
            for (int node = 0; node < unknownCount; ++node)
                nodeErrorVec[node] = 0.0;
            for (int k = 0; k < ni; ++k)
            {
                const double residual = curNext[k] - cur[k] - predictedDelta[k];
                const double* Wk = W[k];
                for (int node = 0; node < unknownCount; ++node)
                    nodeErrorVec[node] += Wk[node] * residual;
            }
            double nodeError = 0.0;
            for (int node = 0; node < unknownCount; ++node)
                nodeError = std::max (nodeError, std::abs (nodeErrorVec[node]));

            // Accept the new point (with the currents evaluated AT it, so the reconstruction below is consistent
            // with it) when the leftover is below tolerance; or, at the noise floor, when steps have stopped shrinking.
            if (nodeError < nodeTolerance || (fullStep && ! limited && iter > 3 && maxStep < 1.0e-4 && maxStep > 0.9 * previousStep))
                converged = true;
            previousStep = maxStep;

            for (int k = 0; k < ni; ++k)
                cur[k] = curNext[k];
            std::swap (Dnow, Dnext);
        }

        if (! converged)
        {
#ifdef NODAL_DEBUG
            for (int it = 92; it < 100; ++it) { std::printf ("  it%d step=%.3g lim=%.0f u:", it, dbgTrace[it][0], dbgTrace[it][1]); for (int q = 0; q < nv && q < 8; ++q) std::printf (" %.4f", dbgTrace[it][2 + q]); std::printf ("\n"); }
            std::printf ("DK Newton failed: nv=%d ni=%d u0:", nv, ni);
            for (int j = 0; j < nv; ++j) std::printf (" %.3f", u0[j]);
            std::printf (" | u:");
            for (int j = 0; j < nv; ++j) std::printf (" %.3f", u[j]);
            std::printf ("\n");
#endif
            return false;
        }

        // `cur` was evaluated AT the accepted u, so the node vector below is consistent with it.
        for (int i = 0; i < unknownCount; ++i)
        {
            double v = xLinear[i];
            for (int k = 0; k < ni; ++k)
                v -= W[k][i] * cur[k];
            x[(size_t) i] = v;
        }

        for (int j = 0; j < nv; ++j)
        {
            uStatePrev[j] = uState[j];
            uState[j] = u[j];
        }

        return true;
    }

    /** SPICE's pnjlim: compresses a large forward-voltage Newton step logarithmically. */
    static double limitJunction (double vnew, double vold, double vt, double vcrit) noexcept
    {
        if (vnew > vcrit && std::abs (vnew - vold) > 2.0 * vt)
        {
            if (vold > 0.0)
            {
                const double arg = (vnew - vold) / vt;
                if (arg > 0.0)
                    vnew = vold + vt * (2.0 + std::log (arg - 2.0));
                else
                    vnew = vold - vt * (2.0 + std::log (2.0 - arg));
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

    double nodeOf (Node n, const double* xv) const noexcept
    {
        if (n <= 0)
            return 0.0;
        const int i = indexOf[(size_t) n];
        return i >= 0 ? xv[i] : knownVoltage[(size_t) n];
    }

    /** Device currents (one per current port) and d(current)/d(port voltage). */
    void evaluateDevices (const double* u, double* cur, double (*D)[maxPortsV]) const noexcept
    {
        for (const auto& m : deviceMaps)
        {
            if (m.kind == 0)
            {
                const auto& d = diodes[(size_t) m.index];
                const double e = std::exp (std::min (u[m.v0] / d.nVt, 40.0));
                cur[m.i0] = d.Is * (e - 1.0);
                D[m.i0][m.v0] = d.Is / d.nVt * e;
            }
            else if (m.kind == 1)
            {
                const auto& q = bjts[(size_t) m.index];
                // u[v0] = vbe, u[v0+1] = vbc (mirrored for PNP): pass vb=vbe, ve=0, vc=vbe-vbc.
                const auto op = q.model.evaluate (u[m.v0], 0.0, u[m.v0] - u[m.v0 + 1]);
                cur[m.i0] = op.iC;
                cur[m.i0 + 1] = op.iB;
                D[m.i0][m.v0] = op.diC_dvbe;
                D[m.i0][m.v0 + 1] = op.diC_dvbc;
                D[m.i0 + 1][m.v0] = op.diB_dvbe;
                D[m.i0 + 1][m.v0 + 1] = op.diB_dvbc;
            }
            else if (m.kind == 4)
            {
                const auto& q = bjts[(size_t) m.index];
                // vbc pinned far reverse: its exponential is ~1e-85 and drops out.
                const auto op = q.model.evaluate (u[m.v0], 0.0, u[m.v0] + 5.0);
                cur[m.i0] = op.iC;
                cur[m.i0 + 1] = op.iB;
                D[m.i0][m.v0] = op.diC_dvbe;
                D[m.i0 + 1][m.v0] = op.diB_dvbe;
            }
            else if (m.kind == 6)
            {
                const auto& j = jfets[(size_t) m.index];
                double iD, dD, dS;
                j.model.evaluate (u[m.v0], j.assumedVds, 0.0, iD, dD, dS);
                cur[m.i0] = iD;
                D[m.i0][m.v0] = -dD - dS; // d/dvgs
            }
            else if (m.kind == 5)
            {
                const auto& pr = diodePairs[(size_t) m.index];
                const auto& d1 = diodes[(size_t) pr.first];
                const auto& d2 = diodes[(size_t) pr.second];
                const double e1 = std::exp (std::min (u[m.v0] / d1.nVt, 40.0));
                const double e2 = std::exp (std::min (-u[m.v0] / d2.nVt, 40.0));
                cur[m.i0] = d1.Is * (e1 - 1.0) - d2.Is * (e2 - 1.0);
                D[m.i0][m.v0] = d1.Is / d1.nVt * e1 + d2.Is / d2.nVt * e2;
            }
            else
            {
                const auto& j = jfets[(size_t) m.index];
                double iD, dD, dS;
                j.model.evaluate (u[m.v0], u[m.v0 + 1], 0.0, iD, dD, dS);
                cur[m.i0] = iD;
                D[m.i0][m.v0] = -dD - dS; // d/dvgs
                D[m.i0][m.v0 + 1] = dD;   // d/dvds
            }
        }
    }

    /** DC by pseudo-transient relaxation: backward-Euler steps of growing size
        starting from the initial guess. Early steps (tiny dt) make every
        capacitor a stiff anchor at its guessed voltage, so the nonlinear
        devices are re-linearised gently; as dt grows the circuit relaxes to
        its true DC point. Far more robust than a raw Newton on the static
        equations for amplifier stages with high loop gain. Returns true once
        the state has stopped changing. */
    bool relaxToDc() noexcept
    {
        // Start every capacitor at the guessed node voltages (unknown nodes not guessed start at 0 V).
        for (auto& c : capacitors)
        {
            c.vPrev = voltage (c.a) - voltage (c.b);
            c.iPrev = 0.0;
        }

        xPrev = x;
        for (int j = 0; j < maxPortsV; ++j)
            uState[j] = uStatePrev[j] = 0.0;

        // The operating point is computed once, so it gets the tight tolerance the per-sample loop cannot afford.
        struct ToleranceScope
        {
            double& target; double saved;
            explicit ToleranceScope (double& t) : target (t), saved (t) { target = 1.0e-9; }
            ~ToleranceScope() { target = saved; }
        } toleranceScope (nodeTolerance);

        double dt = 1.0e-8;
        double lastChange = 1.0;
        int consecutiveSmall = 0;

        for (int step = 0; step < 400; ++step)
        {
            beDt = dt;
            for (auto& c : capacitors)
                c.g = c.farads / dt;
            matrixDirty = true;

            // Ports' warm start: previous node voltages.
            rebuildLinearMatrix (true);
            factorAndBuildReducedModel();
            if (! luValid)
                return false;
            if (step == 0)
                for (size_t j = 0; j < voltagePorts.size(); ++j)
                    uState[j] = uStatePrev[j] = voltage (voltagePorts[j].plus) - voltage (voltagePorts[j].minus);

            double* rhs = rhsLinear.data();
            for (int i = 0; i < unknownCount; ++i)
                rhs[i] = 0.0;
            for (const auto& t : knownTerms)
                rhs[t.row] += t.g * knownVoltage[(size_t) t.knownNode];
            for (auto& c : capacitors)
            {
                c.ieq = c.g * c.vPrev;
                if (c.ia >= 0) rhs[c.ia] += c.ieq;
                if (c.ib >= 0) rhs[c.ib] -= c.ieq;
            }
            for (const auto& o : opTerms)
                rhs[o.row] = opRhs (o);

            for (int i = 0; i < unknownCount; ++i)
                xLinear[i] = rhs[i];
            luBackSubstitute (xLinear);

            const std::vector<double> before = x;
            if (! solveDevices())
            {
                x = before;
                dt *= 0.25; // too big a jump for Newton -- retry gentler
                if (dt < 1.0e-12)
                    return false;
                continue;
            }

            double change = 0.0;
            for (int i = 0; i < unknownCount; ++i)
                change = std::max (change, std::abs (x[(size_t) i] - before[(size_t) i]));

            for (auto& c : capacitors)
                c.vPrev = voltage (c.a) - voltage (c.b);

            lastChange = change;
            dt = std::min (dt * 1.6, 1.0);

            if (dt >= 1.0 && change < 1.0e-7)
            {
                if (++consecutiveSmall >= 3)
                    break;
            }
            else
                consecutiveSmall = 0;
        }

        xPrev = x;
        return lastChange < 1.0e-6;
    }

    /** DC operating point: capacitors open, sources stepped up from 0. */
    bool solveDc() noexcept
    {
        const bool savedDc = dcMode;
        dcMode = true;
        rebuildLinearMatrix (false);

        const std::vector<double> targetVoltage = knownVoltage;
        bool ok = true;

        for (int step = 1; step <= 10 && ok; ++step)
        {
            const double frac = step / 10.0;
            for (size_t n = 1; n < knownVoltage.size(); ++n)
                if (known[n])
                    knownVoltage[n] = targetVoltage[n] * frac;

            for (int i = 0; i < unknownCount; ++i)
                rhsLinear[(size_t) i] = 0.0;

            for (auto& r : resistors)
                stampKnownContribution (r.a, r.b, r.g, rhsLinear.data());
            for (const auto& o : opAmps)
                constraintRhs (o, rhsLinear.data());

            ok = newton (false, 200, 1.0e-9);
        }

        knownVoltage = targetVoltage;
        xPrev = x;
        dcMode = savedDc;
        rebuildLinearMatrix (true);
        factorAndBuildReducedModel();
        for (size_t j = 0; j < voltagePorts.size(); ++j)
            uState[j] = uStatePrev[j] = voltage (voltagePorts[j].plus) - voltage (voltagePorts[j].minus);
        return ok;
    }
};

} // namespace openguitarmultifx
