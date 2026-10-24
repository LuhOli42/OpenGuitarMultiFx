#pragma once

#include "../EbersMollBJT.h"
#include "../TubeModels.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace openguitarmultifx::pipeline
{

/** A tabulated memoryless correction current delta(v) (and its slope) for a transfer port: what a device's real
    characteristic adds on top of the linear part already stamped in the matrix. Built on the control thread. */
struct TransferCurve
{
    static constexpr int size = 1025;
    double vMin = -1.0, vMax = 1.0, step = 1.0, invStep = 1.0;
    std::array<double, size> delta {}, slope {};

    template <typename Fn> void build (double lo, double hi, Fn&& deltaOf)
    {
        vMin = lo; vMax = hi;
        step = (hi - lo) / (size - 1);
        invStep = 1.0 / step;
        for (int k = 0; k < size; ++k) delta[(size_t) k] = deltaOf (lo + k * step);
        for (int k = 0; k < size; ++k)
        {
            const int a = juce::jmax (0, k - 1), b = juce::jmin (size - 1, k + 1);
            slope[(size_t) k] = (delta[(size_t) b] - delta[(size_t) a]) / ((b - a) * step);
        }
    }
    void eval (double v, double& d, double& ds) const noexcept
    {
        if (! (v == v)) { d = ds = 0.0; return; } // NaN never indexes the table
        const double x = (juce::jlimit (vMin, vMax, v) - vMin) * invStep;
        const int k = juce::jmin (size - 2, (int) x);
        const double f = x - k;
        ds = slope[(size_t) k] + f * (slope[(size_t) k + 1] - slope[(size_t) k]);
        d = delta[(size_t) k] + f * (delta[(size_t) k + 1] - delta[(size_t) k]);
        if (v > vMax) d += slope[size - 1] * (v - vMax);
        else if (v < vMin) d += slope[0] * (v - vMin);
    }
};

/** A one-port device characteristic I(v), dI/dv tabulated once (the "static curve"): the sample loop does table lookups
    instead of exp(). Linear interpolation on a 1 mV-class grid; beyond the grid the end slope continues. */
struct DeviceCurve
{
    static constexpr int size = 2049;
    double vMin = -1.0, vMax = 1.0, invStep = 1.0;
    std::array<double, size> current {}, slope {};

    template <typename Fn, typename Dn> void build (double lo, double hi, Fn&& currentOf, Dn&& slopeOf)
    {
        vMin = lo; vMax = hi;
        const double step = (hi - lo) / (size - 1);
        invStep = 1.0 / step;
        for (int k = 0; k < size; ++k) { current[(size_t) k] = currentOf (lo + k * step); slope[(size_t) k] = slopeOf (lo + k * step); }
    }
    void eval (double v, double& i, double& di) const noexcept
    {
        if (! (v == v)) { i = di = 0.0; return; } // NaN never indexes the table
        const double x = (juce::jlimit (vMin, vMax, v) - vMin) * invStep;
        const int k = juce::jmin (size - 2, (int) x);
        const double f = x - k;
        di = slope[(size_t) k] + f * (slope[(size_t) k + 1] - slope[(size_t) k]);
        i = current[(size_t) k] + f * (current[(size_t) k + 1] - current[(size_t) k]);
        if (v > vMax) i += slope[size - 1] * (v - vMax);
        else if (v < vMin) i += slope[0] * (v - vMin);
    }
};

/** Diodes sharing one node pair (any orientation, series chains merged into one exponential each), summed into one
    port: I(v) = sum s_i Is_i (e^(s_i v / nVt_i) - 1). The table holds the sum; the terms give a far-side Newton start. */
struct CompositeDiode
{
    struct Term { double is, nVt, sign; };
    std::array<Term, 6> terms {};
    int n = 0;
    DeviceCurve table;
};

/** A whole transistor as a two-variable port pair (vbe, vbc) -> (ib, ic): Ebers-Moll with both junctions, so it
    saturates and cuts off like the netlist's two-port device. Currents are deviations from the DC operating point
    (the linear network carries no stamp for this transistor). `sign` -1 mirrors for PNP. */
struct BjtDevice
{
    EbersMollBJT model;
    double sign = 1.0, vbe0 = 0.0, vbc0 = 0.0, ib0 = 0.0, ic0 = 0.0;

    void init (const EbersMollBJT& m, bool pnp, double vbe, double vbc)
    {
        model = m; sign = pnp ? -1.0 : 1.0; vbe0 = vbe; vbc0 = vbc;
        ib0 = ic0 = 0.0;
        double ib = 0.0, ic = 0.0, di[4];
        eval (0.0, 0.0, ib, ic, di);
        ib0 = ib; ic0 = ic;
    }
    /** Deviation currents (into base, into collector) and their Jacobian d(ib, ic)/d(ube, ubc) as di[0..3]. */
    void eval (double ube, double ubc, double& ib, double& ic, double* di) const noexcept
    {
        const double vbe = sign * (vbe0 + ube), vbc = sign * (vbc0 + ubc);
        const auto op = model.evaluate (vbe, 0.0, vbe - vbc);
        ib = sign * op.iB - ib0;
        ic = sign * op.iC - ic0;
        di[0] = op.diB_dvbe; di[1] = op.diB_dvbc; di[2] = op.diC_dvbe; di[3] = op.diC_dvbc;
    }
};

/** A triode as a two-variable port pair (vgk, vpk) -> (ig, ip), Koren plate + grid current, as deviations from the DC
    point (the linear network carries no stamp for it). Plate current is a power law (no exponential runaway), so its
    Newton step needs only a coarse limit; grid current is near-exponential above zero bias. */
struct TriodeDevice
{
    KorenTriode model;
    double vgk0 = 0.0, vpk0 = 0.0, ig0 = 0.0, ip0 = 0.0;

    void init (const KorenTriode& m, double vgk, double vpk)
    {
        model = m; vgk0 = vgk; vpk0 = vpk;
        const auto o = model.evaluate (vgk0, vpk0);
        ig0 = o.ig; ip0 = o.ip;
    }
    /** Deviation currents (grid, plate) and d(ig, ip)/d(ugk, upk) as di[0..3]. */
    void eval (double ugk, double upk, double& ig, double& ip, double* di) const noexcept
    {
        const auto o = model.evaluate (vgk0 + ugk, vpk0 + upk);
        ig = o.ig - ig0;
        ip = o.ip - ip0;
        di[0] = o.dig_dvgk; di[1] = 0.0; di[2] = o.dip_dvgk; di[3] = o.dip_dvpk;
    }
};

/** A small-signal circuit as a LINEAR nodal network plus a few nonlinear one-port devices ("ports"), solved per sample
    with no circuit-level Newton:
      - resistors, capacitors (trapezoidal companions) and VCCS (a transistor at its operating point) form a constant
        conductance matrix, inverted only when a resistance changes (a pot moved) -- never inside the sample loop;
      - per sample: one matrix-vector product gives the linear solution, then each port is solved on its own Thevenin
        equivalent `v + Z * I(v) = p` with a FIXED number of monotone Newton steps from a guess on the right side of the
        root (constant cost, no failure path), the couplings between ports taken from this sample for ports already
        solved and from the previous sample for the rest;
      - the port currents are injected back through the stored inverse columns.
    The network is compiled at rebuild into its state-space form over what the sample loop actually needs (capacitor
    voltages, port voltages, the probed outputs), so a sample costs about (caps + ports + probes) x caps multiplies
    instead of nodes^2.
    Fixed capacity, no allocation; copyable (the dual-mono shortcut copies whole channels). */
template <int MaxNodes, int MaxPorts = 4, int MaxCaps = 16, int MaxResistors = 32, int MaxVccs = 6, int MaxProbes = 2, typename Real = double,
          int MaxEntries = 64, int MaxRows = 8>
class LinearPortNetwork
{
public:
    using Node = int;
    static constexpr Node ground = -1;

    Node addNode() { jassert (numNodes < MaxNodes); return numNodes++; }

    int addResistor (Node a, Node b, double ohms)
    {
        jassert (numResistors < MaxResistors);
        resistors[(size_t) numResistors] = { a, b, ohms };
        dirty = true;
        return numResistors++;
    }
    void setResistance (int id, double ohms)
    {
        auto& r = resistors[(size_t) id];
        if (r.ohms != ohms) { r.ohms = ohms; resistorsDirty = true; }
    }

    int addCapacitor (Node a, Node b, double farads)
    {
        jassert (numCaps < MaxCaps);
        caps[(size_t) numCaps] = { a, b, farads, 0.0, 0.0 };
        dirty = true;
        return numCaps++;
    }

    /** An inductor: the same trapezoidal state slot as a capacitor, with conductance T / 2L and the history running the
        other way (i = g v + h, h' = 2 g v + h). `henries` stored in the capacitor's value field. */
    int addInductor (Node a, Node b, double henries)
    {
        const int k = addCapacitor (a, b, henries);
        caps[(size_t) k].inductor = true;
        return k;
    }
    void setCapacitance (int id, double farads)
    {
        auto& c = caps[(size_t) id];
        if (c.farads != farads) { c.farads = farads; dirty = true; }
    }

    /** Current gm * (v(cp) - v(cn)) leaves node `outP` and enters node `outN`. */
    void addVccs (Node outP, Node outN, Node cp, Node cn, double gm)
    {
        jassert (numVccs < MaxVccs);
        vccs[(size_t) numVccs++] = { outP, outN, cp, cn, gm };
        dirty = true;
    }

    /** A raw conductance-matrix entry: current leaving node `row` gains y * v(col). For linearised devices (a
        transistor's 3x3 small-signal Jacobian, a JFET's gm / gds). */
    void addConductance (Node row, Node col, double y)
    {
        jassert (numEntries < MaxEntries);
        entries[(size_t) numEntries++] = { row, col, y };
        dirty = true;
    }

    /** A constraint row (an op-amp / follower / ideal source): sum coef_i v(node_i) = rhs, with one extra unknown, the
        current the constraint injects into node `out`. `isInput`: the rhs is the signal (setInput) instead of 0. Returns
        the row index; `rowNode (row)` names the row as an injection target for ports. */
    int addConstraint (Node out, std::initializer_list<std::pair<Node, double>> terms, bool isInput = false)
    {
        jassert (numRows < MaxRows);
        auto& r = rows[(size_t) numRows];
        r.out = out;
        r.n = 0;
        for (auto& t : terms) { if (r.n < 4) r.terms[(size_t) r.n++] = t; }
        r.input = isInput ? 0 : -1;
        dirty = true;
        return numRows++;
    }
    /** An input constraint row for signal input `index` (0 or 1): a block fed from two places (a dry/wet blend). */
    int addInputConstraint (Node node, int index)
    {
        const int r = addConstraint (node, { { node, 1.0 } }, true);
        rows[(size_t) r].input = index;
        return r;
    }
    static Node rowNode (int row) noexcept { return -(row + 2); }

    /** A whole transistor as two coupled ports solved together (BjtDevice): port k = base current (control vbe,
        injected b -> e), port k+1 = collector current (control vbc, injected c -> e). Do not stamp its Jacobian too. */
    int addBjt (Node c, Node b, Node e, const BjtDevice* dev)
    {
        Port pb { b, e, Kind::bjtBase, 0.0, 1.0, 0.0, 0.0, b, e };
        pb.bjt = dev;
        Port pc { c, e, Kind::bjtCollector, 0.0, 1.0, 0.0, 0.0, b, c };
        pc.bjt = dev;
        const int k = addPort (pb);
        addPort (pc);
        return k;
    }

    /** A triode as two coupled ports solved together (TriodeDevice): port k = grid current (control vgk, injected g -> k),
        port k+1 = plate current (control vpk, injected p -> k). Do not stamp it in the linear part too. */
    int addTriode (Node p, Node g, Node k, const TriodeDevice* dev)
    {
        Port pg { g, k, Kind::triodeGrid, 0.0, 1.0, 0.0, 0.0, g, k };
        pg.tri = dev;
        Port pp { p, k, Kind::triodePlate, 0.0, 1.0, 0.0, 0.0, p, k };
        pp.tri = dev;
        const int first = addPort (pg);
        addPort (pp);
        return first;
    }

    /** Unbiased diodes between a and b as one port (CompositeDiode, current a -> b); `c` must outlive the network. */
    int addCompositeDiode (Node a, Node b, const CompositeDiode* c)
    {
        Port p { a, b, Kind::composite, 0.0, 1.0, 0.0, 0.0, a, b };
        p.comp = c;
        return addPort (p);
    }

    /** Single diode anode -> cathode biased at `vBias` (DC): the linear part (its conductance at vBias) belongs in the
        matrix (add it with addResistor); the port carries the rest, Is (e^((vBias+v)/nVt) - e^(vBias/nVt)) - g0 v. */
    int addDiode (Node a, Node k, double is, double nVt, double vBias)
    {
        return addPort ({ a, k, Kind::diode, is, nVt, vBias, 0.0, a, k });
    }

    /** A swing limit whose correction goes into a constraint row instead of a node (an op-amp output running into
        its rail: the input-referred error the output stage needs, zero inside (-vNeg, vPos) of the DC point). */
    int addRowClamp (Node control, int row, double vPos, double vNeg, double is, double knee)
    {
        return addPort ({ ground, rowNode (row), Kind::clamp, is, knee, vPos, vNeg, control, ground });
    }

    /** The signal source: an ideal voltage behind `seriesOhms` into `n` (Norton form). One per network. */
    void setInputNode (Node n, double seriesOhms) { inputNode = n; inputG = 1.0 / seriesOhms; dirty = true; }
    void setInput (double volts) noexcept { inputVolts[0] = volts; }
    void setInput (int index, double volts) noexcept { inputVolts[(size_t) index] = volts; }
    void setPortSweeps (int n) noexcept { portSweeps = juce::jmax (1, n); }

    /** A node whose voltage is read every sample (the output). Only probed nodes can be read. */
    int addProbe (Node n) { jassert (numProbes < MaxProbes); probes[(size_t) numProbes] = n; dirty = true; return numProbes++; }
    double probe (int index) const noexcept { return probeValue[(size_t) index]; }
    juce::String stats() const { return "nodes " + juce::String (numNodes) + " rows " + juce::String (numRows) + " caps " + juce::String (numCaps)
                                        + " ports " + juce::String (numPorts) + " sweeps " + juce::String (activeSweeps); }

    /** Thevenin impedance between two nodes of the linear part (after prepare): the voltage a unit current injected
        a -> b produces across them. For deriving device limits from the circuit around them. */
    double theveninImpedance (Node a, Node b) const noexcept
    {
        auto m = [&] (Node r, Node c) {
            const int ur = unknownOf (r), uc = unknownOf (c);
            return (ur >= 0 && uc >= 0) ? inv[(size_t) ur][(size_t) uc] : 0.0;
        };
        return m (a, a) - m (a, b) - m (b, a) + m (b, b);
    }

    /** Anti-parallel diode pair between a and b: I = 2 Is sinh(v / nVt), current a -> b. */
    int addDiodePair (Node a, Node b, double is, double nVt) { return addPort ({ a, b, Kind::diodePair, is, nVt, 0.0, 0.0, a, b }); }

    /** The same diode pair with its characteristic read from a table built from the same Is / nVt (no exp() in the
        sample loop); `table` must outlive the network. */
    int addDiodePair (Node a, Node b, double is, double nVt, const DeviceCurve* table)
    {
        return addPort ({ a, b, Kind::diodePair, is, nVt, 0.0, 0.0, a, b, nullptr, table });
    }

    /** A nonlinear transconductance correction: current delta(v(ca) - v(cb)) leaves `oa` and enters `ob`. Use it for
        the part of a transistor's characteristic the linear VCCS does not cover; `curve` must outlive the network. */
    int addTransferPort (Node ca, Node cb, Node oa, Node ob, const TransferCurve* curve)
    {
        return addPort ({ oa, ob, Kind::transfer, 0.0, 1.0, 0.0, 0.0, ca, cb, curve });
    }

    /** Soft swing limit between a and b: no current inside (-vNeg, vPos), exponential beyond, like a transistor running
        into cutoff / saturation. */
    int addClamp (Node a, Node b, double vPos, double vNeg, double is, double knee, const DeviceCurve* expTable = nullptr)
    {
        return addPort ({ a, b, Kind::clamp, is, knee, vPos, vNeg, a, b, nullptr, expTable });
    }

    /** exp(x) for x in [0, 40], continued as a straight line above (expLin), as a table for the swing-limit ports. */
    static void buildExpTable (DeviceCurve& t)
    {
        t.build (0.0, expKnee, [] (double x) { return std::exp (x); }, [] (double x) { return std::exp (x); });
    }

    /** Builds the matrix for `sampleRate` and clears the state. Control thread. */
    /** The capacitor / inductor integration rule, as NodalCircuit::setIntegrationTheta(): 0.5 = trapezoidal, larger
        damps the top octave (the netlist's default is 0.6, and a compiled block must integrate as its netlist does or
        the two differ on every hard edge). */
    void setTheta (double t) { if (t != theta) { theta = t; dirty = true; } }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        dirty = true;
        rebuild();
        reset();
    }

    void reset() noexcept
    {
        hist.fill (0.0);
        portVoltage.fill (0.0);
        for (int k = 0; k < numPorts; ++k) portCurrent[(size_t) k] = 0.0;
        probeValue.fill (0.0);
    }

    /** Bring the compiled form up to date; call at control rate. A structural change rebuilds (assemble + invert, n^3);
        resistance changes alone -- what a pot does -- update the inverse by one rank-1 (Sherman-Morrison) step per
        changed resistor (n^2), so a turning knob or a smoothed snapshot ramp, which moves pots every control tick,
        no longer pays a full inversion each time (measured: up to 84 % of a core in one block on the DT-1 before).
        A full rebuild every `incrementalLimit` updates bounds the round-off. Cheap no-op when nothing changed. */
    void update()
    {
        if (dirty) rebuild();
        else if (resistorsDirty) updateResistors();
    }
    /** Tests: a full assemble-and-invert now, the reference the rank-1 updates are checked against. */
    void rebuildNow() { rebuild(); }

    /** Dot product over n (a multiple of 4) with four independent accumulators: no loop-carried dependency, so it
        pipelines and vectorises (SSE / NEON) where a single running sum does not. */
    static Real dot (const Real* a, const Real* b, int n) noexcept
    {
        Real s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (int m = 0; m < n; m += 4)
        {
            s0 += a[m] * b[m];
            s1 += a[m + 1] * b[m + 1];
            s2 += a[m + 2] * b[m + 2];
            s3 += a[m + 3] * b[m + 3];
        }
        return (s0 + s1) + (s2 + s3);
    }

    void process() noexcept
    {
        // Loops run over the fixed capacities (unused rows/columns are zero) so the compiler can unroll and vectorise.
        // `Real` is the hot-loop precision (state and compiled matrices); the ports are always solved in double.
        const Real u = (Real) inputVolts[0], u1 = (Real) inputVolts[1];
        const int np = numPorts;
        const int ncPad = juce::jmin (MaxCaps, (numCaps + 7) & ~7), npPad = juce::jmin (MaxPorts, (numPorts + 3) & ~3);
        std::array<Real, MaxPorts> ic {};
        for (int j = 0; j < MaxPorts; ++j) ic[(size_t) j] = (Real) portCurrent[(size_t) j];

        std::array<double, MaxPorts> pLin {};
        for (int k = 0; k < np; ++k)
        {
            pLin[(size_t) k] = (double) (portIn[(size_t) k] * u + portIn1[(size_t) k] * u1 + dot (portH[(size_t) k].data(), hist.data(), ncPad));
        }
        // Gauss-Seidel over the ports, a fixed number of sweeps: each port on its Thevenin equivalent with the others'
        // latest currents. One sweep is enough for weakly coupled ports; strongly coupled ones (a transistor's ports
        // sharing nodes with a diode) need the second.
        for (int sweep = 0; sweep < activeSweeps; ++sweep)
            for (int k = 0; k < np; ++k)
            {
                if (ports[(size_t) k].kind == Kind::bjtBase)
                {
                    solveBjt (k, pLin);
                    ++k; // its collector port was solved with it
                    continue;
                }
                if (ports[(size_t) k].kind == Kind::triodeGrid)
                {
                    solveTriode (k, pLin);
                    ++k; // its plate port was solved with it
                    continue;
                }
                const auto& zr = z[(size_t) k];
                double pkd = pLin[(size_t) k];
                for (int j = 0; j < np; ++j) if (j != k) pkd -= zr[(size_t) j] * portCurrent[(size_t) j];
                portCurrent[(size_t) k] = solvePort (ports[(size_t) k], pkd, zr[(size_t) k], portVoltage[(size_t) k]);
            }
        for (int k = 0; k < np; ++k) ic[(size_t) k] = (Real) portCurrent[(size_t) k];

        std::array<Real, MaxCaps> next;
        for (int k = 0; k < ncPad; ++k)
        {
            const Real vc = capIn[(size_t) k] * u + capIn1[(size_t) k] * u1 + dot (capH[(size_t) k].data(), hist.data(), ncPad)
                          - dot (capPort[(size_t) k].data(), ic.data(), npPad);
            // theta-method history: twoG = +-(1 + w) g, stateSign = the history's own weight (see compile)
            next[(size_t) k] = twoG[(size_t) k] * vc + stateSign[(size_t) k] * hist[(size_t) k];
        }

        for (int q = 0; q < numProbes; ++q)
        {
            const Real y = probeIn[(size_t) q] * u + probeIn1[(size_t) q] * u1 + dot (probeH[(size_t) q].data(), hist.data(), ncPad)
                         - dot (probePort[(size_t) q].data(), ic.data(), npPad);
            probeValue[(size_t) q] = (double) y;
        }
        for (int k = 0; k < ncPad; ++k) hist[(size_t) k] = next[(size_t) k];
    }

    double portVoltageCurrent (int port) const noexcept { return portCurrent[(size_t) port]; }

private:
    enum class Kind { diodePair, clamp, transfer, diode, composite, bjtBase, bjtCollector, triodeGrid, triodePlate };
    struct Port { Node a, b; Kind kind; double is, nv, vPos, vNeg; Node ca = -1, cb = -1; const TransferCurve* curve = nullptr; const DeviceCurve* device = nullptr;
                  const CompositeDiode* comp = nullptr; const BjtDevice* bjt = nullptr; const TriodeDevice* tri = nullptr; };
    struct Resistor { Node a, b; double ohms; double gApplied = 0.0; }; // gApplied: the conductance `inv` holds
    struct Cap { Node a, b; double farads, hist, current; double g = 0.0; bool inductor = false; };
    struct Vccs { Node outP, outN, cp, cn; double gm; };
    struct Entry { Node r, c; double y; };
    struct Row { Node out; std::array<std::pair<Node, double>, 4> terms {}; int n = 0; int input = -1; };

    int addPort (Port p)
    {
        jassert (numPorts < MaxPorts);
        ports[(size_t) numPorts] = p;
        dirty = true;
        return numPorts++;
    }


    static double safeExp (double x) noexcept { return std::exp (juce::jlimit (-80.0, 80.0, x)); }

    /** exp() continued as a straight line above 40 (SPICE-style junction limiting): monotone and unbounded, so a port
        equation always has a root however far a high-gain stage pushes it, and never overflows. */
    static constexpr double expKnee = 40.0;
    static double expLin (double x) noexcept
    {
        static const double e40 = std::exp (expKnee);
        return x <= expKnee ? std::exp (juce::jmax (-80.0, x)) : e40 * (1.0 + (x - expKnee));
    }
    static double expLinSlope (double x) noexcept
    {
        static const double e40 = std::exp (expKnee);
        return x <= expKnee ? std::exp (juce::jmax (-80.0, x)) : e40;
    }

    static double current (const Port& p, double vk) noexcept
    {
        if (p.kind == Kind::transfer) { double d = 0.0, ds = 0.0; p.curve->eval (vk, d, ds); return d; }
        if (p.kind == Kind::composite) { double i = 0.0, di = 0.0; p.comp->table.eval (vk, i, di); return i; }
        if (p.kind == Kind::bjtBase || p.kind == Kind::bjtCollector || p.kind == Kind::triodeGrid || p.kind == Kind::triodePlate)
            return 0.0; // solved jointly, see solveBjt / solveTriode
        if (p.kind == Kind::diode)
        {
            const double e0 = expLin (p.vPos / p.nv);
            return p.is * (expLin ((p.vPos + vk) / p.nv) - e0) - p.is / p.nv * e0 * vk;
        }
        if (p.kind == Kind::diodePair)
            return 2.0 * p.is * std::sinh (juce::jlimit (-80.0, 80.0, vk / p.nv));
        if (vk > p.vPos) return p.is * (expLin ((vk - p.vPos) / p.nv) - 1.0);
        if (vk < -p.vNeg) return -p.is * (expLin ((-p.vNeg - vk) / p.nv) - 1.0);
        return 0.0; // exactly zero inside the swing window, so the sample loop can skip it
    }
    static double slope (const Port& p, double vk) noexcept
    {
        if (p.kind == Kind::diodePair)
            return 2.0 * p.is / p.nv * std::cosh (juce::jlimit (-80.0, 80.0, vk / p.nv));
        if (vk > p.vPos) return p.is / p.nv * expLinSlope ((vk - p.vPos) / p.nv);
        if (vk < -p.vNeg) return p.is / p.nv * expLinSlope ((-p.vNeg - vk) / p.nv);
        return 0.0;
    }

    /** The transistor's two ports together: u + Z I(u) = p with u = (vbe, vbc) deviations, I = (ib, ic), Z the 2x2
        block of port impedances (couplings to the other ports taken as in Gauss-Seidel). 2-D Newton from last sample's
        solution, steps limited to 2 Vt per junction (SPICE-style), fixed count. */
    void solveBjt (int k, const std::array<double, MaxPorts>& pLin) noexcept
    {
        const int np = numPorts, kb = k, kc = k + 1;
        const auto& dev = *ports[(size_t) kb].bjt;
        double p1 = pLin[(size_t) kb], p2 = pLin[(size_t) kc];
        for (int j = 0; j < np; ++j)
        {
            if (j == kb || j == kc) continue;
            p1 -= z[(size_t) kb][(size_t) j] * portCurrent[(size_t) j];
            p2 -= z[(size_t) kc][(size_t) j] * portCurrent[(size_t) j];
        }
        const double z11 = z[(size_t) kb][(size_t) kb], z12 = z[(size_t) kb][(size_t) kc];
        const double z21 = z[(size_t) kc][(size_t) kb], z22 = z[(size_t) kc][(size_t) kc];
        double u1 = portVoltage[(size_t) kb], u2 = portVoltage[(size_t) kc], ib = 0.0, ic = 0.0, di[4];
        const double vt = dev.model.thermalVoltageValue();
        const double vcrit = vt * std::log (vt / (1.4142135623730951 * dev.model.saturationCurrentValue()));
        // SPICE pnjlim on the absolute junction voltage: a step that takes a junction above its critical voltage is
        // compressed logarithmically; anywhere else the full Newton step stands (a reverse-biased collector junction
        // must be free to swing volts per sample)
        auto limit = [vt, vcrit] (double vOld, double vNew) {
            if (vNew > vcrit && std::abs (vNew - vOld) > 2.0 * vt)
            {
                if (vOld > 0.0)
                {
                    const double arg = 1.0 + (vNew - vOld) / vt;
                    return arg > 0.0 ? vOld + vt * std::log (arg) : vcrit;
                }
                return vt * std::log (vNew / vt);
            }
            return vNew;
        };
        const double sg = dev.sign;
        for (int it = 0; it < 6; ++it) // fewer steps lose the HM-2's saturation (band 4 dB at 3 steps)
        {
            dev.eval (u1, u2, ib, ic, di);
            const double f1 = u1 + z11 * ib + z12 * ic - p1;
            const double f2 = u2 + z21 * ib + z22 * ic - p2;
            // Jacobian of (f1, f2) wrt (u1, u2)
            const double a = 1.0 + z11 * di[0] + z12 * di[2], b = z11 * di[1] + z12 * di[3];
            const double c = z21 * di[0] + z22 * di[2], d = 1.0 + z21 * di[1] + z22 * di[3];
            const double det = a * d - b * c;
            if (std::abs (det) < 1.0e-30) break;
            const double n1 = u1 - (f1 * d - b * f2) / det, n2 = u2 - (a * f2 - c * f1) / det;
            // limit in the device's own (mirrored for PNP) junction voltages
            const double vbeOld = sg * (dev.vbe0 + u1), vbcOld = sg * (dev.vbc0 + u2);
            u1 = sg * limit (vbeOld, sg * (dev.vbe0 + n1)) - dev.vbe0;
            u2 = sg * limit (vbcOld, sg * (dev.vbc0 + n2)) - dev.vbc0;
        }
        dev.eval (u1, u2, ib, ic, di);
        portVoltage[(size_t) kb] = u1;
        portVoltage[(size_t) kc] = u2;
        portCurrent[(size_t) kb] = ib;
        portCurrent[(size_t) kc] = ic;
    }

    /** The triode's two ports together, same scheme as solveBjt: u + Z I(u) = p, 2x2 Newton from last sample's solution,
        fixed count. Steps limited to 0.25 V on the grid (its current turns on near-exponentially above zero bias) and
        25 V on the plate (a power law). */
    void solveTriode (int k, const std::array<double, MaxPorts>& pLin) noexcept
    {
        const int np = numPorts, kg = k, kp = k + 1;
        const auto& dev = *ports[(size_t) kg].tri;
        double p1 = pLin[(size_t) kg], p2 = pLin[(size_t) kp];
        for (int j = 0; j < np; ++j)
        {
            if (j == kg || j == kp) continue;
            p1 -= z[(size_t) kg][(size_t) j] * portCurrent[(size_t) j];
            p2 -= z[(size_t) kp][(size_t) j] * portCurrent[(size_t) j];
        }
        const double z11 = z[(size_t) kg][(size_t) kg], z12 = z[(size_t) kg][(size_t) kp];
        const double z21 = z[(size_t) kp][(size_t) kg], z22 = z[(size_t) kp][(size_t) kp];
        double u1 = portVoltage[(size_t) kg], u2 = portVoltage[(size_t) kp], ig = 0.0, ip = 0.0, di[4];
        for (int it = 0; it < 2; ++it) // 2 matched 4 on the Tube Driver (band 1.0 vs 2.0 dB) at 1.6x the speed
        {
            dev.eval (u1, u2, ig, ip, di);
            const double f1 = u1 + z11 * ig + z12 * ip - p1;
            const double f2 = u2 + z21 * ig + z22 * ip - p2;
            const double a = 1.0 + z11 * di[0] + z12 * di[2], b = z11 * di[1] + z12 * di[3];
            const double c = z21 * di[0] + z22 * di[2], d = 1.0 + z21 * di[1] + z22 * di[3];
            const double det = a * d - b * c;
            if (std::abs (det) < 1.0e-30) break;
            u1 -= juce::jlimit (-0.25, 0.25, (f1 * d - b * f2) / det);
            u2 -= juce::jlimit (-25.0, 25.0, (a * f2 - c * f1) / det);
        }
        dev.eval (u1, u2, ig, ip, di);
        portVoltage[(size_t) kg] = u1;
        portVoltage[(size_t) kp] = u2;
        portCurrent[(size_t) kg] = ig;
        portCurrent[(size_t) kp] = ip;
    }

    /** Solves v + z I(v) = p and returns I. f(v) = v + z I(v) - p is strictly increasing (and convex/concave either side
        of zero), so Newton converges from any start; the start is the previous sample's solution (the signal moves
        little between samples), and the step count is fixed: constant cost, no failure path. `vLast` is updated. */
    static double solvePort (const Port& d, double p, double z, double& vLast) noexcept
    {
        if (d.kind == Kind::transfer)
        {
            double vk = vLast, dl = 0.0, ds = 0.0;
            for (int it = 0; it < 1; ++it)
            {
                d.curve->eval (vk, dl, ds);
                vk -= (vk + z * dl - p) / juce::jmax (0.05, 1.0 + z * ds);
            }
            d.curve->eval (vk, dl, ds);
            vLast = vk;
            return dl;
        }
        if (d.kind == Kind::composite)
        {
            // I(0) = 0, increasing, convex for v > 0 and concave below: Newton converges monotonically from the far
            // side. Warm start when it is there; else the smallest single-term root in the push direction (each term
            // alone needs at least that voltage, so the sum's root is no further: a guaranteed far-side start).
            const auto& c = *d.comp;
            const double sgn = p >= 0.0 ? 1.0 : -1.0;
            double vk = vLast, i = 0.0, di = 0.0;
            c.table.eval (vk, i, di);
            double f = vk + z * i - p;
            int steps = 2;
            if (vk * sgn < 0.0 || f * sgn < 0.0)
            {
                double guess = std::abs (p);
                for (int t = 0; t < c.n; ++t)
                    if (c.terms[(size_t) t].sign * sgn > 0.0)
                        guess = juce::jmin (guess, c.terms[(size_t) t].nVt * std::log1p (std::abs (p) / juce::jmax (1e-300, z * c.terms[(size_t) t].is)));
                vk = sgn * guess;
                c.table.eval (vk, i, di);
                f = vk + z * i - p;
                steps = 4;
            }
            for (int it = 0; it < steps; ++it)
            {
                vk -= f / (1.0 + z * di);
                c.table.eval (vk, i, di);
                f = vk + z * i - p;
            }
            vLast = vk;
            return i;
        }
        if (d.kind == Kind::diode)
        {
            // dI(v) = I0 (e^(v/nVt) - 1) - g0 v is convex with its minimum (0) at v = 0, so f(v) = v + z dI(v) - p is
            // convex and increasing (z <= 1/g0 because g0 is in the matrix): any start with f >= 0 converges
            // monotonically. Far below conduction the current is negligible: skip.
            const double x0 = d.vPos / d.nv;
            const double i0 = d.is * expLin (x0);
            const double g0 = d.is * expLinSlope (x0) / d.nv;
            if (p < 0.0 && i0 < 1.0e-12) { vLast = p; return 0.0; }
            if (p >= 0.0 && d.is * (expLin (x0 + p / d.nv) - expLin (x0)) < 1.0e-12) { vLast = p; return 0.0; }
            auto fOf = [&] (double v, double& fp) {
                const double x = x0 + v / d.nv;
                fp = 1.0 + z * (d.is * expLinSlope (x) / d.nv - g0);
                return v + z * (d.is * expLin (x) - i0 - g0 * v) - p;
            };
            double fp = 0.0;
            double vk = vLast;
            double f = fOf (vk, fp);
            if (f < 0.0)
            {
                // far-side guess in the log domain (no under/overflow however reverse the bias): the root has
                // z Is e^x <= p, so x = ln(p / (z Is)) is at or beyond it
                vk = p > 0.0 ? juce::jmin (p, d.nv * (std::log (p / juce::jmax (1e-300, z * d.is)) - x0)) : p;
                f = fOf (vk, fp);
                if (f < 0.0) { vk = p; f = fOf (vk, fp); }
            }
            for (int it = 0; it < 4; ++it)
            {
                vk -= f / juce::jmax (1e-9, fp);
                f = fOf (vk, fp);
            }
            vLast = vk;
            return current (d, vk);
        }
        if (d.kind == Kind::diodePair)
        {
            // Newton converges monotonically from a start on the far side of the root from zero (f is convex for
            // v > 0, concave for v < 0). Use last sample's solution when it is on that side (checked with the first
            // step's own evaluation), else the closed-form far-side guess. With a table the characteristic is read,
            // not computed.
            const double sgn = p >= 0.0 ? 1.0 : -1.0;
            auto eval = [&] (double v, double& i, double& di) {
                if (d.device != nullptr) { d.device->eval (v, i, di); return; }
                const double e = std::exp (juce::jlimit (-60.0, 60.0, v / d.nv)), ie = 1.0 / e;
                i = d.is * (e - ie);
                di = d.is * (e + ie) / d.nv;
            };
            double vk = vLast, i = 0.0, di = 0.0;
            eval (vk, i, di);
            double f = vk + z * i - p;
            int steps = 2;
            if (f * sgn < 0.0 || vk * sgn < 0.0)
            {
                vk = sgn * juce::jmin (std::abs (p), d.nv * std::asinh (std::abs (p) / juce::jmax (1e-30, 2.0 * d.is * z)));
                eval (vk, i, di);
                f = vk + z * i - p;
                steps = 3;
            }
            for (int it = 0; it < steps; ++it)
            {
                vk -= f / (1.0 + z * di);
                eval (vk, i, di);
                f = vk + z * i - p;
            }
            vLast = vk;
            return i;
        }
        // clamp: no current inside the window; outside, the closed-form far-side guess and a fixed count
        if (p >= -d.vNeg && p <= d.vPos) { vLast = p; return 0.0; }
        // Beyond the window f is convex (above) / concave (below), so Newton converges monotonically from the far
        // side. A stage pinned on a rail stays there for many samples: start from last sample's solution when it is on
        // that far side (2 steps), else from the closed-form guess (4 steps).
        const double sgn = p >= 0.0 ? 1.0 : -1.0, ap = std::abs (p), lim = sgn > 0 ? d.vPos : d.vNeg;
        const double kz = d.is * z, invN = 1.0 / d.nv;
        // work on the excursion u = |v| - lim beyond the rail (u > 0): I = Is (e^(u/n) - 1), one exp per step
        auto eval = [&] (double u, double& f, double& fp) {
            const double x = u * invN;
            double e, es;
            if (d.device != nullptr) d.device->eval (x, e, es); // tabulated exp, linear beyond 40 like expLin
            else { e = expLin (x); es = expLinSlope (x); }
            f = lim + u + kz * (e - 1.0) - ap;
            fp = 1.0 + kz * es * invN;
            return d.is * (e - 1.0);
        };
        double u = vLast * sgn - lim, f = 0.0, fp = 1.0, i = 0.0;
        int steps = 2;
        if (u <= 0.0 || (i = eval (u, f, fp), f < 0.0))
        {
            u = juce::jmin (ap - lim, d.nv * std::log1p ((ap - lim) / juce::jmax (1e-30, kz)));
            steps = 4;
            i = eval (u, f, fp);
        }
        for (int it = 0; it < steps; ++it)
        {
            u -= f / fp;
            i = eval (u, f, fp);
        }
        vLast = sgn * (lim + u);
        return sgn * i;
    }

    /** Unknown index of a node / row reference: nodes 0..numNodes-1, rows after them, ground -1. */
    int unknownOf (Node n) const noexcept { return n >= 0 ? n : (n == ground ? -1 : numNodes + (-n - 2)); }

    void rebuild()
    {
        dirty = false;
        const int n = numNodes + numRows;
        jassert (n <= MaxNodes);
        std::array<std::array<double, MaxNodes>, MaxNodes> g {};
        auto stamp = [&] (Node a, Node b, double y) {
            if (a >= 0) g[(size_t) a][(size_t) a] += y;
            if (b >= 0) g[(size_t) b][(size_t) b] += y;
            if (a >= 0 && b >= 0) { g[(size_t) a][(size_t) b] -= y; g[(size_t) b][(size_t) a] -= y; }
        };
        for (int k = 0; k < numResistors; ++k) stamp (resistors[(size_t) k].a, resistors[(size_t) k].b, 1.0 / resistors[(size_t) k].ohms);
        for (int k = 0; k < numCaps; ++k)
        {
            auto& c = caps[(size_t) k];
            c.g = c.inductor ? theta / (c.farads * fs) : c.farads * fs / theta;
            stamp (c.a, c.b, c.g);
        }
        for (int k = 0; k < numVccs; ++k)
        {
            const auto& sv = vccs[(size_t) k];
            auto add = [&] (Node r, Node c, double y) { if (r >= 0 && c >= 0) g[(size_t) r][(size_t) c] += y; };
            add (sv.outP, sv.cp, sv.gm); add (sv.outP, sv.cn, -sv.gm);
            add (sv.outN, sv.cp, -sv.gm); add (sv.outN, sv.cn, sv.gm);
        }
        for (int k = 0; k < numEntries; ++k)
        {
            const auto& e = entries[(size_t) k];
            if (e.r >= 0 && e.c >= 0) g[(size_t) e.r][(size_t) e.c] += e.y;
        }
        if (inputNode >= 0) g[(size_t) inputNode][(size_t) inputNode] += inputG;
        for (int r = 0; r < numRows; ++r)
        {
            const auto& row = rows[(size_t) r];
            const int ri = numNodes + r;
            for (int t = 0; t < row.n; ++t)
                if (row.terms[(size_t) t].first >= 0) g[(size_t) ri][(size_t) row.terms[(size_t) t].first] += row.terms[(size_t) t].second;
            if (row.out >= 0) g[(size_t) row.out][(size_t) ri] -= 1.0; // the constraint's current enters `out`
        }

        // Gauss-Jordan with partial pivoting
        std::array<std::array<double, MaxNodes>, MaxNodes> a = g;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                inv[(size_t) r][(size_t) c] = r == c ? 1.0 : 0.0;
        for (int col = 0; col < n; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (a[(size_t) r][(size_t) col]) > std::abs (a[(size_t) piv][(size_t) col])) piv = r;
            std::swap (a[(size_t) col], a[(size_t) piv]);
            std::swap (inv[(size_t) col], inv[(size_t) piv]);
            const double d = a[(size_t) col][(size_t) col];
            jassert (std::abs (d) > 1e-300);
            const double invD = 1.0 / d;
            for (int c = 0; c < n; ++c) { a[(size_t) col][(size_t) c] *= invD; inv[(size_t) col][(size_t) c] *= invD; }
            for (int r = 0; r < n; ++r)
            {
                if (r == col) continue;
                const double f = a[(size_t) r][(size_t) col];
                if (f == 0.0) continue;
                for (int c = 0; c < n; ++c)
                {
                    a[(size_t) r][(size_t) c] -= f * a[(size_t) col][(size_t) c];
                    inv[(size_t) r][(size_t) c] -= f * inv[(size_t) col][(size_t) c];
                }
            }
        }

        for (int k = 0; k < numResistors; ++k) resistors[(size_t) k].gApplied = 1.0 / resistors[(size_t) k].ohms;
        resistorsDirty = false;
        incrementalUpdates = 0;
        compileStateSpace();
    }

    /** Rank-1 updates of `inv` for every resistor whose conductance moved: G' = G + dg d d^T with d = e_a - e_b, so
        inv' = inv - (dg / (1 + dg d^T inv d)) (inv d) (d^T inv). Falls back to a full rebuild when the denominator is
        near zero or after `incrementalLimit` updates. */
    void updateResistors()
    {
        resistorsDirty = false;
        if (++incrementalUpdates > incrementalLimit) { rebuild(); return; }
        const int n = numNodes + numRows;
        std::array<double, MaxNodes> u {}, w {};
        bool changed = false;
        for (int k = 0; k < numResistors; ++k)
        {
            auto& r = resistors[(size_t) k];
            const double gNew = 1.0 / r.ohms, dg = gNew - r.gApplied;
            if (dg == 0.0) continue;
            const int ua = unknownOf (r.a), ub = unknownOf (r.b);
            for (int i = 0; i < n; ++i)
            {
                u[(size_t) i] = (ua >= 0 ? inv[(size_t) i][(size_t) ua] : 0.0) - (ub >= 0 ? inv[(size_t) i][(size_t) ub] : 0.0);
                w[(size_t) i] = (ua >= 0 ? inv[(size_t) ua][(size_t) i] : 0.0) - (ub >= 0 ? inv[(size_t) ub][(size_t) i] : 0.0);
            }
            const double sdd = (ua >= 0 ? u[(size_t) ua] : 0.0) - (ub >= 0 ? u[(size_t) ub] : 0.0);
            const double denom = 1.0 + dg * sdd;
            if (std::abs (denom) < 1.0e-9) { rebuild(); return; }
            const double f = dg / denom;
            for (int i = 0; i < n; ++i)
            {
                const double fu = f * u[(size_t) i];
                if (fu == 0.0) continue;
                for (int j = 0; j < n; ++j) inv[(size_t) i][(size_t) j] -= fu * w[(size_t) j];
            }
            r.gApplied = gNew;
            changed = true;
        }
        if (changed) compileStateSpace();
    }

    /** The per-sample state-space form, from `inv`. */
    void compileStateSpace()
    {
        capIn.fill (0.0); portIn.fill (0.0); probeIn.fill (0.0); twoG.fill (0.0);
        capIn1.fill (0.0); portIn1.fill (0.0); probeIn1.fill (0.0);
        for (auto& r : capH) r.fill (0.0);
        for (auto& r : capPort) r.fill (0.0);
        for (auto& r : portH) r.fill (0.0);
        for (auto& r : z) r.fill (0.0);
        for (auto& r : probeH) r.fill (0.0);
        for (auto& r : probePort) r.fill (0.0);
        for (int k = 0; k < numCaps; ++k)
        {
            // theta-method history (i = g v - h): capacitor h' = (1 + w) g v - w h, inductor h' = h - (1 + w) g v,
            // w = (1 - theta) / theta (NodalCircuit::solveSample); theta = 0.5 is the trapezoid, h' = 2 g v - h
            const double w = (1.0 - theta) / theta;
            twoG[(size_t) k] = (Real) ((caps[(size_t) k].inductor ? -1.0 : 1.0) * (1.0 + w) * caps[(size_t) k].g);
            stateSign[(size_t) k] = (Real) (caps[(size_t) k].inductor ? 1.0 : -w);
        }

        // Compile to the state-space form. Every quantity read per sample is d^T v for a difference vector d (a cap's
        // or port's two nodes, a probe's one node); v = inv (Bcap h + e_in g_in u - sum_j d_j I_j).
        auto form = [&] (Node pa, Node pb, Node qa, Node qb) {
            auto m = [&] (Node r, Node c) {
                const int ur = unknownOf (r), uc = unknownOf (c);
                return (ur >= 0 && uc >= 0) ? inv[(size_t) ur][(size_t) uc] : 0.0;
            };
            return m (pa, qa) - m (pa, qb) - m (pb, qa) + m (pb, qb);
        };
        int inputUnknown[2] = { -1, -1 };
        double inputScale[2] = { 0.0, 0.0 };
        if (inputNode >= 0) { inputUnknown[0] = inputNode; inputScale[0] = inputG; }
        for (int r = 0; r < numRows; ++r)
            if (rows[(size_t) r].input >= 0) { inputUnknown[rows[(size_t) r].input] = numNodes + r; inputScale[rows[(size_t) r].input] = 1.0; }
        auto inFormAt = [&] (int idx, Node pa, Node pb) {
            if (inputUnknown[idx] < 0) return 0.0;
            const int ua = unknownOf (pa), ub = unknownOf (pb);
            const double ra = ua >= 0 ? inv[(size_t) ua][(size_t) inputUnknown[idx]] : 0.0;
            const double rb = ub >= 0 ? inv[(size_t) ub][(size_t) inputUnknown[idx]] : 0.0;
            return (ra - rb) * inputScale[idx];
        };
        auto inForm = [&] (Node pa, Node pb) { return inFormAt (0, pa, pb); };
        auto compileForms = [&] {
        for (int k = 0; k < numCaps; ++k)
        {
            const auto& ck = caps[(size_t) k];
            capIn[(size_t) k] = inForm (ck.a, ck.b);
            capIn1[(size_t) k] = inFormAt (1, ck.a, ck.b);
            for (int m = 0; m < numCaps; ++m) capH[(size_t) k][(size_t) m] = form (ck.a, ck.b, caps[(size_t) m].a, caps[(size_t) m].b);
            for (int j = 0; j < numPorts; ++j) capPort[(size_t) k][(size_t) j] = form (ck.a, ck.b, ports[(size_t) j].a, ports[(size_t) j].b);
        }
        for (int k = 0; k < numPorts; ++k)
        {
            const auto& pk = ports[(size_t) k];
            portIn[(size_t) k] = inForm (pk.ca, pk.cb);
            portIn1[(size_t) k] = inFormAt (1, pk.ca, pk.cb);
            for (int m = 0; m < numCaps; ++m) portH[(size_t) k][(size_t) m] = form (pk.ca, pk.cb, caps[(size_t) m].a, caps[(size_t) m].b);
            for (int j = 0; j < numPorts; ++j) z[(size_t) k][(size_t) j] = form (pk.ca, pk.cb, ports[(size_t) j].a, ports[(size_t) j].b);
        }
        for (int q = 0; q < numProbes; ++q)
        {
            const Node pn = probes[(size_t) q];
            probeIn[(size_t) q] = inForm (pn, ground);
            probeIn1[(size_t) q] = inFormAt (1, pn, ground);
            for (int m = 0; m < numCaps; ++m) probeH[(size_t) q][(size_t) m] = form (pn, ground, caps[(size_t) m].a, caps[(size_t) m].b);
            for (int j = 0; j < numPorts; ++j) probePort[(size_t) q][(size_t) j] = form (pn, ground, ports[(size_t) j].a, ports[(size_t) j].b);
        }
        };
        compileForms();
        // A port injecting into a constraint row has no physical orientation of its own: orient it so its Thevenin
        // impedance is positive (the correction opposes the excursion), which also keeps its Newton monotone.
        bool flipped = false;
        for (int k = 0; k < numPorts; ++k)
            if (z[(size_t) k][(size_t) k] < 0.0 && ports[(size_t) k].a == ground && ports[(size_t) k].b <= -2)
            {
                std::swap (ports[(size_t) k].a, ports[(size_t) k].b);
                flipped = true;
            }
        if (flipped) compileForms();

        // Gauss-Seidel needs its second sweep only where ports are strongly coupled (the product of the cross
        // impedances against the diagonal ones); weakly coupled networks get one sweep.
        double coupling = 0.0;
        for (int k = 0; k < numPorts; ++k)
            for (int j = k + 1; j < numPorts; ++j)
            {
                const double d = z[(size_t) k][(size_t) k] * z[(size_t) j][(size_t) j];
                if (d > 0.0) coupling = std::max (coupling, std::abs (z[(size_t) k][(size_t) j] * z[(size_t) j][(size_t) k]) / d);
            }
        activeSweeps = coupling < 0.05 ? 1 : portSweeps;
    }

    double fs = 48000.0;
    double theta = 0.5;
    int numNodes = 0, numResistors = 0, numCaps = 0, numVccs = 0, numPorts = 0, numProbes = 0;
    std::array<Resistor, MaxResistors> resistors {};
    std::array<Cap, MaxCaps> caps {};
    std::array<Vccs, MaxVccs> vccs {};
    std::array<Entry, MaxEntries> entries {};
    std::array<Row, MaxRows> rows {};
    int numEntries = 0, numRows = 0;
    std::array<Port, MaxPorts> ports {};
    std::array<double, MaxPorts> portCurrent {}, portVoltage {};
    std::array<std::array<double, MaxPorts>, MaxPorts> z {};
    std::array<std::array<double, MaxNodes>, MaxNodes> inv {};
    std::array<Node, MaxProbes> probes {};
    std::array<double, MaxProbes> probeValue {};
    std::array<Real, MaxProbes> probeIn {}, probeIn1 {};
    std::array<std::array<Real, MaxCaps>, MaxProbes> probeH {};
    std::array<std::array<Real, MaxPorts>, MaxProbes> probePort {};
    std::array<Real, MaxCaps> capIn {}, capIn1 {}, hist {}, twoG {}, stateSign {};
    std::array<std::array<Real, MaxCaps>, MaxCaps> capH {};
    std::array<std::array<Real, MaxPorts>, MaxCaps> capPort {};
    std::array<Real, MaxPorts> portIn {}, portIn1 {};
    std::array<std::array<Real, MaxCaps>, MaxPorts> portH {};
    Node inputNode = -1;
    double inputG = 0.0;
    std::array<double, 2> inputVolts {};
    bool dirty = true, resistorsDirty = false;
    int incrementalUpdates = 0;
    static constexpr int incrementalLimit = 256; // rank-1 updates between full rebuilds (round-off bound)
    int portSweeps = 1, activeSweeps = 1;
};

} // namespace openguitarmultifx::pipeline
