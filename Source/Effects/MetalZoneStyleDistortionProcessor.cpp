#include "MetalZoneStyleDistortionProcessor.h"
#include "IconKit.h"
#include "NobelsCommon.h"
#include "PotTaper.h"

#include <IconData.h>

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
    constexpr double jfetDrop = -1.05;                            // Q011 source follower DC seed (fitted)
    constexpr double jfetIdss = 4.5e-3, jfetPinch = -1.2, jfetLambda = 0.02;   // jfet2SK184
    constexpr double followerDrop = 0.65;                          // bootstrap-branch emitter followers

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
            double v = px;
            for (int i = 0; i < 8; ++i)
            {
                const double eh = std::exp (std::min ((v - m5218.highRail) / railClampNVt, 40.0));
                const double el = std::exp (std::min ((m5218.lowRail - v) / railClampNVt, 40.0));
                const double f = 1.0e-6 * (vAmp - v) - cpx.g * v + cpx.ieq - railClampIs * (eh - el);
                const double d = -1.0e-6 - cpx.g - railClampIs / railClampNVt * (eh + el);
                const double dv = f / d;
                v -= dv;
                if (std::abs (dv) < 1.0e-10)
                    break;
            }
            return v;
        }

        /** One iteration of {m-KCL, integrator}: given the (+) input, the inverting-node solve and
            the output from the previous pass, return the new output. */
        double finish (double vPlus, double vm) noexcept
        {
            px = solvePx (m5218.dcGain * (vPlus - vm));
            out = juce::jlimit (m5218.lowRail, m5218.highRail, px);
            return out;
        }
    };

    /** IC3b/IC4b's inverting-leg branch: series cap C1 into node n1; n1 -> rE -> the emitter
        (an ideal follower of the base, drop 0.65); n1 -> C2 -> the base; base -> rBase -> vBias.
        Idealising the emitter follower misses the 2SC3378's Vbe knee, its can't-sink cut-off and
        its base current -- measured vs the netlist that's worth ~8 dB of worst-band error at hot
        inputs (the real-BJT replacement was tried and abandoned: the m-node's solve loses its
        closed form and every iteration scheme tried either latched the amp's parasitic rail
        state or 2-cycled across the rails). */
    struct BootstrapLeg
    {
        ReducedCap c1, c2;
        double f1, f2, rE, rBase;
        double gE = 0.0, gB = 0.0, v1 = vBias, vb2 = vBias, ve = vBias;

        BootstrapLeg (double c1F, double c2F, double emitterOhms, double baseOhms)
            : f1 (c1F), f2 (c2F), rE (emitterOhms), rBase (baseOhms) {}

        void prepare (double fs) noexcept
        {
            c1.prepare (f1, fs);
            c2.prepare (f2, fs);
            gE = 1.0 / rE;
            gB = 1.0 / rBase;
            v1 = vb2 = vBias;
            ve = vBias - followerDrop;
        }

        /** v1 as an affine function of v_m: v1 = aL*v_m + bL -- what the m-node KCL needs to
            express the whole leg as one admittance (i_leg = c1.g*(1-aL)*v_m - c1.g*bL - c1.ieq). */
        double aL() const noexcept
        {
            const double dvb = c2.g + gB;
            return c1.g / (c1.g + gE + c2.g - (gE + c2.g) * c2.g / dvb);
        }
        double bL() const noexcept
        {
            const double dvb = c2.g + gB;
            const double rhs = -c1.ieq + c2.ieq + (gE + c2.g) * (gB * vBias - c2.ieq) / dvb - gE * followerDrop;
            return rhs / (c1.g + gE + c2.g - (gE + c2.g) * c2.g / dvb);
        }
        double kLeg() const noexcept { return c1.g * (1.0 - aL()); }          // d i_leg / d v_m
        double cLeg() const noexcept { return -c1.g * bL() - c1.ieq; }        // i_leg at v_m = 0

        void solveNodes (double vm) noexcept
        {
            v1 = aL() * vm + bL();
            vb2 = (c2.g * v1 - c2.ieq + gB * vBias) / (c2.g + gB);
            ve = vb2 - followerDrop;
        }

        void commit (double vm) noexcept
        {
            c1.commit (vm - v1);
            c2.commit (v1 - vb2);
        }
    };
}

/** The reducedOrder substitute for blocks 0-2. See the netlist in buildChannel() for the topology
    this mirrors -- node names match, the comments there explain the circuit itself. */
struct MetalZoneStyleDistortionProcessor::ReducedFrontEnd
{
    // input ladder: (10k + C042) -> g -> [JFET follower] -> C039 -> nA -> 200R -> nB -> C033 -> nC
    ReducedCap c042, c039, c033;
    // IC3b: fb 220k || C032; (-) leg = the Q010 bootstrap branch
    ReducedCap c032;
    BootstrapLeg leg3b { 0.027e-6, 0.01e-6, 2.2e3, 47.0e3 };
    ReducedAmp amp3b;
    // IC3a interstage: p {10k, C031} -> C029 -> q {100k}; leg = 1k -> nR -> C030; fb = (rDist+1k) || C028
    ReducedCap c031, c029, c030, c028;
    ReducedAmp amp3a;
    // clipper: C027 -> n27 -> 2k2 -> k {diode pair, 10k -> l}; l: 4k7 -> n31 {C023}, C021 -> m2 {100k}
    ReducedCap c027, c023, c021;
    // IC4b: fb 3k3 || C022; legs = the Q008 and Q007 bootstrap branches
    ReducedCap c022;
    BootstrapLeg legA4b { 0.015e-6, 0.0015e-6, 1.0e3, 47.0e3 };
    BootstrapLeg legB4b { 0.22e-6, 0.047e-6, 470.0, 470.0e3 };
    ReducedAmp amp4b;
    // IC4a inverter: 22k in, 22k || C018 fb, (+) at vBias
    ReducedCap c018;
    ReducedAmp amp4a;

    double gDist = 1.0 / 2.0e3;   // 1 / (rDist + R051): set from updatePots()
    double vk = 0.0;              // clipper node warm start

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
        ic3b = ic3a = ic4b = ic4a = ic3aPrev = ic3aP = ic3aQ = v;
        for (int i = 0; i < 300; ++i)
            step (0.0); // settle whatever the analytic seed missed
    }

    /** One feedback-amp macro stage: solves {v_m, px} exactly while the integrator node sits inside
        the rails; past them it re-solves m with the loop broken (v_out held at the rail) and runs
        the clamp-diode Newton -- the netlist's two satMode passes. K = feedback conductance sum,
        G = K + sum of leg admittances, Hm = the m-node history terms. */
    static double stage (double vPlus, double kFb, double gSum, double hm, ReducedAmp& amp) noexcept
    {
        const double gRc = 1.0e-6, gain = m5218.dcGain;
        const double pxLin = (gRc * gain * (vPlus - hm / gSum) + amp.cpx.ieq) / (gRc * gain * kFb / gSum + gRc + amp.cpx.g);
        double vo;
        if (pxLin > m5218.lowRail && pxLin < m5218.highRail)
        {
            amp.px = pxLin;
            vo = pxLin;
        }
        else
        {
            vo = juce::jlimit (m5218.lowRail, m5218.highRail, pxLin);
            const double vm = (kFb * vo + hm) / gSum;
            amp.px = amp.solvePx (gain * (vPlus - vm));
            vo = juce::jlimit (m5218.lowRail, m5218.highRail, amp.px);
        }
        amp.out = vo;
        amp.cpx.commit (amp.px);
        return vo;
    }

    double step (double vin) noexcept
    {
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
        const double g3b = kFb3b + leg3b.kLeg();
        const double hm3b = c032.ieq - leg3b.cLeg();
        const double vo3b = stage (vnC, kFb3b, g3b, hm3b, amp3b);
        const double vm3b = (kFb3b * vo3b + hm3b) / g3b;
        ic3b = vo3b;

        // ---- interstage + IC3a ----
        const double g45 = 1.0 / 10150.0, g42 = 1.0e-4, g40 = 1.0e-5;
        const double aQ = c029.g / (c029.g + g40), bQ = (-c029.ieq + g40 * vBias) / (c029.g + g40); // q = aQ*p + bQ
        const double vp = (g45 * vo3b + g42 * vBias + c031.g * vBias + c031.ieq + c029.ieq + c029.g * bQ)
                          / (g45 + g42 + c031.g + c029.g * (1.0 - aQ));
        const double vq = aQ * vp + bQ;
        const double g41 = 1.0e-3, kLeg3a = g41 * c030.g / (g41 + c030.g), cLeg3a = -g41 * c030.ieq / (g41 + c030.g);
        const double kFb3a = gDist + c028.g;
        const double g3a = kFb3a + kLeg3a;
        const double hm3a = c028.ieq - cLeg3a;
        const double vo3a = stage (vq, kFb3a, g3a, hm3a, amp3a);
        const double vm3a = (kFb3a * vo3a + hm3a) / g3a;
        const double vnR3a = (g41 * vm3a + c030.ieq) / (g41 + c030.g);
        ic3a = vo3a;
        ic3aPrev = vo3b;
        ic3aP = vp;
        ic3aQ = vq;

        // ---- clipper: (150R+C027) -> n27 -> 2k2 -> k {diode pair to gnd, 10k -> l}; l: 4k7 -> n31 {C023}, C021 -> m2 {100k}
        const double gE27 = c027.g / (1.0 + c027.g * 150.0), hE27 = c027.ieq / (1.0 + c027.g * 150.0);
        const double g28 = 1.0 / 2.2e3, g32 = 1.0e-4, g47 = 1.0 / 4.7e3, g29 = 1.0e-5;
        const double a31 = g47 / (g47 + c023.g), b31 = c023.ieq / (g47 + c023.g);
        const double a2 = c021.g / (c021.g + g29), b2 = (-c021.ieq + g29 * vBias) / (c021.g + g29);
        const double dL = g32 + g47 * (1.0 - a31) + c021.g * (1.0 - a2);
        const double aL = g32 / dL, bL = (c021.ieq + g47 * b31 + c021.g * b2) / dL;
        const double aK = g28 * gE27 / (gE27 + g28) + g32 * (1.0 - aL);
        const double bK = g28 * (gE27 * vo3a - hE27) / (gE27 + g28) + g32 * bL;
        for (int i = 0; i < 8; ++i)
        {
            const double ep = std::exp (std::min (vk / siNVt, 40.0)), em = std::exp (std::min (-vk / siNVt, 40.0));
            const double f = aK * vk + siIs * (ep - em) - bK;
            const double d = aK + siIs / siNVt * (ep + em);
            const double dv = f / d;
            vk -= dv;
            if (std::abs (dv) < 1.0e-10)
                break;
        }
        const double vn27 = (gE27 * vo3a - hE27 + g28 * vk) / (gE27 + g28);
        const double vl = aL * vk + bL;
        const double vn31 = a31 * vl + b31;
        const double vm2 = a2 * vl + b2;

        // ---- IC4b ----
        const double kFb4b = 1.0 / 3.3e3 + c022.g;
        const double g4b = kFb4b + legA4b.kLeg() + legB4b.kLeg();
        const double hm4b = c022.ieq - legA4b.cLeg() - legB4b.cLeg();
        const double vo4b = stage (vm2, kFb4b, g4b, hm4b, amp4b);
        const double vm4b = (kFb4b * vo4b + hm4b) / g4b;
        ic4b = vo4b;

        // ---- IC4a inverter: m sees g22->vo4b and (g22||C018)->out; same stage() solve, the
        // input conductance folded into the history term. An iterate-the-output loop period-2
        // oscillates between the rails here, so it cannot be used. ----
        const double g22 = 1.0 / 22.0e3;
        const double vo4a = stage (vBias, g22 + c018.g, g22 + g22 + c018.g, g22 * vo4b + c018.ieq, amp4a);
        const double vm4a = (g22 * vo4b + (g22 + c018.g) * vo4a + c018.ieq) / (g22 + g22 + c018.g);
        ic4a = vo4a;

        // ---- commit every capacitor's terminal voltage ----
        const double iE1 = gE1 * (vin - vg) - hE1;
        c042.commit (vin - iE1 * 10.0e3 - vg);
        c039.commit (vs - vnA);
        c033.commit (vnB - vnC);
        c032.commit (vm3b - vo3b);
        leg3b.solveNodes (vm3b);
        leg3b.commit (vm3b);
        c031.commit (vp - vBias);
        c029.commit (vp - vq);
        c030.commit (vnR3a);
        c028.commit (vm3a - vo3a);
        const double iE27 = gE27 * (vo3a - vn27) - hE27;
        c027.commit (vo3a - iE27 * 150.0 - vn27);
        c023.commit (vn31);
        c021.commit (vl - vm2);
        c022.commit (vm4b - vo4b);
        legA4b.solveNodes (vm4b);
        legB4b.solveNodes (vm4b);
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
