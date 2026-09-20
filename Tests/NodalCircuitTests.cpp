#include "Effects/BD2StyleOverdriveProcessor.h"
#include "Effects/CentaurStyleOverdriveProcessor.h"
#include "Effects/HM2StyleDistortionProcessor.h"
#include "Effects/NodalCircuit.h"
#include "Effects/TubeScreamerStyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class NodalCircuitTests : public juce::UnitTest
{
public:
    NodalCircuitTests() : juce::UnitTest ("NodalCircuit", "Effects") {}

    static constexpr double sr = 48000.0;

    /** Steady-state fundamental amplitude of `outNode` for a sine on `sourceHandle`. */
    static double sineGain (NodalCircuit& c, int sourceHandle, NodalCircuit::Node outNode, double freq, double amp,
                            double dcIn = 0.0, double dcOut = 0.0, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const double cycles = std::floor (measure * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;

        double sumS = 0.0, sumC = 0.0;
        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            c.setSource (sourceHandle, dcIn + amp * std::sin (ph));
            c.solveSample();
            if (n >= start)
            {
                const double y = c.voltage (outNode) - dcOut;
                sumS += y * std::sin (ph);
                sumC += y * std::cos (ph);
            }
        }
        return 2.0 * std::sqrt (sumS * sumS + sumC * sumC) / (double) len / amp;
    }

    void runTest() override
    {
        beginTest ("RC low-pass: -3 dB at the corner (trapezoidal warp accounted for)");
        {
            NodalCircuit c;
            c.setIntegrationTheta (0.5); // this test checks the exact trapezoidal frequency warp
            const auto in = c.addNode(), out = c.addNode();
            const int src = c.addSource (in, 0.0);
            c.addResistor (in, out, 1000.0);
            c.addCapacitor (out, NodalCircuit::ground, 159.155e-9);
            expect (c.prepare (sr));

            // Bilinear-warped analog corner at 1 kHz / 48 kHz.
            const double wa = 2.0 * sr * std::tan (juce::MathConstants<double>::pi * 1000.0 / sr);
            const double expected = 1.0 / std::sqrt (1.0 + std::pow (wa * 1000.0 * 159.155e-9, 2.0));
            const double g = sineGain (c, src, out, 1000.0, 1.0);
            logMessage ("gain@1k=" + juce::String (g, 5) + " expected=" + juce::String (expected, 5));
            expectWithinAbsoluteError (g, expected, 0.002);
        }

        beginTest ("ideal op-amp: 1 + Zf/Zi with a series-cap gain leg (the Tube Screamer's shape)");
        {
            NodalCircuit c;
            c.setIntegrationTheta (0.5); // exact trapezoidal warp in the expected value below
            const auto in = c.addNode(), inv = c.addNode(), mid = c.addNode(), out = c.addNode();
            const int src = c.addSource (in, 0.0);
            c.addOpAmp (in, inv, out);
            c.addResistor (inv, out, 9000.0);       // Rf
            c.addCapacitor (inv, mid, 1.0e-6);      // C in series with R1
            c.addResistor (mid, NodalCircuit::ground, 1000.0);
            expect (c.prepare (sr));

            const double f = 1000.0;
            const double w = 2.0 * sr * std::tan (juce::MathConstants<double>::pi * f / sr);
            const std::complex<double> zi (1000.0, -1.0 / (w * 1.0e-6));
            const double expected = std::abs (1.0 + 9000.0 / zi);
            const double g = sineGain (c, src, out, f, 0.01);
            logMessage ("gain@1k=" + juce::String (g, 4) + " expected=" + juce::String (expected, 4));
            expectWithinAbsoluteError (g, expected, 0.02 * expected);
        }

        beginTest ("saturating op-amp: linear gain matches the ideal one, the output stops at its rails, and the loop really breaks");
        {
            NodalCircuit c;
            c.setIntegrationTheta (0.5);
            const auto in = c.addNode(), inv = c.addNode(), out = c.addNode(), vb = c.addNode();
            const int src = c.addSource (in, 4.5);
            c.addSource (vb, 4.5);
            NodalCircuit::OpAmpSpec spec;
            spec.lowRail = 1.5;
            spec.highRail = 7.5;
            c.addSaturatingOpAmp (in, inv, out, spec);
            c.addResistor (inv, out, 9000.0);
            c.addResistor (inv, vb, 1000.0);
            c.setInitialGuess (inv, 4.5);
            c.setInitialGuess (out, 4.5);
            expect (c.prepare (sr));

            // Non-inverting gain 1 + 9k/1k = 10, exactly (inside the rails it IS the ideal op-amp).
            const double g = sineGain (c, src, out, 1000.0, 0.01, 4.5, 4.5);
            logMessage ("small-signal gain = " + juce::String (g, 4) + " (ideal 10)");
            expectWithinAbsoluteError (g, 10.0, 0.01);

            // 2 V in would be 20 V out: it must stop at the rails (within the clamp diodes' soft knee), and while it
            // sits on a rail the inverting input must have stopped following the non-inverting one.
            double top = -1.0e9, bottom = 1.0e9, worstFollowError = 0.0;
            bool solved = true;
            for (int n = 0; n < (int) (0.1 * sr); ++n)
            {
                const double vin = 4.5 + 2.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * (double) n / sr);
                c.setSource (src, vin);
                solved = c.solveSample() && solved;
                top = juce::jmax (top, c.voltage (out));
                bottom = juce::jmin (bottom, c.voltage (out));
                worstFollowError = juce::jmax (worstFollowError, std::abs (c.voltage (inv) - c.voltage (in)));
            }
            logMessage ("output range [" + juce::String (bottom, 2) + ", " + juce::String (top, 2) + "] V, worst V(+)-V(-) = " + juce::String (worstFollowError, 2));
            expect (solved);
            expectWithinAbsoluteError (top, 7.5, 1.0e-6);   // sits exactly at the rail
            expectWithinAbsoluteError (bottom, 1.5, 1.0e-6);
            expectGreaterThan (worstFollowError, 0.5);
        }

        beginTest ("anti-parallel diode clipper DC point matches an independent scalar solve");
        {
            NodalCircuit c;
            const auto in = c.addNode(), out = c.addNode();
            const int src = c.addSource (in, 2.0);
            c.addResistor (in, out, 10000.0);
            const double Is = 2.52e-9, nVt = 25.85e-3 * 1.752;
            c.addDiode (out, NodalCircuit::ground, Is, nVt);
            c.addDiode (NodalCircuit::ground, out, Is, nVt);
            expect (c.prepare (sr));
            c.setSource (src, 2.0);
            expect (c.solveSample());

            // (2 - v)/10k = 2*Is*sinh(v/nVt), plain bisection.
            double lo = 0.0, hi = 2.0;
            for (int i = 0; i < 200; ++i)
            {
                const double v = 0.5 * (lo + hi);
                const double f = (2.0 - v) / 10000.0 - 2.0 * Is * std::sinh (v / nVt);
                (f > 0.0 ? lo : hi) = v;
            }
            logMessage ("nodal=" + juce::String (c.voltage (out), 6) + " scalar=" + juce::String (0.5 * (lo + hi), 6));
            expectWithinAbsoluteError (c.voltage (out), 0.5 * (lo + hi), 1.0e-6);
        }

        beginTest ("BJT emitter follower: MNA matches EbersMollBJT's own Thevenin solve (NPN and PNP)");
        {
            const NodalCircuit::BjtParams p { 1.0e-14, 25.85e-3, 300.0, 4.0 };

            NodalCircuit npn;
            const auto vcc = npn.addNode(), bias = npn.addNode(), b = npn.addNode(), e = npn.addNode();
            npn.addSource (vcc, 9.0);
            npn.addSource (bias, 4.5);
            npn.addResistor (b, bias, 510.0e3);
            npn.addResistor (e, NodalCircuit::ground, 10.0e3);
            npn.addBjt (vcc, b, e, false, p);
            expect (npn.prepare (sr));

            EbersMollBJT ref;
            ref.setParameters (p.Is, p.Vt, p.betaF, p.betaR);
            ref.reset (3.9, 3.3, 9.0);
            double vb, ve, vc;
            expect (ref.solve (510.0e3, 4.5, 10.0e3, 0.0, 1.0e-6, 9.0, vb, ve, vc));
            logMessage ("NPN mna Vb=" + juce::String (npn.voltage (b), 4) + " ref Vb=" + juce::String (vb, 4));
            expectWithinAbsoluteError (npn.voltage (b), vb, 1.0e-3);
            expectWithinAbsoluteError (npn.voltage (e), ve, 1.0e-3);

            // PNP mirror image: rails and bias reflected about the supply.
            NodalCircuit pnp;
            const auto v9 = pnp.addNode(), pbias = pnp.addNode(), pb = pnp.addNode(), pe = pnp.addNode();
            pnp.addSource (v9, 9.0);
            pnp.addSource (pbias, 4.5);
            pnp.addResistor (pb, pbias, 510.0e3);
            pnp.addResistor (pe, v9, 10.0e3);
            pnp.addBjt (NodalCircuit::ground, pb, pe, true, p);
            expect (pnp.prepare (sr));
            expectWithinAbsoluteError (pnp.voltage (pb), 9.0 - vb, 1.0e-3);
            expectWithinAbsoluteError (pnp.voltage (pe), 9.0 - ve, 1.0e-3);
        }

        beginTest ("JFET source follower: MNA matches ShichmanHodgesJFET's own solve");
        {
            NodalCircuit c;
            const auto vdd = c.addNode(), g = c.addNode(), s = c.addNode();
            c.addSource (vdd, 9.0);
            c.addResistor (g, NodalCircuit::ground, 1.0e6);
            c.addResistor (s, NodalCircuit::ground, 10.0e3);
            c.addJfet (vdd, g, s, { 4.0e-3, -2.0, 0.01 });
            expect (c.prepare (sr));

            ShichmanHodgesJFET ref;
            ref.setParameters (4.0e-3, -2.0, 0.01);
            ref.reset (9.0, 1.0);
            double vd, vs;
            expect (ref.solve (0.0, 1.0e-6, 9.0, 10.0e3, 0.0, vd, vs));
            logMessage ("JFET mna Vs=" + juce::String (c.voltage (s), 4) + " ref Vs=" + juce::String (vs, 4));
            expectWithinAbsoluteError (c.voltage (s), vs, 1.0e-3);
        }

        beginTest ("whole-pedal cross-check: a TS808 netlist reproduces the hand-derived TubeScreamerStyleOverdriveProcessor");
        {
            using TS = TubeScreamerStyleOverdriveProcessor;
            const double drive = 0.5, tone = 0.5, level = 0.5;

            NodalCircuit c;
            c.setIntegrationTheta (0.5); // the hand-derived processor it is compared against is trapezoidal
            const auto vcc = c.addNode(), bias = c.addNode(), in = c.addNode();
            const auto n1 = c.addNode(), nb = c.addNode(), ne = c.addNode(), np = c.addNode();
            const auto nm = c.addNode(), nc3 = c.addNode(), nf = c.addNode(), o1 = c.addNode();
            const auto nA = c.addNode(), nB = c.addNode(), nW = c.addNode(), nw6 = c.addNode(), o2 = c.addNode();
            const auto n7 = c.addNode(), nt = c.addNode(), nlw = c.addNode(), nsw = c.addNode();
            const auto nqb = c.addNode(), nqe = c.addNode(), nc9 = c.addNode(), out = c.addNode();
            c.addSource (vcc, 9.0);
            c.addSource (bias, 4.5);
            const int src = c.addSource (in, 0.0);

            const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, 300.0, 4.0 };
            const double Is = 2.52e-9, nVt = 25.85e-3 * 1.752;
            const auto gnd = NodalCircuit::ground;

            c.addCapacitor (in, n1, 0.02e-6);   c.addResistor (n1, nb, 1.0e3);   c.addResistor (nb, bias, 510.0e3);
            c.addBjt (vcc, nb, ne, false, npn); c.addResistor (ne, gnd, 10.0e3);
            c.addCapacitor (ne, np, 1.0e-6);    c.addResistor (np, bias, 10.0e3);
            c.addOpAmp (np, nm, o1);
            c.addCapacitor (nm, nc3, 0.047e-6); c.addResistor (nc3, gnd, 4.7e3);
            c.addResistor (nm, nf, 51.0e3);     c.addResistor (nf, o1, 500.0e3 * drive * drive);
            c.addCapacitor (nm, o1, 51.0e-12);
            c.addDiode (nm, o1, Is, nVt);       c.addDiode (o1, nm, Is, nVt);
            c.addResistor (o1, nA, 1.0e3);      c.addCapacitor (nA, gnd, 0.22e-6);   c.addResistor (nA, bias, 10.0e3);
            c.addOpAmp (nA, nB, o2);            c.addResistor (nB, o2, 1.0e3);
            c.addResistor (nA, nW, 20.0e3 * tone);        c.addResistor (nW, nB, 20.0e3 * (1.0 - tone));
            c.addResistor (nW, nw6, 220.0);     c.addCapacitor (nw6, gnd, 0.22e-6);
            c.addCapacitor (o2, n7, 1.0e-6);    c.addResistor (n7, nt, 1.0e3);
            c.addResistor (nt, nlw, 100.0e3 * (1.0 - level * level));
            c.addResistor (nlw, gnd, 100.0e3 * level * level);
            c.addResistor (nlw, nsw, 100.0);    c.addCapacitor (nsw, nqb, 0.1e-6);   c.addResistor (nqb, bias, 510.0e3);
            c.addBjt (vcc, nqb, nqe, false, npn); c.addResistor (nqe, gnd, 10.0e3);
            c.addResistor (nqe, nc9, 100.0);    c.addCapacitor (nc9, out, 10.0e-6);
            c.addResistor (out, gnd, 1.0 / (1.0 / 10.0e3 + 1.0 / 1.0e6));
            expect (c.prepare (sr));
            logMessage ("TS808 netlist: " + juce::String (c.unknowns()) + " unknowns");

            TS proc (TS::Model::ts808);
            proc.prepare (sr, 512, 1);
            for (auto* prm : proc.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    if (f->paramID == "ts808_drive") *f = (float) drive;
                    if (f->paramID == "ts808_tone") *f = (float) tone;
                    if (f->paramID == "ts808_level") *f = (float) level;
                }

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double amp : { 0.005, 0.05 })
            {
                const double freq = 1000.0;
                const long long total = (long long) (0.6 * sr);
                const long long len = 9600; // 200 whole cycles at 1 kHz
                const long long start = total - len;
                double aS = 0, aC = 0, bS = 0, bC = 0;
                juce::AudioBuffer<float> buf (1, 1);

                for (long long n = 0; n < total; ++n)
                {
                    const double x = amp * std::sin (twoPi * freq * (double) n / sr);
                    c.setSource (src, x);
                    c.solveSample();
                    buf.setSample (0, 0, (float) x);
                    proc.process (buf);

                    if (n >= start)
                    {
                        const double ph = twoPi * freq * (double) n / sr;
                        const double a = c.voltage (out) - c.voltage (out) * 0.0; // netlist output (DC blocked by C9)
                        const double b = (double) buf.getSample (0, 0);
                        aS += a * std::sin (ph); aC += a * std::cos (ph);
                        bS += b * std::sin (ph); bC += b * std::cos (ph);
                    }
                }

                const double ampNetlist = 2.0 * std::sqrt (aS * aS + aC * aC) / (double) len;
                const double ampProc = 2.0 * std::sqrt (bS * bS + bC * bC) / (double) len;
                logMessage ("input " + juce::String (amp, 3) + " V: netlist=" + juce::String (ampNetlist, 5)
                            + " processor=" + juce::String (ampProc, 5)
                            + " diff=" + juce::String (100.0 * std::abs (ampNetlist - ampProc) / ampProc, 3) + "%");
                expectWithinAbsoluteError (ampNetlist, ampProc, 0.02 * ampProc);
            }
        }

        beginTest ("dual-mono shortcut: identical channels give identical output, and resync is exact when channels diverge");
        {
            // Stream A: stereo, dual-mono for 6 blocks, then two different channels for 6 blocks, then dual-mono again.
            // Reference L / R: the same signals through separate MONO processors (no shortcut ever taken).
            auto check = [this] (auto makeProcessor, const char* name)
            {
                auto stereo = makeProcessor();
                auto refL = makeProcessor();
                auto refR = makeProcessor();
                stereo->prepare (sr, 512, 2);
                refL->prepare (sr, 512, 1);
                refR->prepare (sr, 512, 1);

                double maxErr = 0.0;
                for (int b = 0; b < 18; ++b)
                {
                    juce::AudioBuffer<float> st (2, 512), l (1, 512), r (1, 512);
                    const bool dual = b < 6 || b >= 12;
                    for (int i = 0; i < 512; ++i)
                    {
                        const double t = (double) (b * 512 + i) / sr;
                        const float x = (float) (0.02 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t));
                        const float y = dual ? x : (float) (0.02 * std::sin (2.0 * juce::MathConstants<double>::pi * 517.0 * t));
                        st.setSample (0, i, x); st.setSample (1, i, y);
                        l.setSample (0, i, x); r.setSample (0, i, y);
                    }
                    stereo->process (st);
                    refL->process (l);
                    refR->process (r);
                    for (int i = 0; i < 512; ++i)
                    {
                        maxErr = juce::jmax (maxErr, (double) std::abs (st.getSample (0, i) - l.getSample (0, i)));
                        maxErr = juce::jmax (maxErr, (double) std::abs (st.getSample (1, i) - r.getSample (0, i)));
                    }
                }
                logMessage (juce::String (name) + ": max |stereo - reference| = " + juce::String (maxErr, 9));
                expectLessThan (maxErr, 1.0e-6);
            };

            check ([] { return std::make_unique<CentaurStyleOverdriveProcessor>(); }, "Centaur");
            check ([] { return std::make_unique<BD2StyleOverdriveProcessor>(); }, "BD-2");
            check ([] { return std::make_unique<HM2StyleDistortionProcessor>(); }, "HM-2");
        }

        beginTest ("cost: the 21-unknown TS808 netlist, time per sample (informational)");
        {
            NodalCircuit c;
            const auto vcc = c.addNode(), bias = c.addNode(), in = c.addNode();
            const auto n1 = c.addNode(), nb = c.addNode(), ne = c.addNode(), np = c.addNode();
            const auto nm = c.addNode(), nc3 = c.addNode(), nf = c.addNode(), o1 = c.addNode();
            const auto nA = c.addNode(), nB = c.addNode(), nW = c.addNode(), nw6 = c.addNode(), o2 = c.addNode();
            const auto n7 = c.addNode(), nt = c.addNode(), nlw = c.addNode(), nsw = c.addNode();
            const auto nqb = c.addNode(), nqe = c.addNode(), nc9 = c.addNode(), out = c.addNode();
            c.addSource (vcc, 9.0);
            c.addSource (bias, 4.5);
            const int src = c.addSource (in, 0.0);
            const NodalCircuit::BjtParams npn { 1.0e-14, 25.85e-3, 300.0, 4.0 };
            const auto gnd = NodalCircuit::ground;
            c.addCapacitor (in, n1, 0.02e-6);   c.addResistor (n1, nb, 1.0e3);   c.addResistor (nb, bias, 510.0e3);
            c.addBjt (vcc, nb, ne, false, npn); c.addResistor (ne, gnd, 10.0e3);
            c.addCapacitor (ne, np, 1.0e-6);    c.addResistor (np, bias, 10.0e3);
            c.addOpAmp (np, nm, o1);
            c.addCapacitor (nm, nc3, 0.047e-6); c.addResistor (nc3, gnd, 4.7e3);
            c.addResistor (nm, nf, 51.0e3);     c.addResistor (nf, o1, 125.0e3);
            c.addCapacitor (nm, o1, 51.0e-12);
            c.addDiode (nm, o1, 2.52e-9, 45.3e-3); c.addDiode (o1, nm, 2.52e-9, 45.3e-3);
            c.addResistor (o1, nA, 1.0e3);      c.addCapacitor (nA, gnd, 0.22e-6);   c.addResistor (nA, bias, 10.0e3);
            c.addOpAmp (nA, nB, o2);            c.addResistor (nB, o2, 1.0e3);
            c.addResistor (nA, nW, 10.0e3);     c.addResistor (nW, nB, 10.0e3);
            c.addResistor (nW, nw6, 220.0);     c.addCapacitor (nw6, gnd, 0.22e-6);
            c.addCapacitor (o2, n7, 1.0e-6);    c.addResistor (n7, nt, 1.0e3);
            c.addResistor (nt, nlw, 75.0e3);    c.addResistor (nlw, gnd, 25.0e3);
            c.addResistor (nlw, nsw, 100.0);    c.addCapacitor (nsw, nqb, 0.1e-6);   c.addResistor (nqb, bias, 510.0e3);
            c.addBjt (vcc, nqb, nqe, false, npn); c.addResistor (nqe, gnd, 10.0e3);
            c.addResistor (nqe, nc9, 100.0);    c.addCapacitor (nc9, out, 10.0e-6);
            c.addResistor (out, gnd, 9.9e3);
            expect (c.prepare (sr));

            long long failures = 0;
            const long long n = 48000;
            const auto t0 = std::chrono::steady_clock::now();
            for (long long i = 0; i < n; ++i)
            {
                c.setSource (src, 0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sr));
                if (! c.solveSample())
                    ++failures;
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double usPerSample = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) n;
            logMessage ("21-unknown TS808 netlist: " + juce::String (usPerSample, 2) + " us/sample = "
                        + juce::String (100.0 * usPerSample * sr * 1.0e-6, 1) + "% of one core at 48 kHz; "
                        + juce::String (failures) + " non-converged samples; avg Newton iterations/sample = "
                        + juce::String (c.averageIterations(), 2));
            expectEquals (failures, (long long) 0);
        }

        beginTest ("cost: nonlinear block time per sample (informational)");
        {
            NodalCircuit c;
            const auto in = c.addNode(), inv = c.addNode(), mid = c.addNode(), out = c.addNode();
            const int src = c.addSource (in, 0.0);
            c.addOpAmp (in, inv, out);
            c.addResistor (inv, out, 100.0e3);
            c.addCapacitor (inv, mid, 47.0e-9);
            c.addResistor (mid, NodalCircuit::ground, 4.7e3);
            c.addDiode (inv, out, 2.52e-9, 45.3e-3);
            c.addDiode (out, inv, 2.52e-9, 45.3e-3);
            expect (c.prepare (sr));

            const long long n = 48000;
            const auto t0 = std::chrono::steady_clock::now();
            for (long long i = 0; i < n; ++i)
            {
                c.setSource (src, 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / sr));
                c.solveSample();
            }
            const auto t1 = std::chrono::steady_clock::now();
            logMessage ("4-unknown op-amp+diode block: " + juce::String (std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) n, 2)
                        + " us/sample");
        }
    }
};

static NodalCircuitTests nodalCircuitTests;

} // namespace openguitarmultifx
