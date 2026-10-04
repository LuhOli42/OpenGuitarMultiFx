#pragma once

#include "EbersMollBJT.h"
#include "ShichmanHodgesJFET.h"
#include "TubeModels.h"

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
    static constexpr int maxUnknowns = 48;

    using Node = int;
    static constexpr Node ground = 0;

    /** Capacitor integration: the theta-method. 0.5 is the trapezoidal rule (exact frequency response shape, but it
        does not damp stiff modes -- every diode switch or transistor saturation rings at Nyquist); 1 is backward
        Euler (heavily damped). 0.6 cuts the ringing energy above 12 kHz by ~9 dB (HM-2 clipping op-amp) for a 0.03 dB
        change in the audible-band response; going to 1.0 gains only 4 dB more and costs 1.3 dB at 8 kHz. */
    static inline double defaultTheta = 0.6;
    /** Newton stops when the leftover error, mapped to any node voltage, is below this (V); see solveDevices(). */
    static inline double defaultNodeTolerance = 1.0e-5;
    /** A Newton step whose largest port change is below this (V) is accepted without evaluating the devices again. */
    static inline double smallStepAccept = 5.0e-4;
    /** Deadline mode. When a real-time caller sees that a block is running out of its time budget it switches the remaining samples to bounded work:
        each sample then does at most `maxIterations` Newton iterations from the predicted start (and at most 10 + 20 in the two rescue attempts for a
        singular sample), and if it has not converged the last iterate stands. The cost of a sample is bounded by construction, at the price of
        an inexact sample in a passage that was already too hard to afford. 0 = off, the general solver. */
    void setDeadlineMode (int maxIterations) noexcept { deadlineIterations = maxIterations; }
    bool isDeadlineMode() const noexcept { return deadlineIterations > 0; }
    long long getDeadlineSamples() const noexcept { return deadlineSamples; }
    /** The circuit's memory and nothing else: capacitor and inductor state, the Newton warm-start history, the last excitation. Saving and restoring it
        is a few hundred bytes and touches no parameter, so it does NOT invalidate the reduced model (a full copy of the circuit from a "rest" snapshot
        also restores the resistances the knobs had at prepare(), and re-applying the knobs then rebuilds the model: milliseconds, on the audio thread, in the
        block that is already in trouble). Used to recover from a lost operating point. */
    struct DynamicState
    {
        static constexpr int maxPentodeSlots = 8;
        double capV[32] {}, capI[32] {}, capIeq[32] {};
        double uState[16] {}, uPrev[16] {}, uPrev2[16] {};
        double Elast[48] {}, curLast[16] {};
        double pentodeVgk[maxPentodeSlots] {}, pentodeVpk[maxPentodeSlots] {};
        int satMode = 0, lastMode = 0;
        bool valid = false;
    };
    void saveDynamicState (DynamicState& d) const noexcept
    {
        for (size_t c = 0; c < capacitors.size() && c < 32; ++c)
        {
            d.capV[c] = capacitors[c].vPrev;
            d.capI[c] = capacitors[c].iPrev;
            d.capIeq[c] = capacitors[c].ieq;
        }
        for (int j = 0; j < maxPortsV; ++j)
        {
            d.uState[j] = uState[j];
            d.uPrev[j] = uStatePrev[j];
            d.uPrev2[j] = uStatePrev2[j];
        }
        for (int e = 0; e < maxExcite; ++e)
            d.Elast[e] = Elast[e];
        for (int k = 0; k < maxPortsI; ++k)
            d.curLast[k] = curLast[k];
        for (size_t q = 0; q < pentodes.size() && q < (size_t) DynamicState::maxPentodeSlots; ++q)
        {
            d.pentodeVgk[q] = pentodes[q].lastVgk;
            d.pentodeVpk[q] = pentodes[q].lastVpk;
        }
        d.satMode = satMode;
        d.lastMode = lastMode;
        d.valid = true;
    }
    void restoreDynamicState (const DynamicState& d) noexcept
    {
        if (! d.valid)
            return;
        for (size_t c = 0; c < capacitors.size() && c < 32; ++c)
        {
            capacitors[c].vPrev = d.capV[c];
            capacitors[c].iPrev = d.capI[c];
            capacitors[c].ieq = d.capIeq[c];
        }
        for (int j = 0; j < maxPortsV; ++j)
        {
            uState[j] = d.uState[j];
            uStatePrev[j] = d.uPrev[j];
            uStatePrev2[j] = d.uPrev2[j];
        }
        for (int e = 0; e < maxExcite; ++e)
            Elast[e] = d.Elast[e];
        for (int k = 0; k < maxPortsI; ++k)
            curLast[k] = d.curLast[k];
        for (size_t q = 0; q < pentodes.size() && q < (size_t) DynamicState::maxPentodeSlots; ++q)
        {
            pentodes[q].lastVgk = d.pentodeVgk[q];
            pentodes[q].lastVpk = d.pentodeVpk[q];
        }
        satMode = d.satMode;
        lastMode = d.lastMode;
        consecutiveFailures = 0;
    }
    static inline double portResidualAccept = 30.0; // volts: the largest port-equation error a converged point may carry (garbage is hundreds; a real point is micro-volts)
    void setIntegrationTheta (double newTheta) noexcept { theta = newTheta; }

    /** `tauF` (forward transit time, 1 / 2 pi fT), `cje` and `cjc` (junction capacitances, F) add the transistor's own
        bandwidth: a base-emitter capacitance cje + tauF * gm (gm at the DC operating point) and a base-collector one (the Miller
        capacitor). Zero = the ideal, infinite-bandwidth Ebers-Moll model. A germanium fuzz transistor has fT of a few hundred kHz
        to 1 MHz (a beta corner at 5-15 kHz): leaving that out makes a modelled fuzz far brighter than the real one. */
    struct BjtParams { double Is, Vt, betaF, betaR; double tauF = 0.0, cje = 0.0, cjc = 0.0; };
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

    /** Op-amp with a finite DC gain: V(out) = gain * (V(plus) - V(minus) - offset), still a constraint row (no extra
        unknown, the output stays an ideal source). For the macro-model of a discrete op-amp whose open-loop gain is
        a few hundred, where the ideal one would overstate the closed-loop gain by 40%. Combine with an RC and an
        addSaturatingOpAmp() follower for its dominant pole and its output swing. */
    void addFiniteGainOpAmp (Node inPlus, Node inMinus, Node out, double gain, double offset = 0.0, double commonMode = 0.0)
    {
        opAmps.push_back ({ inPlus, inMinus, out, offset, 1.0 / gain, commonMode });
    }

    /** A real op-amp as a macro-model: finite DC gain, a dominant pole set by the gain-bandwidth product, an output
        resistance and an output swing that saturates against the rails. Built from addFiniteGainOpAmp() -> R*C ->
        addSaturatingOpAmp() follower -> a series resistor into `out`; the block can hold only ONE (the saturating
        follower), as addSaturatingOpAmp() says. The gain-bandwidth product is what sets a hard-driven stage's treble (a
        741 at a gain of 200 has 5 kHz of bandwidth).

        The integrator node (after the R*C) is clamped by a diode to each rail. Without that, a hard-driven stage winds it
        up: the error between the inputs is ~1 V while the output sits on a rail, times an open-loop gain of 200 000, and
        an integrator with a 5 Hz pole takes hundreds of microseconds to unwind when the input reverses -- the clipped
        output came out a quarter of a cycle late (measured: 560 us at 440 Hz). A real op-amp's internal node is limited
        by its own output stage; the two diodes limit it to ~0.6 V beyond the rails. Cost: two Newton ports.

        Not modelled: slew rate (0.5 V/us of a 741, 13 V/us of a TL072 -- ten volts per sample at 48 kHz; the LM308 of
        a RAT, at 0.3 V/us, is the one where it starts to matter), input-pair limiting, offset. */
    struct OpAmpMacro
    {
        double dcGain = 2.0e5;      // open-loop gain at DC
        double gainBandwidth = 1.0e6; // Hz
        double outputOhms = 75.0;
        double lowRail = 1.5, highRail = 7.5; // output swing (V): ~1.5 V short of each rail of a 9 V supply
        double commonMode = 0.0;    // (1+cm) V+ - (1-cm) V- (see addFiniteGainOpAmp)
        double offset = 0.0;        // input-referred offset (V): out = gain * (V+ - V- - offset)
    };

    void addOpAmpMacro (Node inPlus, Node inMinus, Node out, const OpAmpMacro& m)
    {
        const Node amp = addNode(), px = addNode(), ob = addNode(), hi = addNode(), lo = addNode();
        const double pole = m.gainBandwidth / m.dcGain;
        addFiniteGainOpAmp (inPlus, inMinus, amp, m.dcGain, m.offset, m.commonMode);
        addResistor (amp, px, 1.0e6);
        addCapacitor (px, ground, 1.0 / (2.0 * 3.14159265358979323846 * 1.0e6 * pole));
        addSource (hi, m.highRail);
        addSource (lo, m.lowRail);
        addDiode (px, hi, 1.0e-9, 25.85e-3);
        addDiode (lo, px, 1.0e-9, 25.85e-3);
        addSaturatingOpAmp (px, ob, ob, { m.lowRail, m.highRail });
        addResistor (ob, out, m.outputOhms);
    }

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

    /** `tauF` (forward transit time, s): the diode's own diffusion capacitance while it is conducting, Cd = tauF *
        (dId/dVd), on top of a small fixed junction capacitance floor. Zero = the ideal, infinite-bandwidth Shockley
        diode this project used everywhere before 2026-09-27. A small-signal switching diode's Cd is negligible at
        the current a low-gain stage draws, but not at the peak current a hard clipper actually pushes through it --
        see docs/circuits/ClipperRealism.md. Same mechanism as BjtParams::tauF/cje, one home (bjtDynamics's sibling,
        diodeDynamics), so the two device families cannot drift apart. */
    void addDiode (Node anode, Node cathode, double saturationCurrent, double nTimesVt, double tauF = 0.0)
    {
        diodes.push_back ({ anode, cathode, saturationCurrent, nTimesVt });
        if (tauF > 0.0)
            diodeDynamics.push_back ({ (int) diodes.size() - 1, addCapacitor (anode, cathode, 2.0e-12), tauF });
    }

    void addBjt (Node collector, Node base, Node emitter, bool pnp, const BjtParams& p)
    {
        Bjt q { collector, base, emitter, pnp, {} };
        q.model.setParameters (p.Is, p.Vt, p.betaF, p.betaR);
        bjts.push_back (q);
        if (p.tauF > 0.0 || p.cje > 0.0)
            bjtDynamics.push_back ({ (int) bjts.size() - 1, addCapacitor (base, emitter, p.cje + 1.0e-12), p.tauF, p.cje });
        if (p.cjc > 0.0)
            addCapacitor (base, collector, p.cjc);
    }

    /** A transistor as a ONE-port device (vbe only) whose collector current saturates smoothly at `saturationCurrent`
        (the load line's limit, (rail - Vce(sat)) / (collector + emitter resistance)): the collector junction, which
        would take a second Newton port, only ever matters as the point where a stage runs out of collector current, and
        for a stage with a resistive load that point is known. Base current stays iC/beta of the UNSATURATED current
        (no extra base current when the real transistor saturates); check that against the full model where it is used. */
    void addBjtSaturating (Node collector, Node base, Node emitter, bool pnp, const BjtParams& p, double saturationCurrent)
    {
        Bjt q { collector, base, emitter, pnp, {}, saturationCurrent };
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


    /** Triode (Koren plate current + Dempwolf grid current): two ports, vgk and vpk. */
    void addTriode (Node plate, Node grid, Node cathode, const KorenTriode::Parameters& p = {})
    {
        Triode t { plate, grid, cathode, {} };
        t.model.setParameters (p);
        triodes.push_back (t);
    }

    /** Beam tetrode / pentode with the screen NOT a circuit node: `screenVolts` is supplied by the caller and can be
        changed per sample with setPentodeScreen() (supply sag, the drop across the screen resistor). Two ports, vgk
        and vpk. Returns a handle. */
    int addPentode (Node plate, Node grid, Node cathode, const KorenPentode::Parameters& p, double screenVolts)
    {
        Pentode t { plate, grid, cathode, {}, screenVolts, 0.0 };
        t.model.setParameters (p);
        t.vg2Pow = t.model.screenFactor (screenVolts);
        pentodes.push_back (t);
        return (int) pentodes.size() - 1;
    }

    void setPentodeScreen (int handle, double volts) noexcept
    {
        auto& t = pentodes[(size_t) handle];
        t.vg2 = volts;
        t.vg2Pow = t.model.screenFactor (volts);
    }

    /** Plate and screen current of a pentode at the last solved sample, from the converged port currents (cheap: no
        model evaluation beyond one arctan). */
    void pentodeCurrents (int handle, double& ip, double& ig2) const noexcept
    {
        const auto& t = pentodes[(size_t) handle];
        const auto& q = t.model.parameters();
        ip = curLast[t.portI0];
        const double f = 1.0 + q.lambda * (t.lastVpk - q.vRef);
        const double at = std::atan (t.lastVpk / q.kvb);
        ig2 = at * f > 1.0e-6 ? ip * q.kg1 / (2.0 * q.kg2 * at * f) : 0.0;
    }

    /** Plate current of a pentode at the last solved operating point. */
    double pentodePlateCurrent (int handle) const noexcept
    {
        const auto& t = pentodes[(size_t) handle];
        return t.model.evaluate (t.lastVgk, t.lastVpk, t.vg2).ip;
    }

    /** Screen current of a pentode at the last solved operating point. */
    double pentodeScreenCurrent (int handle) const noexcept
    {
        const auto& t = pentodes[(size_t) handle];
        return t.model.evaluate (t.lastVgk, 100.0, t.vg2).ig2;
    }

    /** Group of magnetically coupled windings. `windings[i]` is the node pair (a, b) winding i sits between (current
        flows a -> b); `inductance` is the row-major n x n matrix of self (diagonal) and mutual inductances in henries.
        The winding resistances are separate resistors. Each winding is one entry of the capacitors' folded state.
        Returns the index of the first winding's state (so the currents can be read with windingCurrent()). */
    int addCoupledInductors (const std::vector<std::pair<Node, Node>>& windings, const std::vector<double>& inductance)
    {
        const int n = (int) windings.size();
        InductorGroup grp;
        grp.first = (int) capacitors.size();
        grp.n = n;
        grp.inverse = invertMatrix (inductance, n);
        for (int i = 0; i < n; ++i)
        {
            Cap c { windings[(size_t) i].first, windings[(size_t) i].second, 0.0, 0.0, 0.0, 0.0, 0.0, -1, -1 };
            c.group = (int) inductorGroups.size();
            capacitors.push_back (c);
        }
        inductorGroups.push_back (grp);
        return grp.first;
    }

    /** Changes a capacitor's value while running (no allocation). The stored charge is kept as a voltage, so a change
        while signal is flowing clicks a little: meant for rare switch-like controls (a speaker selector). */
    void setCapacitance (int handle, double farads) noexcept
    {
        auto& c = capacitors[(size_t) handle];
        c.farads = farads;
        c.g = farads * sampleRate / theta;
        matrixDirty = true;
        modelsDirty = true;
    }

    /** The inverse of an inductance matrix, for setInductorInverse() -- computed off the audio thread. */
    static std::vector<double> inverseInductance (const std::vector<double>& inductance, int n) { return invertMatrix (inductance, n); }

    /** Replaces a coupled-inductor group's inverse inductance (same size as when it was added); allocation-free. */
    void setInductorInverse (int firstState, const std::vector<double>& inverse) noexcept
    {
        for (auto& grp : inductorGroups)
            if (grp.first == firstState && grp.inverse.size() == inverse.size())
            {
                std::copy (inverse.begin(), inverse.end(), grp.inverse.begin());
                matrixDirty = true;
                modelsDirty = true;
                return;
            }
    }

    /** Current through winding `state` (as returned by addCoupledInductors, plus the winding's position). */
    double windingCurrent (int state) const noexcept { return capacitors[(size_t) state].iPrev; }

    /** A source that injects `amps` into `node` (a load draws a negative value); the node stays an unknown.
        Returns a handle for setCurrentSource(). */
    int addCurrentSource (Node node, double amps = 0.0)
    {
        sources.push_back ({ node, amps, true });
        return (int) sources.size() - 1;
    }
    void setCurrentSource (int handle, double amps) noexcept { sources[(size_t) handle].volts = amps; }

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
            if (s.current)
                continue;
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
        if ((int) (diodes.size() + 2 * bjts.size() + 2 * jfets.size() + 2 * triodes.size() + 2 * pentodes.size()) > maxPortsV)
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
                opTerms.push_back ({ r, o.plus, o.minus, (int) oi == satOpIndex, o.offset, o.cm });
        }
        satMode = 0;
        if ((int) capacitors.size() > maxCaps || (int) (capacitors.size() + sources.size()) + 1 > maxExcite)
            return false; // too many capacitors/sources for the folded model
        models.assign (satOpIndex >= 0 ? 3 : 1, Model {});
        modelsDirty = true;
        useX = true;
        sourceOfNode.assign ((size_t) nodeCount + 1, -1);
        for (size_t h = 0; h < sources.size(); ++h)
            if (! sources[h].current)
                sourceOfNode[(size_t) sources[h].node] = (int) h;

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

        // The transistors' base-emitter capacitances follow their DC gm (diffusion capacitance = tauF * gm).
        for (const auto& d : bjtDynamics)
        {
            const auto& q = bjts[(size_t) d.bjt];
            const double sign = q.pnp ? -1.0 : 1.0;
            const auto op = q.model.evaluate (sign * voltage (q.b), sign * voltage (q.e), sign * voltage (q.c));
            capacitors[(size_t) d.capBe].farads = d.cje + d.tauF * std::abs (op.diC_dvbe);
        }

        // Same mechanism, for diodes: Cd = tauF * dId/dVd = tauF * Is/nVt * exp(Vd/nVt) while forward biased.
        for (const auto& d : diodeDynamics)
        {
            const auto& dio = diodes[(size_t) d.diode];
            const double vd = voltage (dio.a) - voltage (dio.k);
            const double gd = dio.Is / dio.nVt * std::exp (std::clamp (vd / dio.nVt, -40.0, 40.0));
            capacitors[(size_t) d.cap].farads = 2.0e-12 + d.tauF * gd;
        }

        for (auto& c : capacitors)
        {
            c.g = c.farads * sampleRate / theta;
            c.vPrev = voltage (c.a) - voltage (c.b);
            if (c.group < 0)
                c.iPrev = 0.0;
        }
        beDt = 0.0;
        matrixDirty = true;
        satMode = 0;
        modelsDirty = true;
        useX = true;
        xPrev = x;

        return ok;
    }

    // ---- Run time (allocation-free) ----

    void setSource (int handle, double volts) noexcept
    {
        sources[(size_t) handle].volts = volts;
        const auto n = (size_t) sources[(size_t) handle].node;
        if (n < knownVoltage.size() && ! sources[(size_t) handle].current) // before prepare() only the stored value matters
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
        modelsDirty = true;
    }

    /** Advances one sample. Returns false if Newton failed to converge (the previous sample's values are then
        held for this sample).

        This is the DK method proper. Everything linear in the circuit is folded, once per change of a resistance,
        into dense maps from an "excitation" vector E = [capacitor history currents, source voltages, 1] to what
        the sample needs: the no-device port voltages u0 = Hu E, the capacitor voltages, and (on demand) any node
        voltage. Per sample that is a few small matrix-vector products, Newton over the device ports, and one more
        product to advance the capacitors -- no per-sample assembly, factorisation or back-substitution over the
        whole node set. */
    bool solveSample() noexcept
    {
        if (modelsDirty)
            invalidateModels();

        const int nv = (int) voltagePorts.size();
        const int ni = (int) currentPorts.size();
        const double ieqHistoryWeight = (1.0 - theta) / theta;
        const int ns = (int) capacitors.size(), nsrc = (int) sources.size();
        const int m = ns + nsrc + 1;

        double E[maxExcite];
        for (int c = 0; c < ns; ++c)
        {
            auto& cap = capacitors[(size_t) c];
            cap.ieq = cap.g * cap.vPrev + ieqHistoryWeight * cap.iPrev;
            E[c] = cap.ieq;
        }
        for (const auto& grp : inductorGroups)
            for (int i = 0; i < grp.n; ++i)
            {
                double hist = capacitors[(size_t) (grp.first + i)].iPrev;
                for (int j = 0; j < grp.n; ++j)
                    hist += ieqHistoryWeight * inductorConductance (grp, i, j) * capacitors[(size_t) (grp.first + j)].vPrev;
                capacitors[(size_t) (grp.first + i)].ieq = -hist;
                E[grp.first + i] = -hist;
            }
        for (int h = 0; h < nsrc; ++h)
            E[ns + h] = sources[(size_t) h].volts;
        E[m - 1] = 1.0;

        double cur[maxPortsI] = {};
        bool ok = false;

        // A saturating op-amp can need up to a few passes (ideal -> held at a rail -> back); everything else, one.
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            Model& M = ensureModel (satMode);
            if (! M.valid)
                break;

            double u0[maxPortsV];
            for (int j = 0; j < nv; ++j)
            {
                double v = 0.0;
                for (int e = 0; e < m; ++e)
                    v += M.Hu[j][e] * E[e];
                u0[j] = v;
            }

            ok = newtonPorts (u0, M.W, M.K, cur);
            if (! ok || satOpIndex < 0)
                break;

            const int wanted = wantedSatMode (M, E, cur);
            if (wanted == satMode)
                break;
            satMode = wanted;
        }

        if (! ok)
            return false;

        Model& M = models[(size_t) satMode];
        for (int c = 0; c < ns; ++c)
        {
            auto& cap = capacitors[(size_t) c];
            double v = 0.0;
            for (int e = 0; e < m; ++e)
                v += M.Hv[c][e] * E[e];
            for (int k = 0; k < ni; ++k)
                v -= M.Kv[c][k] * cur[k];
            cap.iPrev = cap.g * v - cap.ieq;
            cap.vPrev = v;
        }
        for (const auto& grp : inductorGroups)
            for (int i = 0; i < grp.n; ++i)
            {
                double amps = -capacitors[(size_t) (grp.first + i)].ieq;
                for (int j = 0; j < grp.n; ++j)
                    amps += inductorConductance (grp, i, j) * capacitors[(size_t) (grp.first + j)].vPrev;
                capacitors[(size_t) (grp.first + i)].iPrev = amps;
            }

        for (const auto& dm : deviceMaps)
            if (dm.kind == 8)
                {
                pentodes[(size_t) dm.index].lastVgk = uState[dm.v0];
                pentodes[(size_t) dm.index].lastVpk = uState[dm.v0 + 1];
            }
        for (int e = 0; e < m; ++e)
            Elast[e] = E[e];
        for (int k = 0; k < ni; ++k)
            curLast[k] = cur[k];
        lastMode = satMode;
        useX = false;
        return true;
    }

    double voltage (Node node) const noexcept
    {
        if (node <= 0)
            return 0.0;
        const int i = indexOf[(size_t) node];
        if (i < 0)
            return knownVoltage[(size_t) node];
        if (useX)
            return x[(size_t) i];
        return nodeVoltageFrom (node, models[(size_t) lastMode], Elast, curLast);
    }

    /** The current a fixed-voltage node (rail/input) delivers into the
        circuit -- useful to read supply current. Not needed by pedals. */
    int unknowns() const noexcept { return unknownCount; }
    int nodes() const noexcept { return nodeCount; }

    /** Newton iterations per solved sample so far (diagnostic). */
    double averageIterations() const noexcept { return samplesSolved > 0 ? (double) iterationsTotal / (double) samplesSolved : 0.0; }
    /** Iterations the last solveSample() actually took (diagnostic: finding where a rare expensive sample's cost really goes). */
    int lastIterations() const noexcept { return iterationsThisSolve; }

private:
    struct Res { Node a, b; double g; };
    struct Cap { Node a, b; double farads, g, vPrev, iPrev, ieq; int ia = -1, ib = -1; int group = -1; };
    struct InductorGroup { int first = 0, n = 0; std::vector<double> inverse; }; // inverse inductance matrix, row-major
    struct Src { Node node; double volts; bool current = false; };
    struct Op { Node plus, minus, out; double offset; double invGain = 0.0, cm = 0.0; }; // (1+cm) V(plus) - (1-cm) V(minus) - invGain V(out) = offset
    struct Dio { Node a, k; double Is, nVt; };
    struct Bjt { Node c, b, e; bool pnp; EbersMollBJT model; double iSat = 0.0; };
    struct Jfet { Node d, g, s; ShichmanHodgesJFET model; double assumedVds = 0.0; };
    struct Triode { Node p, g, k; KorenTriode model; };
    struct Pentode { Node p, g, k; KorenPentode model; double vg2 = 0.0, vg2Pow = 0.0; double lastVgk = 0.0, lastVpk = 0.0; int portI0 = 0; };

    // -- element storage --
    std::vector<Res> resistors;
    std::vector<Cap> capacitors;
    std::vector<Src> sources;
    std::vector<Op> opAmps;
    std::vector<Dio> diodes;
    struct BjtDynamics { int bjt; int capBe; double tauF, cje; };
    std::vector<BjtDynamics> bjtDynamics;
    struct DiodeDynamics { int diode; int cap; double tauF; };
    std::vector<DiodeDynamics> diodeDynamics;
    std::vector<Bjt> bjts;
    std::vector<Jfet> jfets;
    std::vector<Triode> triodes;
    std::vector<Pentode> pentodes;
    std::vector<InductorGroup> inductorGroups;
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
    int iterationsThisSolve = 0;
    int deadlineIterations = 0;                  // see setDeadlineMode()
    long long deadlineSamples = 0;               // samples solved in deadline mode
    int consecutiveFailures = 0;                 // see newtonPorts(): stops the fallback modes burning time in a runaway
    static constexpr int failureStreakBeforeGivingUp = 2;
    double theta = defaultTheta;
    double beDt = 0.0; // > 0 only during the pseudo-transient DC relaxation (backward Euler with this step)

    struct KnownTerm { int row; Node knownNode; double g; };
    std::vector<KnownTerm> knownTerms; // g * V(known) added to rhs rows, for elements touching a rail/input
    struct OpTerm { int row; Node plus, minus; bool saturating; double offset; double cm = 0.0; };
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
    int satOpIndex = -1;
    OpAmpSpec satSpec;
    int satMode = 0;

    static constexpr int maxPortsV = 16;  // independent branch voltages across all devices
    static constexpr int maxPortsI = 16;  // independent device currents across all devices
    // -- the folded-linear (DK state-space) model, one per saturating-op-amp mode
    static constexpr int maxCaps = 32;
    static constexpr int maxExcite = 48; // capacitors + sources + 1
    struct Model
    {
        double Hx[maxUnknowns][maxExcite];   // node voltages      = Hx E - W^T cur
        double Hu[maxPortsV][maxExcite];     // no-device port voltages u0 = Hu E
        double Hv[maxCaps][maxExcite];       // capacitor voltages = Hv E - Kv cur
        double Kv[maxCaps][maxPortsI];
        double W[maxPortsI][maxUnknowns];    // A^-1 e_k, per current port
        double K[maxPortsV][maxPortsI];      // port coupling
        bool valid = false;
    };
    std::vector<Model> models;               // 1 entry, or 3 with a saturating op-amp
    bool modelsDirty = true;
    bool useX = true;                        // true until the first fast sample: voltage() then reads the DC solution `x`
    double Elast[maxExcite] = {}, curLast[maxPortsI] = {};
    int lastMode = 0;
    std::vector<int> sourceOfNode;           // node -> index into `sources`, or -1
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
    double uStatePrev2[maxPortsV] {};          // and the one before that (quadratic predictor for the tube ports)
    double nodeTolerance = defaultNodeTolerance;             // Newton stops when the leftover node error is below this (V)
    static inline double predictWeight = 1.0;
    static inline bool predictorEnabled = true;
    struct JunctionInfo { bool isJunction; double vt, vcrit; bool pair = false; double vt2 = 0.0, vcrit2 = 0.0; }; // pair: antiparallel diodes, forward limits on both sides
    JunctionInfo junction[maxPortsV] {};       // per voltage port: p-n junction limiting data (SPICE pnjlim)
    double portLimit[maxPortsV] {};            // largest Newton step (V) a port may take in one iteration (1 V; a tube's plate 40 V)
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
        return o.offset - ((1.0 + o.cm) * kp - (1.0 - o.cm) * km);
    }

    void constraintRhs (const Op& o, double* rhs) const noexcept
    {
        const int r = indexOf[(size_t) o.out];
        if (r < 0)
            return;
        const double kp = (o.plus > 0 && indexOf[(size_t) o.plus] < 0) ? knownVoltage[(size_t) o.plus] : 0.0;
        const double km = (o.minus > 0 && indexOf[(size_t) o.minus] < 0) ? knownVoltage[(size_t) o.minus] : 0.0;
        rhs[r] = o.offset - ((1.0 + o.cm) * kp - (1.0 - o.cm) * km);
    }


    // ---------------------------------------------------------------- inductors

    /** Conductance-like coefficient of the trapezoidal (or, during the DC relaxation, backward-Euler) companion model
        of a coupled-inductor group: i = G v + history, G = h * L^-1 with h = theta / fs (or the BE step). */
    /** During the DC solution a winding is a wire: a stiff conductance whose current is the winding's DC current. */
    static constexpr double inductorShortG = 1.0e4;

    double inductorConductance (const InductorGroup& grp, int i, int j) const noexcept
    {
        const double h = beDt > 0.0 ? beDt : theta / sampleRate;
        return h * grp.inverse[(size_t) (i * grp.n + j)];
    }

    static std::vector<double> invertMatrix (const std::vector<double>& m, int n)
    {
        std::vector<double> a = m, inv ((size_t) n * (size_t) n, 0.0);
        for (int i = 0; i < n; ++i)
            inv[(size_t) (i * n + i)] = 1.0;
        for (int col = 0; col < n; ++col)
        {
            int best = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (a[(size_t) (r * n + col)]) > std::abs (a[(size_t) (best * n + col)]))
                    best = r;
            for (int c = 0; c < n; ++c)
            {
                std::swap (a[(size_t) (col * n + c)], a[(size_t) (best * n + c)]);
                std::swap (inv[(size_t) (col * n + c)], inv[(size_t) (best * n + c)]);
            }
            const double d = 1.0 / a[(size_t) (col * n + col)];
            for (int c = 0; c < n; ++c)
            {
                a[(size_t) (col * n + c)] *= d;
                inv[(size_t) (col * n + c)] *= d;
            }
            for (int r = 0; r < n; ++r)
            {
                if (r == col)
                    continue;
                const double f = a[(size_t) (r * n + col)];
                for (int c = 0; c < n; ++c)
                {
                    a[(size_t) (r * n + c)] -= f * a[(size_t) (col * n + c)];
                    inv[(size_t) (r * n + c)] -= f * inv[(size_t) (col * n + c)];
                }
            }
        }
        return inv;
    }

    void stampInductors (bool includeCaps) noexcept
    {
        double dummy[maxUnknowns] {};
        for (const auto& grp : inductorGroups)
            for (int i = 0; i < grp.n; ++i)
            {
                const auto& wi = capacitors[(size_t) (grp.first + i)];
                if (! includeCaps || beDt > 0.0)
                {
                    stampConductance (wi.a, wi.b, inductorShortG, linearMatrix); // DC: a winding is a wire
                    continue;
                }
                for (int j = 0; j < grp.n; ++j)
                {
                    const auto& wj = capacitors[(size_t) (grp.first + j)];
                    const double g = inductorConductance (grp, i, j);
                    addCoeff (wi.a, wj.a, g, linearMatrix, dummy);
                    addCoeff (wi.a, wj.b, -g, linearMatrix, dummy);
                    addCoeff (wi.b, wj.a, -g, linearMatrix, dummy);
                    addCoeff (wi.b, wj.b, g, linearMatrix, dummy);
                }
            }
    }

    void registerInductorKnownTerms (bool includeCaps)
    {
        auto add = [this] (Node row, Node col, double coeff)
        {
            const int r = row <= 0 ? -1 : indexOf[(size_t) row];
            if (r < 0 || isOpAmpRow[(size_t) r])
                return;
            if (col > 0 && indexOf[(size_t) col] < 0)
                knownTerms.push_back ({ r, col, -coeff });
        };
        for (const auto& grp : inductorGroups)
            for (int i = 0; i < grp.n; ++i)
            {
                const auto& wi = capacitors[(size_t) (grp.first + i)];
                if (! includeCaps || beDt > 0.0)
                {
                    add (wi.a, wi.b, -inductorShortG);
                    add (wi.b, wi.a, -inductorShortG);
                    continue;
                }
                for (int j = 0; j < grp.n; ++j)
                {
                    const auto& wj = capacitors[(size_t) (grp.first + j)];
                    const double g = inductorConductance (grp, i, j);
                    add (wi.a, wj.a, g);
                    add (wi.a, wj.b, -g);
                    add (wi.b, wj.a, -g);
                    add (wi.b, wj.b, g);
                }
            }
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
                if (c.group < 0)
                    stampConductance (c.a, c.b, c.g, linearMatrix);
        stampInductors (includeCaps);

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
            if (ip >= 0) linearMatrix[r][ip] += 1.0 + o.cm;
            if (im >= 0) linearMatrix[r][im] -= 1.0 - o.cm;
            linearMatrix[r][r] -= o.invGain;
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
                if (c.group < 0)
                    addKnown (c.a, c.b, c.g);
        registerInductorKnownTerms (includeCaps);

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

        // Tubes (terminal order p, g, k; the cathode current is the sum of plate and grid currents).
        auto stampTube = [&] (Node p, Node g, Node k, double ip, double dip_dvgk, double dip_dvpk, double ig, double dig_dvgk)
        {
            const double vp = vAt (p, xv), vg = vAt (g, xv), vk = vAt (k, xv);
            // d/dvp = d/dvpk, d/dvg = d/dvgk, d/dvk = -(both)
            const double J[3][3] = {
                { dip_dvpk, dip_dvgk, -dip_dvpk - dip_dvgk },
                { 0.0, dig_dvgk, -dig_dvgk },
                { -dip_dvpk, -dip_dvgk - dig_dvgk, dip_dvpk + dip_dvgk + dig_dvgk }
            };
            const double i0[3] = { ip, ig, -ip - ig };
            const Node nodes[3] = { p, g, k };
            const double v0[3] = { vp, vg, vk };
            stampDevice<3> (nodes, i0, J, v0);
        };
        for (auto& t : triodes)
        {
            const auto op = t.model.evaluate (vAt (t.g, xv) - vAt (t.k, xv), vAt (t.p, xv) - vAt (t.k, xv));
            stampTube (t.p, t.g, t.k, op.ip, op.dip_dvgk, op.dip_dvpk, op.ig, op.dig_dvgk);
        }
        for (auto& t : pentodes)
        {
            const auto op = t.model.evaluate (vAt (t.g, xv) - vAt (t.k, xv), vAt (t.p, xv) - vAt (t.k, xv), t.vg2);
            stampTube (t.p, t.g, t.k, op.ip, op.dip_dvgk, op.dip_dvpk, op.ig, op.dig_dvgk);
        }
    }

    bool hasNonlinear() const noexcept { return ! diodes.empty() || ! bjts.empty() || ! jfets.empty() || ! triodes.empty() || ! pentodes.empty(); }

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

    /** Which state the saturating op-amp should be in given the solution just computed in `satMode`: ideal is right
        while its output is inside the rails; held at a rail is right while (+) - (-) keeps pushing into it. */
    int wantedSatMode (const Model& M, const double* E, const double* cur) const noexcept
    {
        const auto& o = opAmps[(size_t) satOpIndex];
        const double vout = nodeVoltageFrom (o.out, M, E, cur);
        const double vd = nodeVoltageFrom (o.plus, M, E, cur) - nodeVoltageFrom (o.minus, M, E, cur);
        if (satMode == 0)
            return vout > satSpec.highRail ? 1 : (vout < satSpec.lowRail ? 2 : 0);
        if (satMode == 1)
            return vd < 0.0 ? 0 : 1;
        return vd > 0.0 ? 0 : 2;
    }

    void invalidateModels() noexcept
    {
        for (auto& mo : models)
            mo.valid = false;
        modelsDirty = false;
    }

    Model& ensureModel (int mode) noexcept
    {
        Model& mo = models[(size_t) mode];
        if (! mo.valid)
            buildModel (mode);
        return mo;
    }

    double nodeVoltageFrom (Node node, const Model& mo, const double* E, const double* cur) const noexcept
    {
        if (node <= 0)
            return 0.0;
        const int i = indexOf[(size_t) node];
        if (i < 0)
            return knownVoltage[(size_t) node];
        const int m = (int) capacitors.size() + (int) sources.size() + 1;
        double v = 0.0;
        for (int e = 0; e < m; ++e)
            v += mo.Hx[i][e] * E[e];
        const int ni = (int) currentPorts.size();
        for (int k = 0; k < ni; ++k)
            v -= mo.W[k][i] * cur[k];
        return v;
    }

    /** Folds the linear network (in op-amp state `mode`) into the dense maps of a Model. Runs when a resistance
        changed, so a moving knob costs about one LU plus one back-substitution per excitation every 16 samples. */
    void buildModel (int mode) noexcept
    {
        const int savedMode = satMode;
        satMode = mode;
        rebuildLinearMatrix (true);
        luValid = false;
        factorAndBuildReducedModel();
        satMode = savedMode;
        matrixDirty = true; // the members above now belong to `mode`; the DC path rebuilds what it needs

        Model& mo = models[(size_t) mode];
        mo.valid = false;
        if (! luValid)
            return;

        const int n = unknownCount;
        const int ns = (int) capacitors.size(), nsrc = (int) sources.size();
        const int m = ns + nsrc + 1;
        const int ni = (int) currentPorts.size(), nv = (int) voltagePorts.size();

        for (int e = 0; e < m; ++e)
        {
            double col[maxUnknowns] = {};
            if (e < ns)
            {
                const auto& c = capacitors[(size_t) e];
                if (c.ia >= 0) col[c.ia] += 1.0;
                if (c.ib >= 0) col[c.ib] -= 1.0;
            }
            else if (e < ns + nsrc)
            {
                const Node sn = sources[(size_t) (e - ns)].node;
                if (sources[(size_t) (e - ns)].current)
                {
                    const int r = indexOf[(size_t) sn];
                    if (r >= 0 && ! isOpAmpRow[(size_t) r])
                        col[r] += 1.0;
                    luBackSubstitute (col);
                    for (int i = 0; i < n; ++i)
                        mo.Hx[i][e] = col[i];
                    continue;
                }
                for (const auto& t : knownTerms)
                    if (t.knownNode == sn)
                        col[t.row] += t.g;
                for (const auto& o : opTerms)
                {
                    if (o.saturating && mode != 0)
                        continue; // held at a rail: the row no longer refers to its inputs
                    if (o.plus == sn && indexOf[(size_t) o.plus] < 0)
                        col[o.row] -= 1.0 + o.cm;
                    if (o.minus == sn && indexOf[(size_t) o.minus] < 0)
                        col[o.row] += 1.0 - o.cm;
                }
            }
            else
            {
                for (const auto& o : opTerms)
                    col[o.row] += (o.saturating && mode == 1) ? satSpec.highRail
                                : (o.saturating && mode == 2) ? satSpec.lowRail
                                                              : o.offset;
            }
            luBackSubstitute (col);
            for (int i = 0; i < n; ++i)
                mo.Hx[i][e] = col[i];
        }

        std::memcpy (mo.W, W, sizeof W);
        std::memcpy (mo.K, K, sizeof K);

        // rowE/rowW of "voltage of a node" as a linear function of E and of the device currents.
        auto accumulate = [&] (Node node, double sign, double* rowE, double* rowW)
        {
            if (node <= 0)
                return;
            const int i = indexOf[(size_t) node];
            if (i >= 0)
            {
                for (int e = 0; e < m; ++e)
                    rowE[e] += sign * mo.Hx[i][e];
                if (rowW != nullptr)
                    for (int k = 0; k < ni; ++k)
                        rowW[k] += sign * W[k][i];
            }
            else if (sourceOfNode[(size_t) node] >= 0)
                rowE[ns + sourceOfNode[(size_t) node]] += sign;
        };

        for (int j = 0; j < nv; ++j)
        {
            double rowE[maxExcite] = {};
            accumulate (voltagePorts[(size_t) j].plus, 1.0, rowE, nullptr);
            accumulate (voltagePorts[(size_t) j].minus, -1.0, rowE, nullptr);
            for (int e = 0; e < m; ++e)
                mo.Hu[j][e] = rowE[e];
        }
        for (int c = 0; c < ns; ++c)
        {
            double rowE[maxExcite] = {}, rowW[maxPortsI] = {};
            accumulate (capacitors[(size_t) c].a, 1.0, rowE, rowW);
            accumulate (capacitors[(size_t) c].b, -1.0, rowE, rowW);
            for (int e = 0; e < m; ++e)
                mo.Hv[c][e] = rowE[e];
            for (int k = 0; k < ni; ++k)
                mo.Kv[c][k] = rowW[k];
        }
        mo.valid = true;
    }

    void buildPorts()
    {
        currentPorts.clear();
        voltagePorts.clear();
        deviceMaps.clear();
        for (auto& j : junction)
            j = { false, 0.0, 0.0 };
        for (auto& l : portLimit)
            l = 1.0;
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
            const bool collectorFixed = q.iSat > 0.0 || q.c <= 0 || indexOf[(size_t) q.c] < 0;
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

        // Tubes: ports vgk and vpk, currents ip (plate -> cathode) and ig (grid -> cathode). The plate port swings
        // by tens of volts per sample in a power stage, so its Newton step may be far larger than a junction's.
        auto addTube = [this] (int kind, size_t index, Node p, Node g, Node k)
        {
            if (kind == 8)
                pentodes[index].portI0 = (int) currentPorts.size();
            deviceMaps.push_back ({ kind, (int) voltagePorts.size(), (int) currentPorts.size(), (int) index, 2, 2 });
            const size_t vp = voltagePorts.size();
            voltagePorts.push_back ({ g, k });
            voltagePorts.push_back ({ p, k });
            if (vp + 1 < (size_t) maxPortsV)
            {
                portLimit[vp] = 10.0; // grid current is a power law above the knee, not an exponential: no overshoot to fear
                portLimit[vp + 1] = 40.0;
            }
            currentPorts.push_back ({ p, k, 1.0 });
            currentPorts.push_back ({ g, k, 1.0 });
        };
        for (size_t i = 0; i < triodes.size(); ++i)
            addTube (7, i, triodes[i].p, triodes[i].g, triodes[i].k);
        for (size_t i = 0; i < pentodes.size(); ++i)
            addTube (8, i, pentodes[i].p, pentodes[i].g, pentodes[i].k);

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
    /** Solves J delta = f for the small dense Newton system (last column of J is f), partial pivoting, one
        reciprocal per pivot. Compile-time sizes 1..8 let the compiler unroll everything; larger systems fall
        back to the same code with a run-time size. */
    template <int N>
    static bool solveFixed (double (&J)[maxPortsV][maxPortsV + 1], double* delta) noexcept
    {
        double invPivot[N];
        for (int col = 0; col < N; ++col)
        {
            int best = col;
            double bestAbs = std::abs (J[col][col]);
            for (int r = col + 1; r < N; ++r)
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
                for (int c = col; c <= N; ++c)
                    std::swap (J[col][c], J[best][c]);
            const double inv = 1.0 / J[col][col];
            invPivot[col] = inv;
            for (int r = col + 1; r < N; ++r)
            {
                const double fct = J[r][col] * inv;
                for (int c = col + 1; c <= N; ++c)
                    J[r][c] -= fct * J[col][c];
            }
        }
        for (int r = N - 1; r >= 0; --r)
        {
            double sum = J[r][N];
            for (int c = r + 1; c < N; ++c)
                sum -= J[r][c] * delta[c];
            delta[r] = sum * invPivot[r];
        }
        return true;
    }

    static bool solveSmall (double (&J)[maxPortsV][maxPortsV + 1], int n, double* delta) noexcept
    {
        switch (n)
        {
            case 1: return solveFixed<1> (J, delta);
            case 2: return solveFixed<2> (J, delta);
            case 3: return solveFixed<3> (J, delta);
            case 4: return solveFixed<4> (J, delta);
            case 5: return solveFixed<5> (J, delta);
            case 6: return solveFixed<6> (J, delta);
            case 7: return solveFixed<7> (J, delta);
            case 8: return solveFixed<8> (J, delta);
            default: break;
        }
        double invPivot[maxPortsV];
        for (int col = 0; col < n; ++col)
        {
            int best = col;
            double bestAbs = std::abs (J[col][col]);
            for (int r = col + 1; r < n; ++r)
            {
                const double a = std::abs (J[r][col]);
                if (a > bestAbs) { bestAbs = a; best = r; }
            }
            if (bestAbs < 1.0e-30)
                return false;
            if (best != col)
                for (int c = col; c <= n; ++c)
                    std::swap (J[col][c], J[best][c]);
            const double inv = 1.0 / J[col][col];
            invPivot[col] = inv;
            for (int r = col + 1; r < n; ++r)
            {
                const double fct = J[r][col] * inv;
                for (int c = col + 1; c <= n; ++c)
                    J[r][c] -= fct * J[col][c];
            }
        }
        for (int r = n - 1; r >= 0; --r)
        {
            double sum = J[r][n];
            for (int c = r + 1; c < n; ++c)
                sum -= J[r][c] * delta[c];
            delta[r] = sum * invPivot[r];
        }
        return true;
    }

    /** Newton-Raphson over the device port voltages. `u0` is the no-device response of the ports; `Wm`/`Km` are the
        node-response and port-coupling matrices of the active mode. Leaves the converged currents in `curOut` (the
        node vector is x = x_linear - W^T cur) and the port voltages in `uState`. */
    /** Newton over the ports, with fallbacks: the predicted start first (cheap, almost always right), then, if that
        fails, the last converged point without extrapolation, then that with small steps. A hard-driven tube stage
        can make the extrapolation overshoot into a region the iteration cannot come back from. */
    bool newtonPorts (const double* u0, const double (*Wm)[maxUnknowns], const double (*Km)[maxPortsI], double* curOut) noexcept
    {
        if (deadlineIterations > 0)
        {
            ++deadlineSamples;
            if (newtonPortsFrom (u0, Wm, Km, curOut, 0) || newtonPortsFrom (u0, Wm, Km, curOut, 1) || newtonPortsFrom (u0, Wm, Km, curOut, 2))
                return true;
            return false;
        }
        if (newtonPortsFrom (u0, Wm, Km, curOut, 0))
        {
            consecutiveFailures = 0;
            return true;
        }

        ++consecutiveFailures;
        const bool streakRunaway = consecutiveFailures > failureStreakBeforeGivingUp;

        // Mode 1 (retry from the last converged point, full-size steps) only rescues an OCCASIONAL sample whose
        // predicted start was bad. Once several samples in a row have failed it rescues nothing -- it just burns 100
        // more iterations per sample, on the audio thread, exactly when the circuit is being driven hardest. Measured
        // on a distortion pedal at full gain into the Bassman: a burst of ~48 failing samples cost 13 ms in one 2.67 ms
        // block (a dropout, and a CPU meter reading ~500%). Skipped once the failures are consecutive.
        if (! streakRunaway && newtonPortsFrom (u0, Wm, Km, curOut, 1))
        {
            consecutiveFailures = 0;
            return true;
        }

        // Mode 2 (last resort, quarter steps) is tried EVEN IN A STREAK, unlike mode 1 above: its own iteration budget
        // is now evidence-gated (see the stall check inside newtonPortsFrom), so a genuinely stuck sample still bails
        // out in about the same time as before, but a sample that is merely FAR from its last converged state -- which
        // is exactly what a sustained streak under extreme, sustained drive is -- gets the room it actually needs to
        // finish the walk instead of being refused a try at all. See docs/circuits/Bassman5F6A.md.
        if (newtonPortsFrom (u0, Wm, Km, curOut, 2))
        {
            consecutiveFailures = 0;
            return true;
        }
        return false;
    }

    bool newtonPortsFrom (const double* u0, const double (*Wm)[maxUnknowns], const double (*Km)[maxPortsI], double* curOut, int mode) noexcept
    {
        const int nv = (int) voltagePorts.size();
        const int ni = (int) currentPorts.size();

        if (nv == 0)
            return true;

        const double limitScale = mode == 2 ? 0.25 : 1.0;
        double u[maxPortsV];
        for (int j = 0; j < nv; ++j)
        {
            u[j] = uState[j];
            if (predictorEnabled && mode == 0)
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

        iterationsThisSolve = 0;
        double cur[maxPortsI], curNext[maxPortsI];
        bool converged = false;
        double previousStep = 1.0e9;
        double (*Dnow)[maxPortsV] = Dscratch;
        double (*Dnext)[maxPortsV] = DscratchNext;

        ++samplesSolved;
        evaluateDevices (u, cur, Dnow);

        // mode 2 (the last-resort quarter-step fallback) gets a much larger SAFETY ceiling, but only ever USES it when
        // the extra iterations are provably not wasted: past the original 100, every 100 iterations checks whether the
        // pre-limit Newton step (maxStepThisSolve below) has shrunk by at least 2%, and gives up the moment it hasn't.
        //
        // Why: a sample whose forcing changed by hundreds of volts in one 20 us step (measured on the Bassman driven by
        // Volume Normal/Bright and Power Drive all at once, `docs/circuits/Bassman5F6A.md`) makes Newton's per-port step
        // limiter (10 V grid / 40 V plate, a quarter of that in mode 2) the bottleneck, not the algorithm: a logged trace
        // showed it creeping toward the true answer at a small, fixed volts/iteration and simply running out of
        // iterations before arriving -- not diverging. Raising the cap without a shrink check would let a genuinely
        // stuck/oscillating block burn the full extra budget for nothing, the exact CPU problem this project has fixed
        // twice already this session -- the check is what keeps this safe and free for every OTHER circuit (a normal
        // sample converges in single digits of iterations, long before 100, so `iter` never even reaches it; a future
        // high-gain amp model is protected by the same evidence-based rule, no per-pedal tuning).
        //
        // Verified partial fix, not a full one: combined with the tube-model gradient fix below (KorenTriode/
        // KorenPentode/GridCurrent), the single-channel extreme case (Bassman Input = Normal, every preamp control
        // maxed) improved from ~15-20 recoveries in 30 s to ~6-9. The most extreme corner (Input = Jumped, doubling
        // the preamp drive, ALSO with every control maxed) still fails: a trace showed Newton making real, sustained
        // progress for several hundred iterations and then the proposed (pre-limit) step suddenly spiking by orders of
        // magnitude at one specific iteration, while the actual (limited) port voltages stayed physically reasonable --
        // a transient ill-conditioned Jacobian, not a runaway state. Left open rather than tuned further under time
        // pressure; see docs/circuits/Bassman5F6A.md's "still bugging" section for the exact numbers and next steps.
        // mode 2's ceiling was 900 until 2026-09-27: on the Super Lead (TS808 into the amp, hot picking), a rare sample making
        // real but slow progress burned most of that budget, and enough of those in one block made the block itself run at up
        // to ~2.8x its own real-time duration (measured with PresetChainBench) -- a genuine worst-case CPU spike, the thing a
        // real-time guard was tried and REMOVED for (see SuperLeadStyleAmplifierProcessor.h). Cut to 300 (still two stall
        // checks' worth of room past the first 100, so a genuinely-converging-but-slow sample keeps its evidence-gated
        // chance): past 300, recover()'s now-cheap, now-RECENT known-good restore (see the "known-good" DynamicState comment
        // in each amp's Channel struct) is a better outcome for one rare sample than a block-wide time spike.
        const int hardIterationCeiling = deadlineIterations > 0 ? (mode == 0 ? deadlineIterations : (mode == 1 ? 10 : 20))
                                                               : (mode == 2 ? 300 : 100);
        double progressCheckpoint = 1.0e300;
        double maxStepThisSolve = 0.0; // this iteration's pre-limit Newton step, for the stall check above

        for (int iter = 0; iter < hardIterationCeiling && ! converged; ++iter)
        {
            if (mode == 2 && iter >= 100 && (iter % 100) == 0)
            {
                if (maxStepThisSolve >= progressCheckpoint * 0.98)
                    break; // stalled, not just slow -- let the caller's own fallback/recovery handle it as before
                progressCheckpoint = maxStepThisSolve;
            }
            ++iterationsTotal;
            iterationsThisSolve = iter + 1;

            // J = I + K * D, exploiting that D is block-diagonal (each device
            // couples only its own currents to its own port voltages).
            double J[maxPortsV][maxPortsV + 1];
            double meritOld = 0.0; // largest port-equation error at the current point
            for (int j = 0; j < nv; ++j)
            {
                const double* Kj = Km[j];
                double* Jj = J[j];
                for (int c = 0; c < nv; ++c)
                    Jj[c] = (j == c) ? 1.0 : 0.0;

                double f = u[j] - u0[j];
                for (int k = 0; k < ni; ++k)
                    f += Kj[k] * cur[k];
                Jj[nv] = f;
                meritOld = std::max (meritOld, std::abs (f));

                // J[j][c] += sum_k K[j][k] D[k][c], one device at a time (each has 1 or 2 currents and ports).
                for (const auto& m : deviceMaps)
                {
                    if (m.ni == 1 && m.nv == 1)
                        Jj[m.v0] += Kj[m.i0] * Dnow[m.i0][m.v0];
                    else if (m.ni == 2 && m.nv == 2)
                    {
                        const double k0 = Kj[m.i0], k1 = Kj[m.i0 + 1];
                        Jj[m.v0] += k0 * Dnow[m.i0][m.v0] + k1 * Dnow[m.i0 + 1][m.v0];
                        Jj[m.v0 + 1] += k0 * Dnow[m.i0][m.v0 + 1] + k1 * Dnow[m.i0 + 1][m.v0 + 1];
                    }
                    else if (m.ni == 2 && m.nv == 1)
                        Jj[m.v0] += Kj[m.i0] * Dnow[m.i0][m.v0] + Kj[m.i0 + 1] * Dnow[m.i0 + 1][m.v0];
                    else // 1 current, 2 ports (JFET)
                    {
                        const double k0 = Kj[m.i0];
                        Jj[m.v0] += k0 * Dnow[m.i0][m.v0];
                        Jj[m.v0 + 1] += k0 * Dnow[m.i0][m.v0 + 1];
                    }
                }
            }
            // Solve J * delta = f.
            //
            // Tried and reverted: Levenberg-Marquardt diagonal damping as a rare last-resort retry when the raw
            // solution implied a step >500x a port's physical limit (found by tracing the Bassman power stage under
            // extreme sustained drive to a genuinely near-singular local Jacobian). Gating it to mode 2 past its
            // first 200 iterations -- the only region the pathology was ever observed in -- still regressed the
            // realistic single-channel case (BassmanHotInputProbe/BASSMAN_INPUT=0: 8 -> 15 recoveries) while only
            // partially helping the extreme corner (167 -> 134). An ordinary, otherwise-fine mode-2 sample can
            // apparently still pass through that same region on its way to converging; damping the linear system
            // there changes which direction the step takes and knocks some of those off their working trajectory.
            // Two independent attempts at patching this at the linear-algebra level (this one, and the earlier
            // reverted per-port step clamp) have now each destabilized the well-converging case while only
            // partially fixing the target one -- see docs/circuits/Bassman5F6A.md for why the next attempt should
            // target the tube model's own derivative (the actual source of the near-zero Jacobian diagonal)
            // instead of patching the Newton solve around it.
            double delta[maxPortsV];
            if (! solveSmall (J, nv, delta))
                return false;

            // Proposed update, with SPICE-style logarithmic limiting on p-n junction ports: an exponential's
            // plain Newton step from a low-current start overshoots by orders of magnitude, then sheds only
            // ~Vt per iteration on the way back.
            double proposed[maxPortsV], rawStep[maxPortsV];
            bool limited = false;
            double maxStep = 0.0;
            // The fraction of its Newton step that the forward-junction limiter let through, at its smallest over all
            // ports. Junctions coupled through the network (the two clamp diodes of an op-amp macro-model: u_a + u_b is a
            // constant of the circuit) have to move by the same fraction, or the one that is not being limited runs away
            // on its own (its reverse-bias step limit, ~2 |v|, grows every iteration) and drags the limited one to a
            // standstill through the common step scale below: the solve then fails, and a failed solve freezes the circuit.
            double forwardFraction = 1.0;
            for (int j = 0; j < nv; ++j)
            {
                double unew = u[j] - delta[j];
                rawStep[j] = unew - u[j];
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
                    {
                        limited = true;
                        if (rawStep[j] > 1.0e-9 && limitedValue > u[j])
                            forwardFraction = std::min (forwardFraction, (limitedValue - u[j]) / rawStep[j]);
                    }
                    unew = limitedValue;
                }
                proposed[j] = unew;
            }

            double maxRatio = 0.0;
            for (int j = 0; j < nv; ++j)
            {
                if (forwardFraction < 1.0 && junction[j].isJunction && ! junction[j].pair && rawStep[j] < 0.0
                    && std::abs (forwardFraction * rawStep[j]) < std::abs (proposed[j] - u[j]))
                    proposed[j] = u[j] + forwardFraction * rawStep[j];
                maxStep = std::max (maxStep, std::abs (proposed[j] - u[j]));
                maxRatio = std::max (maxRatio, std::abs (proposed[j] - u[j]) / (portLimit[j] * limitScale));
            }

            const bool fullStep = maxRatio <= 1.0;
            const double scale = fullStep ? 1.0 : 1.0 / maxRatio;
            double step[maxPortsV];
            for (int j = 0; j < nv; ++j)
            {
                step[j] = scale * (proposed[j] - u[j]);
                u[j] += step[j];
            }
            maxStepThisSolve = maxStep;

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

            // A step this small leaves an error of ~step^2 / (2 nVt) (<= 3 uV at 0.5 mV): take the linearised currents
            // and skip the evaluation that would only confirm it (the common case for a converged sample).
            if (fullStep && ! limited && maxStep <= smallStepAccept)
            {
                for (int k = 0; k < ni; ++k)
                    cur[k] += predictedDelta[k];
                converged = true;
                break;
            }

            evaluateDevices (u, curNext, Dnext);

            // Safeguard for hard-driven tubes: a Newton step that makes the residual (the port equations' error)
            // larger is halved until it does not (bounded); the plain step is kept whenever it already improves.
            {
                auto meritAt = [&] (const double* curEval)
                {
                    double worst = 0.0;
                    for (int j = 0; j < nv; ++j)
                    {
                        double f = u[j] - u0[j];
                        for (int k = 0; k < ni; ++k)
                            f += Km[j][k] * curEval[k];
                        worst = std::max (worst, std::abs (f));
                    }
                    return worst;
                };

                // Only for tube circuits: a diode's exponential makes the residual rise before it falls, and damping
                // those steps costs the solver more than it saves.
                const bool safeguard = ! triodes.empty() || ! pentodes.empty();
                double meritNew = safeguard ? meritAt (curNext) : 0.0;
                for (int back = 0; safeguard && back < 8 && meritNew > meritOld && meritNew > 1.0e-4; ++back)
                {
                    for (int j = 0; j < nv; ++j)
                    {
                        step[j] *= 0.5;
                        u[j] -= step[j];
                    }
                    maxStep *= 0.5;
                    evaluateDevices (u, curNext, Dnext);
                    meritNew = meritAt (curNext);
                    limited = true; // the linearised prediction no longer applies to this point
                    for (int k = 0; k < ni; ++k)
                        predictedDelta[k] = curNext[k] - cur[k];
                }
            }

            double nodeErrorVec[maxUnknowns];
            for (int node = 0; node < unknownCount; ++node)
                nodeErrorVec[node] = 0.0;
            for (int k = 0; k < ni; ++k)
            {
                const double residual = curNext[k] - cur[k] - predictedDelta[k];
                const double* Wk = Wm[k];
                for (int node = 0; node < unknownCount; ++node)
                    nodeErrorVec[node] += Wk[node] * residual;
            }
            double nodeError = 0.0;
            for (int node = 0; node < unknownCount; ++node)
                nodeError = std::max (nodeError, std::abs (nodeErrorVec[node]));

            // Accept the new point (with the currents evaluated AT it, so the reconstruction below is consistent
            // with it) when the leftover is below tolerance; or, at the noise floor, when steps have stopped shrinking.
            if (nodeError < nodeTolerance || (fullStep && ! limited && iter > 3 && maxStep < 1.0e-4 && maxStep > 0.9 * previousStep))
            {
                // Never accept a point whose port equations are still far from balanced. The "steps stopped shrinking" branch above (and a
                // small nodeError on a device whose current is enormous but insensitive, like a plate arcing at 6 kV) can pass a state
                // that is not a solution of the circuit: the Super Lead's power block landed there and printed 1000 V plates one sample
                // after a hard flip (docs/circuits/SuperLead1959.md). A real solution has u = u0 - K cur to well within a volt.
                double worstPortError = 0.0;
                for (int j = 0; j < nv; ++j)
                {
                    double f = u[j] - u0[j];
                    for (int k = 0; k < ni; ++k)
                        f += Km[j][k] * curNext[k];
                    worstPortError = std::max (worstPortError, std::abs (f) / (1.0 + 1.0e-3 * std::abs (u[j])));
                }
                converged = worstPortError < portResidualAccept;
            }
            previousStep = maxStep;

            for (int k = 0; k < ni; ++k)
                cur[k] = curNext[k];
            std::swap (Dnow, Dnext);
        }

        if (! converged && deadlineIterations > 0 && mode == 0 && iterationsThisSolve >= deadlineIterations)
            converged = true; // deadline: the last iterate stands (see setDeadlineMode())

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

        for (int k = 0; k < ni; ++k)
            curOut[k] = cur[k];
        for (int j = 0; j < nv; ++j)
        {
            uStatePrev2[j] = uStatePrev[j];
            uStatePrev[j] = uState[j];
            uState[j] = u[j];
        }
        return true;
    }

    /** DC / relaxation path: node vector from the no-device response `xLinear`. */
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

        double u0[maxPortsV], cur[maxPortsI];
        for (int j = 0; j < nv; ++j)
        {
            const auto& vp = voltagePorts[(size_t) j];
            u0[j] = nodeOf (vp.plus, xLinear) - nodeOf (vp.minus, xLinear);
        }
        if (! newtonPorts (u0, W, K, cur))
            return false;

        // `cur` was evaluated AT the accepted u, so the node vector below is consistent with it.
        for (int i = 0; i < unknownCount; ++i)
        {
            double v = xLinear[i];
            for (int k = 0; k < ni; ++k)
                v -= W[k][i] * cur[k];
            x[(size_t) i] = v;
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
                double ic = op.iC, dic = op.diC_dvbe;
                if (q.iSat > 0.0 && ic > 0.0)
                {
                    // smooth limit ic / (1 + (ic/S)^4)^(1/4): the knee of a stage running out of collector current
                    const double r = ic / q.iSat, r2 = r * r, a = 1.0 + r2 * r2, root = std::sqrt (std::sqrt (a));
                    dic *= (1.0 / root) * (1.0 - r2 * r2 / a);
                    ic /= root;
                }
                cur[m.i0] = ic;
                cur[m.i0 + 1] = op.iB;
                D[m.i0][m.v0] = dic;
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
            else if (m.kind == 7)
            {
                const auto op = triodes[(size_t) m.index].model.evaluate (u[m.v0], u[m.v0 + 1]);
                cur[m.i0] = op.ip;
                cur[m.i0 + 1] = op.ig;
                D[m.i0][m.v0] = op.dip_dvgk;
                D[m.i0][m.v0 + 1] = op.dip_dvpk;
                D[m.i0 + 1][m.v0] = op.dig_dvgk;
                D[m.i0 + 1][m.v0 + 1] = 0.0;
            }
            else if (m.kind == 8)
            {
                const auto& t = pentodes[(size_t) m.index];
                const auto op = t.model.evaluate (u[m.v0], u[m.v0 + 1], t.vg2, t.vg2Pow);
                cur[m.i0] = op.ip;
                cur[m.i0 + 1] = op.ig;
                D[m.i0][m.v0] = op.dip_dvgk;
                D[m.i0][m.v0 + 1] = op.dip_dvpk;
                D[m.i0 + 1][m.v0] = op.dig_dvgk;
                D[m.i0 + 1][m.v0 + 1] = 0.0;
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
            uState[j] = uStatePrev[j] = uStatePrev2[j] = 0.0;

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
                    uState[j] = uStatePrev[j] = uStatePrev2[j] = voltage (voltagePorts[j].plus) - voltage (voltagePorts[j].minus);

            double* rhs = rhsLinear.data();
            for (int i = 0; i < unknownCount; ++i)
                rhs[i] = 0.0;
            for (const auto& t : knownTerms)
                rhs[t.row] += t.g * knownVoltage[(size_t) t.knownNode];
            for (const auto& src : sources)
                if (src.current && indexOf[(size_t) src.node] >= 0)
                    rhs[indexOf[(size_t) src.node]] += src.volts;
            for (auto& c : capacitors)
            {
                c.ieq = c.group >= 0 ? 0.0 : c.g * c.vPrev;
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
            for (const auto& grp : inductorGroups)
                for (int i = 0; i < grp.n; ++i)
                    capacitors[(size_t) (grp.first + i)].iPrev = inductorShortG * capacitors[(size_t) (grp.first + i)].vPrev;

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
            uState[j] = uStatePrev[j] = uStatePrev2[j] = voltage (voltagePorts[j].plus) - voltage (voltagePorts[j].minus);
        return ok;
    }
};

} // namespace openguitarmultifx
