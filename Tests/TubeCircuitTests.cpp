#include "Effects/NodalCircuit.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

class TubeCircuitTests : public juce::UnitTest
{
public:
    TubeCircuitTests() : juce::UnitTest ("TubeCircuit", "Effects") {}

    static constexpr double sr = 48000.0;

    static double sineGain (NodalCircuit& c, int src, NodalCircuit::Node outNode, double freq, double amp,
                            double dcIn, double dcOut, double warmup = 0.4, double measure = 0.2)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) ((warmup + measure) * sr);
        const long long len = (long long) std::llround (std::floor (measure * freq) * sr / freq);
        const long long start = total - len;
        double sumS = 0.0, sumC = 0.0;
        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            c.setSource (src, dcIn + amp * std::sin (ph));
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
        beginTest ("common-cathode 12AX7 stage: bias point and gain");
        {
            // 250 V, 100k plate load, 1.5k cathode bypassed by 25 uF: the textbook stage (~1.2 mA, gain ~ -60).
            NodalCircuit c;
            const auto vcc = c.addNode(), plate = c.addNode(), grid = c.addNode(), cath = c.addNode(), in = c.addNode();
            c.addSource (vcc, 250.0);
            const int src = c.addSource (in, 0.0);
            c.addResistor (vcc, plate, 100.0e3);
            c.addResistor (cath, NodalCircuit::ground, 1.5e3);
            c.addCapacitor (cath, NodalCircuit::ground, 25.0e-6);
            c.addCapacitor (in, grid, 22.0e-9);
            c.addResistor (grid, NodalCircuit::ground, 1.0e6);
            c.addTriode (plate, grid, cath);
            expect (c.prepare (sr));
            logMessage ("plate " + juce::String (c.voltage (plate)) + " V, cathode " + juce::String (c.voltage (cath)) + " V");
            const double ip = c.voltage (cath) / 1.5e3;
            expect (ip > 0.5e-3 && ip < 2.5e-3, "plate current " + juce::String (ip * 1e3) + " mA");
            const double g = sineGain (c, src, plate, 1000.0, 0.05, 0.0, c.voltage (plate));
            logMessage ("gain " + juce::String (g));
            expect (g > 45.0 && g < 75.0, "gain " + juce::String (g));
        }

        beginTest ("coupled inductors behave as a transformer");
        {
            // 1 H : 4 H windings (ratio 1:2), k = 0.999, secondary loaded by 10k: mid-band voltage gain ~ 2 * 10k/(10k+small).
            NodalCircuit c;
            const auto in = c.addNode(), p = c.addNode(), s = c.addNode();
            const int src = c.addSource (in, 0.0);
            c.addResistor (in, p, 100.0);
            c.addResistor (s, NodalCircuit::ground, 10.0e3);
            const double k = 0.999, l1 = 1.0, l2 = 4.0, m = k * std::sqrt (l1 * l2);
            c.addCoupledInductors ({ { p, NodalCircuit::ground }, { s, NodalCircuit::ground } }, { l1, m, m, l2 });
            expect (c.prepare (sr));
            const double g1k = sineGain (c, src, s, 1000.0, 1.0, 0.0, 0.0);
            logMessage ("transformer gain at 1 kHz " + juce::String (g1k));
            expect (g1k > 1.8 && g1k < 2.05, "gain " + juce::String (g1k));
            const double g10 = sineGain (c, src, s, 10.0, 1.0, 0.0, 0.0, 1.0, 0.5);
            expect (g10 < g1k * 0.85, "low-frequency roll-off");
        }

        beginTest ("current source injects into a node");
        {
            NodalCircuit c;
            const auto n = c.addNode();
            c.addResistor (n, NodalCircuit::ground, 1000.0);
            const int i = c.addCurrentSource (n, 0.002);
            expect (c.prepare (sr));
            c.solveSample();
            expectWithinAbsoluteError (c.voltage (n), 2.0, 1.0e-6);
            c.setCurrentSource (i, -0.001);
            c.solveSample();
            expectWithinAbsoluteError (c.voltage (n), -1.0, 1.0e-6);
        }



        beginTest ("table-based tube models agree with the formulas");
        {
            KorenTriode tri;
            double worst = 0.0;
            for (double vgk = -6.0; vgk <= 1.5; vgk += 0.137)
                for (double vpk = 5.0; vpk <= 450.0; vpk += 7.3)
                {
                    const auto a = tri.evaluate (vgk, vpk), b = tri.evaluateExact (vgk, vpk);
                    const double scale = std::abs (b.ip) + 1.0e-6;
                    worst = std::max (worst, std::abs (a.ip - b.ip) / scale);
                    worst = std::max (worst, std::abs (a.dip_dvgk - b.dip_dvgk) / (std::abs (b.dip_dvgk) + 1.0e-6));
                    worst = std::max (worst, std::abs (a.dip_dvpk - b.dip_dvpk) / (std::abs (b.dip_dvpk) + 1.0e-7));
                }
            logMessage ("triode worst relative error " + juce::String (worst));
            expect (worst < 1.0e-3, "triode table error " + juce::String (worst));
        }

        beginTest ("probe: analytic derivatives");
        {
            KorenPentode pen;
            KorenTriode tri;
            for (double vgk : { -60.0, -48.0, -30.0, -10.0, 0.0, 2.0 })
                for (double vpk : { 20.0, 100.0, 300.0, 450.0 })
                {
                    const double h = 1.0e-4;
                    const auto o = pen.evaluate (vgk, vpk, 430.0);
                    const double ngm = (pen.evaluate (vgk + h, vpk, 430.0).ip - pen.evaluate (vgk - h, vpk, 430.0).ip) / (2 * h);
                    const double ngp = (pen.evaluate (vgk, vpk + h, 430.0).ip - pen.evaluate (vgk, vpk - h, 430.0).ip) / (2 * h);
                    const double ngg = (pen.evaluate (vgk + h, vpk, 430.0).ig - pen.evaluate (vgk - h, vpk, 430.0).ig) / (2 * h);
                    const double tol = 1.0e-3 * (std::abs (o.dip_dvgk) + std::abs (o.dip_dvpk)) + 1.0e-12;
                    if (std::abs (ngm - o.dip_dvgk) > tol || std::abs (ngp - o.dip_dvpk) > tol || std::abs (ngg - o.dig_dvgk) > 1.0e-3 * std::abs (o.dig_dvgk) + 1.0e-12)
                        logMessage ("PENTODE mismatch vgk " + juce::String (vgk) + " vpk " + juce::String (vpk) + ": " + juce::String (ngm, 6) + " vs " + juce::String (o.dip_dvgk, 6) + " | " + juce::String (ngp, 6) + " vs " + juce::String (o.dip_dvpk, 6));
                    const auto t = tri.evaluate (vgk / 20.0, vpk);
                    const double tgm = (tri.evaluate (vgk / 20.0 + h, vpk).ip - tri.evaluate (vgk / 20.0 - h, vpk).ip) / (2 * h);
                    const double tgp = (tri.evaluate (vgk / 20.0, vpk + h).ip - tri.evaluate (vgk / 20.0, vpk - h).ip) / (2 * h);
                    const double ttol = 1.0e-3 * (std::abs (t.dip_dvgk) + std::abs (t.dip_dvpk)) + 1.0e-12;
                    if (std::abs (tgm - t.dip_dvgk) > ttol || std::abs (tgp - t.dip_dvpk) > ttol)
                        logMessage ("TRIODE mismatch vgk " + juce::String (vgk / 20.0) + " vpk " + juce::String (vpk) + ": " + juce::String (tgm, 6) + " vs " + juce::String (t.dip_dvgk, 6) + " | " + juce::String (tgp, 6) + " vs " + juce::String (t.dip_dvpk, 6));
                }
        }

        beginTest ("probe: push-pull output stage DC");
        {
            for (int variant = 0; variant < 3; ++variant)
            {
                NodalCircuit c;
                const auto ct = c.addNode(), bias = c.addNode();
                c.addSource (ct, 452.0);
                c.addSource (bias, -48.0);
                const auto g1 = c.addNode(), g2 = c.addNode(), p1 = c.addNode(), p2 = c.addNode(), a1 = c.addNode(), a2 = c.addNode();
                const auto sw = c.addNode(), out = c.addNode();
                c.addResistor (g1, bias, 220.0e3);
                c.addResistor (g2, bias, 220.0e3);
                c.addPentode (p1, g1, NodalCircuit::ground, {}, 448.0);
                c.addPentode (p2, g2, NodalCircuit::ground, {}, 448.0);
                c.addResistor (ct, a1, 45.0);
                c.addResistor (ct, a2, 45.0);
                const double lh = 6.25, ls = lh / (22.36 * 22.36), m12 = -0.9997 * lh, mps = 0.9992 * std::sqrt (lh * ls);
                if (variant == 0)
                {
                    c.addResistor (a1, p1, 1.0);
                    c.addResistor (a2, p2, 1.0);
                    c.addResistor (sw, out, 0.06);
                    c.addResistor (out, NodalCircuit::ground, 2.0);
                }
                else if (variant == 1)
                {
                    c.addCoupledInductors ({ { a1, p1 }, { a2, p2 } }, { lh, m12, m12, lh });
                    c.addResistor (sw, out, 0.06);
                    c.addResistor (out, NodalCircuit::ground, 2.0);
                    c.addResistor (sw, NodalCircuit::ground, 1.0e6);
                }
                else
                {
                    c.addCoupledInductors ({ { a1, p1 }, { a2, p2 }, { sw, NodalCircuit::ground } }, { lh, m12, -mps, m12, lh, mps, -mps, mps, ls });
                    c.addResistor (sw, out, 0.06);
                    c.addResistor (out, NodalCircuit::ground, 2.0);
                }
                c.addCapacitor (g1, NodalCircuit::ground, 1.0e-9);
                c.setInitialGuess (p1, 452.0);
                c.setInitialGuess (p2, 452.0);
                c.setInitialGuess (a1, 452.0);
                c.setInitialGuess (a2, 452.0);
                c.setInitialGuess (g1, -48.0);
                c.setInitialGuess (g2, -48.0);
                const bool ok = c.prepare (sr);
                logMessage ("variant " + juce::String (variant) + " ok=" + juce::String ((int) ok) + " p1 " + juce::String (c.voltage (p1)) + " p2 " + juce::String (c.voltage (p2)) + " g1 " + juce::String (c.voltage (g1)));
            }
        }


        beginTest ("probe: cost of one triode stage per sample");
        {
            NodalCircuit c;
            const auto vcc = c.addNode(), plate = c.addNode(), grid = c.addNode(), cath = c.addNode(), in = c.addNode();
            c.addSource (vcc, 250.0);
            const int src = c.addSource (in, 0.0);
            c.addResistor (vcc, plate, 100.0e3);
            c.addResistor (cath, NodalCircuit::ground, 1.5e3);
            c.addCapacitor (cath, NodalCircuit::ground, 25.0e-6);
            c.addCapacitor (in, grid, 22.0e-9);
            c.addResistor (grid, NodalCircuit::ground, 1.0e6);
            c.addTriode (plate, grid, cath);
            c.prepare (sr);
            const long long n = 200000;
            const auto t0 = std::chrono::steady_clock::now();
            double acc = 0.0;
            for (long long i = 0; i < n; ++i)
            {
                c.setSource (src, 0.05 * std::sin (0.13 * (double) i));
                c.solveSample();
                acc += c.voltage (plate);
            }
            const double ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (double) n;
            logMessage ("one triode stage: " + juce::String (ns) + " ns/sample (" + juce::String (ns * 3.0) + " cycles @3GHz) iter " + juce::String (c.averageIterations()) + " acc " + juce::String (acc));
        }


        beginTest ("probe: cost of model evaluations");
        {
            KorenTriode tri;
            KorenPentode pen;
            const long long n = 2000000;
            double acc = 0.0;
            auto t0 = std::chrono::steady_clock::now();
            for (long long i = 0; i < n; ++i)
            {
                const auto o = tri.evaluate (-1.5 + 0.001 * (double) (i % 100), 150.0 + 0.01 * (double) (i % 1000));
                acc += o.ip + o.dip_dvgk;
            }
            const double nsT = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (double) n;
            t0 = std::chrono::steady_clock::now();
            for (long long i = 0; i < n; ++i)
            {
                const auto o = pen.evaluate (-48.0 + 0.001 * (double) (i % 100), 300.0 + 0.01 * (double) (i % 1000), 430.0);
                acc += o.ip + o.dip_dvgk;
            }
            const double nsP = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (double) n;
            logMessage ("triode evaluate " + juce::String (nsT) + " ns, pentode " + juce::String (nsP) + " ns (acc " + juce::String (acc) + ")");
        }


        beginTest ("beam tetrode: bias point");
        {
            NodalCircuit c;
            const auto vb = c.addNode(), plate = c.addNode(), grid = c.addNode(), bias = c.addNode();
            c.addSource (vb, 450.0);
            c.addSource (bias, -48.0);
            c.addResistor (vb, plate, 1.0e3);
            c.addResistor (grid, bias, 220.0e3);
            KorenPentode::Parameters p;
            const int h = c.addPentode (plate, grid, NodalCircuit::ground, p, 430.0);
            expect (c.prepare (sr));
            c.solveSample();
            logMessage ("pentode plate " + juce::String (c.voltage (plate)) + " V, screen " + juce::String (c.pentodeScreenCurrent (h) * 1e3) + " mA");
            const double ip = (450.0 - c.voltage (plate)) / 1.0e3;
            expect (ip > 10.0e-3 && ip < 60.0e-3, "plate current " + juce::String (ip * 1e3) + " mA");
        }
    }
};

static TubeCircuitTests tubeCircuitTests;

} // namespace openguitarmultifx
