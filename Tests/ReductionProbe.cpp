#include "Effects/NodalCircuit.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{
class ReductionProbe : public juce::UnitTest
{
public:
    ReductionProbe() : juce::UnitTest ("ReductionProbe", "Bench") {}

    void runTest() override
    {
        beginTest ("BD-2 stage: open-loop response");
        if (juce::SystemStats::getEnvironmentVariable ("REDUCTION_PROBE", {}).isEmpty())
            return;
        const double fs = 192000.0;
        const NodalCircuit::JfetParams jfet { 4.5e-3, -1.2, 0.02 };
        const NodalCircuit::BjtParams pnp { 1.0e-14, 25.85e-3, 200.0, 4.0 };
        const double vcc = 8.0, vb = 4.0;
        for (double stage : { 1.0, 2.0 })
        {
            const double r28 = 2.2e3, c21 = stage == 1.0 ? 47e-12 : 100e-12;
            // find g11 (DC) that makes c9 = ~4.16 V by bisection on the DC solution
            double lo = 3.5, hi = 4.8, best = 4.2;
            for (int it = 0; it < 30; ++it)
            {
                best = 0.5 * (lo + hi);
                NodalCircuit c;
                const auto v8 = c.addNode(), g10 = c.addNode(), d10 = c.addNode(), s10 = c.addNode(), g11 = c.addNode(), c9 = c.addNode();
                c.addSource (v8, vcc); c.addSource (g10, vb); c.addSource (g11, best);
                c.addJfet (d10, g10, s10, jfet); c.addResistor (v8, d10, r28); c.addResistor (s10, NodalCircuit::ground, 4.7e3);
                c.addJfet (v8, g11, s10, jfet, 5.0); c.addBjt (c9, d10, v8, true, pnp);
                c.addCapacitor (d10, c9, c21); c.addResistor (c9, NodalCircuit::ground, 2.2e3); c.addResistor (c9, NodalCircuit::ground, 80.0e3);
                c.setInitialGuess (d10, 7.3); c.setInitialGuess (s10, 4.9); c.setInitialGuess (c9, 4.2);
                c.prepare (fs);
                const double v = c.voltage (c9);
                if (v > 4.16) lo = best; else hi = best; // higher g11 -> lower output (inverting input)
            }
            NodalCircuit c;
            const auto v8 = c.addNode(), g10 = c.addNode(), d10 = c.addNode(), s10 = c.addNode(), g11 = c.addNode(), c9 = c.addNode();
            c.addSource (v8, vcc); const int src = c.addSource (g10, vb); c.addSource (g11, best);
            c.addJfet (d10, g10, s10, jfet); c.addResistor (v8, d10, r28); c.addResistor (s10, NodalCircuit::ground, 4.7e3);
            c.addJfet (v8, g11, s10, jfet, 5.0); c.addBjt (c9, d10, v8, true, pnp);
            c.addCapacitor (d10, c9, c21); c.addResistor (c9, NodalCircuit::ground, 2.2e3); c.addResistor (c9, NodalCircuit::ground, 80.0e3);
            c.setInitialGuess (d10, 7.3); c.setInitialGuess (s10, 4.9); c.setInitialGuess (c9, 4.2);
            c.prepare (fs);
            logMessage ("stage " + juce::String (stage, 0) + ": g11 DC " + juce::String (best, 4) + ", c9 DC " + juce::String (c.voltage (c9), 4) + ", d10 " + juce::String (c.voltage (d10), 3));
            for (double f : { 20.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0, 30000.0, 100000.0 })
            {
                NodalCircuit cc = c;
                const double a = 20.0e-6;
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                const long long total = (long long) (0.15 * fs) + 4000, len = (long long) std::llround (std::floor (0.05 * f) * fs / f);
                double ss = 0, cs = 0;
                const double dc = cc.voltage (c9);
                for (long long n = 0; n < total; ++n)
                {
                    const double ph = twoPi * f * (double) n / fs;
                    cc.setSource (src, vb + a * std::sin (ph));
                    cc.solveSample();
                    if (n >= total - std::max<long long> (len, 400))
                    {
                        const double y = cc.voltage (c9) - dc;
                        ss += y * std::sin (ph); cs += y * std::cos (ph);
                    }
                }
                const long long cnt = std::max<long long> (len, 400);
                const double mag = 2.0 * std::sqrt (ss * ss + cs * cs) / (double) cnt / a;
                const double phase = std::atan2 (cs, ss) * 180.0 / juce::MathConstants<double>::pi;
                logMessage ("  f " + juce::String (f, 0) + " Hz: |A| " + juce::String (mag, 1) + " (" + juce::String (20.0 * std::log10 (mag), 1) + " dB) phase " + juce::String (phase, 0));
            }
        }
    }
};
static ReductionProbe reductionProbe;
}
