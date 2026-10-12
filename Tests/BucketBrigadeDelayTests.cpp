#include "Effects/Modulation/Chorus/BucketBrigadeDelay.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/** Checks the Holters & Parker BBD model against the analytic case the paper itself validates with (DAFx-18 sec. 4):
    the Juno-60's BBD filters, a constant clock, a sine. */
class BucketBrigadeDelayTests : public juce::UnitTest
{
public:
    BucketBrigadeDelayTests() : juce::UnitTest ("BucketBrigadeDelay", "Effects") {}

    using C = std::complex<double>;

    // Table 1 of the paper (poles in rad/s); DC gains from its residues.
    static BucketBrigadeDelay::Filter junoIn()
    {
        return { { C (-46580, 0), C (-55482, 25082), C (-55482, -25082), C (-26292, -59437), C (-26292, 59437) }, 0.9678 };
    }
    static BucketBrigadeDelay::Filter junoOut()
    {
        return { { C (-176261, 0), C (-51468, 21437), C (-51468, -21437), C (-26276, -59699), C (-26276, 59699) }, 0.9138 };
    }

    static C response (const BucketBrigadeDelay::Filter& f, double hz)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * hz);
        C prodNeg = 1.0, h = 1.0;
        for (const auto& p : f.poles)
        {
            prodNeg *= -p;
            h /= (s - p);
        }
        return f.dcGain * prodNeg * h;
    }

    void runTest() override
    {
        const double pi = juce::MathConstants<double>::pi;

        beginTest ("constant clock: amplitude and phase match the analytic BBD + filters response (paper eq. 36-38)");
        {
            const double fs = 44100.0, f0 = 1000.0, fBbd = 50000.0;
            const int stages = 256;
            BucketBrigadeDelay bbd;
            bbd.prepare (fs, stages, junoIn(), junoOut());

            const C hIn = response (junoIn(), f0), hOut = response (junoOut(), f0);
            const double x = f0 / fBbd;
            const double sinc = std::sin (pi * x) / (pi * x);
            const double a = sinc * std::abs (hIn) * std::abs (hOut);
            const double phi = -pi * f0 * stages / fBbd + std::arg (hIn) + std::arg (hOut);

            double maxErr = 0.0;
            const int n = (int) fs;
            for (int k = 0; k < n; ++k)
            {
                const double y = bbd.process (std::sin (2.0 * pi * f0 * k / fs), fBbd);
                if (k > n / 2)
                    maxErr = std::max (maxErr, std::abs (y - a * std::sin (2.0 * pi * f0 * k / fs + phi)));
            }
            logMessage ("expected amplitude " + juce::String (a, 4) + ", max |model - analytic| " + juce::String (maxErr, 6));
            // The paper's own figure 7b shows ~1e-3 (aliasing from the filters' finite attenuation at Nyquist).
            expectLessThan (maxErr, 3.0e-3);
        }

        beginTest ("a step in the clock bends the pitch smoothly (no discontinuity like a digital delay jump)");
        {
            const double fs = 44100.0, f0 = 1000.0;
            BucketBrigadeDelay bbd;
            bbd.prepare (fs, 256, junoIn(), junoOut());
            double prev = 0.0, maxStep = 0.0;
            for (int k = 0; k < (int) (0.02 * fs); ++k)
            {
                const double clock = k < (int) (0.005 * fs) ? 50000.0 : 25000.0;
                const double y = bbd.process (std::sin (2.0 * pi * f0 * k / fs), clock);
                if (k > (int) (0.004 * fs))
                    maxStep = std::max (maxStep, std::abs (y - prev));
                prev = y;
            }
            // A full-scale 1 kHz sine moves at most 2*pi*1000/44100 = 0.142 per sample; a jump would be far larger.
            logMessage ("largest sample-to-sample step after the clock step: " + juce::String (maxStep, 4));
            expectLessThan (maxStep, 0.16);
        }

        beginTest ("delay equals N/2 clock periods");
        {
            BucketBrigadeDelay bbd;
            bbd.prepare (48000.0, 1024, junoIn(), junoOut());
            expectWithinAbsoluteError (bbd.delaySeconds (100000.0), 512.0 / 100000.0, 1.0e-12);
            expectWithinAbsoluteError (bbd.clockForDelay (0.005), 512.0 / 0.005, 1.0e-6);
        }

        beginTest ("cost: a chorus-range clock on a 1024-stage chip");
        {
            BucketBrigadeDelay bbd;
            bbd.prepare (48000.0, 1024, junoIn(), junoOut());
            const int n = 48000 * 2;
            double sink = 0.0;
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < n; ++k)
                sink += bbd.process (std::sin (0.01 * k), 100000.0 + 40000.0 * std::sin (2.0e-4 * k));
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            logMessage ("BBD cost: " + juce::String (100.0 * secs / 2.0, 2) + "% of a core (sink " + juce::String (sink, 1) + ")");
            expectLessThan (100.0 * secs / 2.0, 5.0);
        }
    }
};

static BucketBrigadeDelayTests bucketBrigadeDelayTests;

} // namespace openguitarmultifx
