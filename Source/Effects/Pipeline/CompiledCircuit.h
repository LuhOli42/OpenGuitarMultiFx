#pragma once

#include "../NodalCircuit.h"
#include "LinearPortNetwork.h"

#include <functional>
#include <memory>
#include <vector>

namespace openguitarmultifx::pipeline
{

struct CompileOptions
{
    bool transistorExponential = false;      // per pedal, only where the equivalence test shows it pays (docs/PerformancePipeline.md)
    bool transistorSaturation = true;        // fold Vce(sat) into each two-port BJT's curve (see compile())
    bool transistorFull = false;             // each two-port BJT as a full two-junction port pair (BjtDevice): deep
                                             // saturation and cutoff like the netlist, at a 2x2 Newton per transistor
    int portSweeps = 2;                      // Gauss-Seidel sweeps over the ports per sample
    double saturationIs = 1.0e-4, saturationKnee = 0.05;
    double railIs = 1.0e-3, railKnee = 0.01;  // row rail port: input-referred volts, exponential beyond the rail
    double railNodeIs = 1.0e-2, railNodeKnee = 0.01; // node rail port on a converted follower (1-ohm source)
};

/** A NodalCircuit block compiled into a LinearPortNetwork at its DC operating point (docs/PerformancePipeline.md):
      - fixed sources (supplies, bias rails) are AC ground; the signal source becomes an input constraint row;
      - resistors keep their NodalCircuit handles (same order), so setResistance() / pots work unchanged;
      - op-amp / follower rows stay constraint rows; saturating and rail-limited op-amp outputs get a rail port;
      - BJTs and JFETs are their small-signal Jacobians at the DC point; each two-terminal-driven BJT can also get a
        transfer port carrying its exponential (Options::transistorExponential);
      - diodes are ports: anti-parallel unbiased pairs as one tabulated pair, the rest single exponential ports.
    The API mirrors the part of NodalCircuit a pedal's process() uses: setSource / setResistance / solveSample /
    voltage (for the probed nodes, DC included, so blocks chain exactly as their netlists do). */
class CompiledCircuit
{
public:
    using Node = NodalCircuit::Node;
    using Net = LinearPortNetwork<64, 16, 40, 96, 2, 4, float, 160, 12>;

    using Options = CompileOptions;

    /** `circuit` must be prepared (its DC point solved). `inputSource` is the handle of the source that carries the
        signal; `probes` the nodes that will be read. Returns false if the netlist holds an element this cannot express. */
    bool compile (const NodalCircuit& circuit, int inputSource, std::initializer_list<Node> probes, double sampleRate, const Options& o = {})
    {
        return compile (circuit, inputSource, -1, probes, sampleRate, o);
    }

    /** Two signal sources (a block fed from two blocks, e.g. a dry/wet blend): `secondSource` -1 for none. */
    bool compile (const NodalCircuit& circuit, int inputSource, int secondSource, std::initializer_list<Node> probes, double sampleRate,
                  const Options& o = {})
    {
        secondHandle = secondSource;
        NodalCircuit::Description d;
        circuit.describe (d);
        return compileImpl (d, inputSource, probes, sampleRate, o);
    }

    bool compileImpl (const NodalCircuit::Description& d, int inputSource, std::initializer_list<Node> probes, double sampleRate, const Options& o)
    {
        if (! d.supported) return false;
        net = std::make_unique<Net>();
        curves.clear();
        expCurve.reset();
        composites.clear();
        bjtDevices.clear();
        transfer.clear();
        dc = d.dc;
        inputHandle = inputSource;

        // node map: fixed (source-driven) nodes are AC ground, except the signal source's node
        std::vector<Node> map ((size_t) d.nodeCount + 1, Net::ground);
        std::vector<bool> fixed ((size_t) d.nodeCount + 1, false);
        for (const auto& src : d.sources)
            if (! src.current && src.node > 0) fixed[(size_t) src.node] = true;
        const Node inNode = d.sources[(size_t) inputSource].node;
        inputDc = d.sources[(size_t) inputSource].volts;
        const Node inNode2 = secondHandle >= 0 ? d.sources[(size_t) secondHandle].node : Node (0);
        secondDc = secondHandle >= 0 ? d.sources[(size_t) secondHandle].volts : 0.0;
        for (int n = 1; n <= d.nodeCount; ++n)
            if (! fixed[(size_t) n] || n == inNode || n == inNode2) map[(size_t) n] = net->addNode();
        auto m = [&] (Node n) { return n > 0 ? map[(size_t) n] : Net::ground; };

        net->addInputConstraint (m (inNode), 0);
        if (secondHandle >= 0) net->addInputConstraint (m (inNode2), 1);
        for (const auto& r : d.resistors) net->addResistor (m (r.a), m (r.b), r.ohms);
        capMap.assign (d.capacitors.size(), -1);
        for (size_t k = 0; k < d.capacitors.size(); ++k)
        {
            const auto& c = d.capacitors[k];
            if (m (c.a) < 0 && m (c.b) < 0) continue;
            if (c.inductor)
            {
                if (c.farads <= 0.0) return false;
                capMap[k] = net->addInductor (m (c.a), m (c.b), c.farads);
            }
            else
                capMap[k] = net->addCapacitor (m (c.a), m (c.b), c.farads);
        }
        // Op-amp rows. A saturating unity follower (an op-amp macro's output stage: + = integrator node, - = out = its
        // own output) becomes a 1-ohm controlled source instead of a constraint, so its rail limit can be an ordinary
        // node port on the output: the output stops at the rail while the integrator behind it carries on (clamped by
        // its own diodes) and the feedback network keeps charging -- what the netlist's saturating switch does.
        std::vector<int> rowOf (d.opRows.size(), -1);
        std::vector<bool> railed (d.opRows.size(), false);
        for (const auto& rail : d.rails) railed[(size_t) rail.row] = true;
        for (size_t k = 0; k < d.opRows.size(); ++k)
        {
            const auto& op = d.opRows[k];
            const bool follower = op.minus == op.out && op.invGain == 0.0 && op.cm == 0.0;
            if (railed[k] && follower)
            {
                constexpr double g = 1.0; // 1 ohm output: negligible next to the macro's 75
                net->addConductance (m (op.out), m (op.out), g);
                net->addConductance (m (op.out), m (op.plus), -g);
                continue;
            }
            rowOf[k] = net->addConstraint (m (op.out), { { m (op.plus), 1.0 + op.cm }, { m (op.minus), -(1.0 - op.cm) }, { m (op.out), -op.invGain } });
        }

        // diodes: unbiased anti-parallel pairs as one tabulated pair, the rest single ports. A converted follower's
        // integrator clamp diodes are replaced by the eco macro's anti-windup bleed (470 ohm integrator -> output,
        // NodalCircuit::addOpAmpMacroLite, verified there against the two-diode macro): one port per op-amp, not three
        // fighting over the same loop.
        std::vector<bool> used (d.diodes.size(), false);
        for (size_t k = 0; k < d.opRows.size(); ++k)
        {
            if (rowOf[k] >= 0 || ! railed[k]) continue;
            const Node px = d.opRows[k].plus;
            bool bled = false;
            for (size_t i = 0; i < d.diodes.size(); ++i)
            {
                const auto& di = d.diodes[i];
                if ((di.a == px && di.k > 0 && fixed[(size_t) di.k]) || (di.k == px && di.a > 0 && fixed[(size_t) di.a]))
                {
                    used[i] = true;
                    if (! bled) { stampConductance (m (px), m (d.opRows[k].out), 1.0 / 470.0); bled = true; }
                }
            }
        }
        // Series chains: a node touched by exactly two diodes, one in and one out, same Is, and nothing else but their own
        // junction capacitors is one diode of the summed nVt (exact for identical diodes).
        struct Dio { Node a, k; double is, nVt; };
        std::vector<Dio> dio;
        auto touches = [&] (Node n) {
            int count = 0;
            for (const auto& r : d.resistors) count += (r.a == n) + (r.b == n);
            for (const auto& op : d.opRows) count += (op.plus == n) + (op.minus == n) + (op.out == n);
            for (const auto& q : d.bjts) count += (q.c == n) + (q.b == n) + (q.e == n);
            for (const auto& j : d.jfets) count += (j.d == n) + (j.g == n) + (j.s == n);
            for (const auto& src : d.sources) count += (src.node == n);
            return count;
        };
        for (size_t i = 0; i < d.diodes.size(); ++i)
        {
            if (used[i]) continue;
            for (size_t j = 0; j < d.diodes.size(); ++j)
            {
                if (i == j || used[j]) continue;
                const auto& x = d.diodes[i];
                const auto& y = d.diodes[j];
                const Node mid = x.k;
                if (y.a != mid || mid <= 0 || fixed[(size_t) mid] || x.is != y.is || touches (mid) != 0) continue;
                if (x.a == y.k) continue; // that is an anti-parallel pair on (mid, x.a), not a chain
                bool foreignCap = false;  // a real capacitor at the middle node (not the diodes' own junctions) breaks the chain
                for (const auto& c : d.capacitors)
                {
                    const Node other = c.a == mid ? c.b : (c.b == mid ? c.a : -1);
                    if (other != -1 && other != x.a && other != y.k) foreignCap = true;
                }
                if (foreignCap) continue;
                int diodesAtMid = 0;
                for (size_t k2 = 0; k2 < d.diodes.size(); ++k2) diodesAtMid += (d.diodes[k2].a == mid) + (d.diodes[k2].k == mid);
                if (diodesAtMid != 2) continue;
                dio.push_back ({ x.a, y.k, x.is, x.nVt + y.nVt });
                used[i] = used[j] = true;
                break;
            }
        }
        for (size_t i = 0; i < d.diodes.size(); ++i)
            if (! used[i]) dio.push_back ({ d.diodes[i].a, d.diodes[i].k, d.diodes[i].is, d.diodes[i].nVt });

        // Unbiased diodes on the same node pair -> one composite port; a biased one alone -> a single diode port.
        std::vector<bool> taken (dio.size(), false);
        for (size_t i = 0; i < dio.size(); ++i)
        {
            if (taken[i]) continue;
            const Node a = dio[i].a, k = dio[i].k;
            const double bias = dc[(size_t) (a > 0 ? a : 0)] - dc[(size_t) (k > 0 ? k : 0)];
            if (std::abs (bias) > 0.1)
            {
                const double g0 = dio[i].is / dio[i].nVt * std::exp (juce::jlimit (-80.0, 80.0, bias / dio[i].nVt));
                stampConductance (m (a), m (k), g0);
                net->addDiode (m (a), m (k), dio[i].is, dio[i].nVt, bias);
                taken[i] = true;
                continue;
            }
            auto comp = std::make_unique<CompositeDiode>();
            double vHi = 0.05, vLo = 0.05;
            for (size_t j = i; j < dio.size() && comp->n < (int) comp->terms.size(); ++j)
            {
                if (taken[j]) continue;
                const bool same = dio[j].a == a && dio[j].k == k, reversed = dio[j].a == k && dio[j].k == a;
                if (! same && ! reversed) continue;
                const double sgn = same ? 1.0 : -1.0;
                comp->terms[(size_t) comp->n++] = { dio[j].is, dio[j].nVt, sgn };
                const double vOneAmp = dio[j].nVt * std::log (1.0 / dio[j].is); // where this one carries ~1 A
                (sgn > 0 ? vHi : vLo) = juce::jmax (sgn > 0 ? vHi : vLo, vOneAmp);
                taken[j] = true;
            }
            const auto* terms = comp.get();
            comp->table.build (-vLo, vHi,
                               [terms] (double v) { double s = 0; for (int t = 0; t < terms->n; ++t) { const auto& q = terms->terms[(size_t) t]; s += q.sign * q.is * std::expm1 (q.sign * v / q.nVt); } return s; },
                               [terms] (double v) { double s = 0; for (int t = 0; t < terms->n; ++t) { const auto& q = terms->terms[(size_t) t]; s += q.is / q.nVt * std::exp (q.sign * v / q.nVt); } return s; });
            net->addCompositeDiode (m (a), m (k), comp.get());
            composites.push_back (std::move (comp));
        }

        for (const auto& q : d.bjts)
        {
            if (o.transistorFull && ! q.onePort)
            {
                auto dev = std::make_unique<BjtDevice>();
                dev->init (q.model, q.pnp, q.vb - q.ve, q.vb - q.vc);
                net->addBjt (m (q.c), m (q.b), m (q.e), dev.get());
                bjtDevices.push_back (std::move (dev));
                continue;
            }
            const Node t[3] = { m (q.c), m (q.b), m (q.e) };
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    if (t[r] >= 0 && t[c] >= 0 && q.J[r][c] != 0.0) net->addConductance (t[r], t[c], q.J[r][c]);
            if (! o.transistorExponential) continue;
            // collector current's departure from its slope as vbe moves (vbc held: its effect is in the Jacobian)
            const double sg = q.pnp ? -1.0 : 1.0;
            auto icAt = [q, sg] (double v) {
                const double vbe = q.vb - q.ve + v;
                const auto op = q.onePort ? q.model.evaluate (sg * vbe, 0.0, sg * vbe + 5.0)
                                          : q.model.evaluate (sg * q.vb, sg * (q.ve - v), sg * q.vc);
                double ic = op.iC;
                if (q.iSat > 0.0 && ic > 0.0)
                {
                    const double r = ic / q.iSat, r2 = r * r;
                    ic /= std::sqrt (std::sqrt (1.0 + r2 * r2));
                }
                return sg * ic;
            };
            auto curve = std::make_unique<TransferCurve>();
            net->addTransferPort (m (q.b), m (q.e), m (q.c), m (q.e), curve.get());
            pending.push_back ({ curve.get(), icAt, m (q.c), m (q.e), q.pnp ? -(q.vc - q.ve) : (q.vc - q.ve), q.pnp ? -1.0 : 1.0, ! q.onePort && o.transistorSaturation });
            transfer.push_back (std::move (curve));
        }

        for (const auto& j : d.jfets)
        {
            const Node dn = m (j.d), gn = m (j.g), sn = m (j.s);
            const double gm = -(j.dD + j.dS);
            if (j.onePort)
            {
                net->addConductance (dn, gn, gm); net->addConductance (dn, sn, -gm);
                net->addConductance (sn, gn, -gm); net->addConductance (sn, sn, gm);
            }
            else
            {
                const double y[3] = { j.dD, gm, j.dS }; // d iD / d (vd, vg, vs)
                const Node t[3] = { dn, gn, sn };
                for (int c = 0; c < 3; ++c)
                {
                    net->addConductance (dn, t[c], y[c]);
                    net->addConductance (sn, t[c], -y[c]);
                }
            }
        }

        for (const auto& rail : d.rails)
        {
            const auto& op = d.opRows[(size_t) rail.row];
            const double v0 = dc[(size_t) op.out];
            if (rowOf[(size_t) rail.row] < 0) // converted follower: a node limit on its output
                net->addClamp (m (op.out), Net::ground, rail.hi - v0, v0 - rail.lo, o.railNodeIs, o.railNodeKnee, expTable());
            else
                net->addRowClamp (m (op.out), rowOf[(size_t) rail.row], rail.hi - v0, v0 - rail.lo, o.railIs, o.railKnee);
        }

        probeNodes.assign (probes);
        for (auto n : probeNodes) net->addProbe (m (n));
        net->setPortSweeps (o.portSweeps);
        net->prepare (sampleRate);

        // The transistor curves need the circuit around them: saturation is where the collector current has pulled
        // vce down to ~0.2 V, i.e. an extra (|Vce0| - 0.2) / Z_ce of current, Z_ce the linear part's impedance between
        // collector and emitter. Folded into the curve as a smooth ceiling (the one-port model's knee), so a transistor
        // stays ONE port.
        for (auto& pc : pending)
        {
            const double ic0 = pc.icAt (0.0), h = 1.0e-6, slope = (pc.icAt (h) - pc.icAt (-h)) / (2.0 * h);
            const double zce = std::abs (net->theveninImpedance (pc.c, pc.e));
            const double headroom = pc.saturate && zce > 0.0 ? juce::jmax (1.0e-6, (pc.vce0 - 0.2) / zce) : 0.0;
            auto icOf = pc.icAt;
            const double sgn = pc.sign;
            pc.curve->build (-0.45, 0.25, [=] (double v) {
                double di = sgn * (icOf (v) - ic0); // increase of |ic| over its bias
                if (headroom > 0.0 && di > 0.0)
                {
                    const double r = di / headroom, r2 = r * r;
                    di /= std::sqrt (std::sqrt (1.0 + r2 * r2));
                }
                return sgn * di - slope * v;
            });
        }
        pending.clear();
        net->update();
        return true;
    }

    void setSource (int handle, double volts) noexcept
    {
        if (handle == inputHandle) net->setInput (0, volts - inputDc);
        else if (handle == secondHandle) net->setInput (1, volts - secondDc);
    }
    void setResistance (int handle, double ohms) noexcept { net->setResistance (handle, ohms); }
    void setCapacitance (int handle, double farads) noexcept
    {
        if (handle >= 0 && handle < (int) capMap.size() && capMap[(size_t) handle] >= 0) net->setCapacitance (capMap[(size_t) handle], farads);
    }
    bool compiled() const noexcept { return net != nullptr; }
    void clear() { net.reset(); }
    juce::String stats() const { return net != nullptr ? net->stats() : juce::String ("not compiled"); }
    void update() { net->update(); }
    bool solveSample() noexcept { net->process(); return true; }
    double voltage (Node n) const noexcept
    {
        for (size_t k = 0; k < probeNodes.size(); ++k)
            if (probeNodes[k] == n) return dc[(size_t) n] + net->probe ((int) k);
        jassertfalse; // only probed nodes can be read
        return 0.0;
    }
    void reset() noexcept { net->reset(); }

    CompiledCircuit() = default;
    CompiledCircuit (const CompiledCircuit& o) { *this = o; }
    CompiledCircuit& operator= (const CompiledCircuit& o)
    {
        // The dual-mono shortcut copies channel 0 onto channel 1: only the dynamic state and the inputs differ.
        // Both channels were compiled from identical netlists, so a state copy is enough (no allocation).
        if (this != &o && net != nullptr && o.net != nullptr) *net = *o.net;
        return *this;
    }

private:
    const DeviceCurve* expTable()
    {
        if (expCurve == nullptr) { expCurve = std::make_unique<DeviceCurve>(); Net::buildExpTable (*expCurve); }
        return expCurve.get();
    }

    void stampConductance (Node a, Node b, double g)
    {
        net->addConductance (a, a, g); net->addConductance (b, b, g);
        net->addConductance (a, b, -g); net->addConductance (b, a, -g);
    }

    std::unique_ptr<Net> net;
    std::vector<std::unique_ptr<DeviceCurve>> curves;
    std::unique_ptr<DeviceCurve> expCurve;
    std::vector<std::unique_ptr<CompositeDiode>> composites;
    std::vector<std::unique_ptr<BjtDevice>> bjtDevices;
    std::vector<std::unique_ptr<TransferCurve>> transfer;
    struct PendingCurve { TransferCurve* curve; std::function<double (double)> icAt; Node c, e; double vce0, sign; bool saturate; };
    std::vector<PendingCurve> pending;
    std::vector<double> dc;
    std::vector<Node> probeNodes;
    std::vector<int> capMap; // NodalCircuit capacitor handle -> network capacitor (-1: both ends AC ground)
    int inputHandle = 0, secondHandle = -1;
    double inputDc = 0.0, secondDc = 0.0;
};

/** Diagnostics: runs a prepared netlist block and its compiled version side by side on a sine at `amp` (around the
    input source's DC) and returns "rms netlist / rms compiled / rms difference" at the probe. Copies the netlist. */
inline juce::String compareBlock (const NodalCircuit& block, int inputSource, NodalCircuit::Node probe, double inputDc, double amp,
                                  double hz, double sampleRate, const CompileOptions& o = CompileOptions())
{
    NodalCircuit ref = block;
    CompiledCircuit fast;
    if (! fast.compile (block, inputSource, { probe }, sampleRate, o)) return "not compilable";
    const double dcOut = ref.voltage (probe);
    double sa = 0, sb = 0, sd = 0, pa = 0, pb = 0;
    const int n = (int) sampleRate, skip = n / 2;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        const double gate = hz < 0.0 ? (std::fmod (t, 0.2) < 0.1 ? 1.0 : 0.0) : 1.0; // hz < 0: gated bursts at |hz|
        const double x = inputDc + gate * amp * std::sin (2.0 * 3.141592653589793 * std::abs (hz) * i / sampleRate);
        ref.setSource (inputSource, x);
        ref.solveSample();
        fast.setSource (inputSource, x);
        fast.solveSample();
        if (i < skip) continue;
        const double a = ref.voltage (probe) - dcOut, b = fast.voltage (probe) - dcOut;
        sa += a * a; sb += b * b; sd += (a - b) * (a - b);
        pa = std::max (pa, std::abs (a)); pb = std::max (pb, std::abs (b));
    }
    const double k = 1.0 / (n - skip);
    return juce::String (std::sqrt (sa * k), 5) + " / " + juce::String (std::sqrt (sb * k), 5) + " / diff " + juce::String (std::sqrt (sd * k), 5)
         + " pk " + juce::String (pa, 3) + "/" + juce::String (pb, 3);
}

} // namespace openguitarmultifx::pipeline
