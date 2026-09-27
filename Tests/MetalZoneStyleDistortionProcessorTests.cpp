#include "Effects/MetalZoneStyleDistortionProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <complex>

#include "Effects/PotTaper.h"

namespace openguitarmultifx
{

class MetalZoneStyleDistortionProcessorTests : public juce::UnitTest
{
public:
    MetalZoneStyleDistortionProcessorTests() : juce::UnitTest ("MetalZoneStyleDistortionProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using Probe = MetalZoneStyleDistortionProcessor::Probe;

    struct K { float dist = 0.5f, high = 0.5f, middle = 0.5f, midFreq = 0.5f, low = 0.5f, level = 0.5f; };

    static void setKnobs (MetalZoneStyleDistortionProcessor& p, const K& k)
    {
        const float v[6] = { k.dist, k.high, k.middle, k.midFreq, k.low, k.level };
        int i = 0;
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && i < 6)
                *f = v[i++];
    }

    /** Peak-to-peak of the last two periods at the output, or at a probe. */
    static double steadyPeakToPeak (MetalZoneStyleDistortionProcessor& p, double freq, double amplitude, int probe = -1, int periods = 200)
    {
        juce::AudioBuffer<float> buf (1, 1);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        const int total = (int) (periods * sr / freq);
        const int tail = (int) (2.0 * sr / freq) + 8;
        for (int n = 0; n < total + tail; ++n)
        {
            phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
            buf.setSample (0, 0, (float) (amplitude * std::sin (phase)));
            p.process (buf);
            if (n >= total)
            {
                const double v = probe < 0 ? (double) buf.getSample (0, 0) : p.debugVoltage ((Probe) probe);
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
        }
        return hi - lo;
    }

    static double outDb (const K& k, double freq, double amplitude = 0.0001, int probe = -1)
    {
        MetalZoneStyleDistortionProcessor p;
        p.prepare (sr, 128, 1);
        setKnobs (p, k);
        return 20.0 * std::log10 (juce::jmax (1.0e-12, steadyPeakToPeak (p, freq, amplitude, probe, freq < 300.0 ? 30 : 120)));
    }

    void runTest() override
    {
        beginTest ("DC operating point: every op-amp output sits at the 4.5 V reference, the jack at 0 V");
        {
            MetalZoneStyleDistortionProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            for (auto pr : { Probe::ic3b, Probe::ic3a, Probe::ic4b, Probe::ic4a, Probe::ic1a, Probe::ic2a })
                expectWithinAbsoluteError (p.debugVoltage (pr), 4.5, 0.15);
            expectWithinAbsoluteError (p.debugVoltage (Probe::output), 0.0, 0.05);
            logMessage ("IC3b " + juce::String (p.debugVoltage (Probe::ic3b), 3) + ", IC3a " + juce::String (p.debugVoltage (Probe::ic3a), 3)
                        + ", IC4b " + juce::String (p.debugVoltage (Probe::ic4b), 3) + ", IC4a " + juce::String (p.debugVoltage (Probe::ic4a), 3)
                        + ", IC1a " + juce::String (p.debugVoltage (Probe::ic1a), 3) + ", IC2a " + juce::String (p.debugVoltage (Probe::ic2a), 3) + " V");
        }

        beginTest ("IC3a (Dist) gain against the closed form for a finite-GBW op-amp, with its input network");
        {
            for (float dist : { 0.2f, 0.5f, 0.8f })
                for (double freq : { 400.0, 1600.0 })
                {
                    K k; k.dist = dist;
                    const double measured = std::pow (10.0, (outDb (k, freq, 0.0001, (int) Probe::ic3a) - outDb (k, freq, 0.0001, (int) Probe::ic3b)) / 20.0);

                    const std::complex<double> j (0.0, 1.0);
                    const double w = 2.0 * juce::MathConstants<double>::pi * freq;
                    const std::complex<double> zc42 = 1.0 / (j * w * 0.047e-6);
                    const std::complex<double> zc29 = 1.0 / (j * w * 0.033e-6);
                    const std::complex<double> zp = 1.0 / (1.0 / 10.0e3 + 1.0 / zc42 + 1.0 / (100.0e3 + zc29)); // R042 || C031 || (C029 + R040)
                    const std::complex<double> vp = zp / (75.0 + 10.0e3 + zp);
                    const std::complex<double> vq = vp * (100.0e3 / (100.0e3 + zc29));
                    const double rf = 250.0e3 * pots::audio (dist) + 1.0e3;
                    const std::complex<double> zf = 1.0 / (1.0 / rf + j * w * 47.0e-12);
                    const std::complex<double> zg = 1.0e3 + 1.0 / (j * w * 10.0e-6);
                    const std::complex<double> ideal = 1.0 + zf / zg;
                    const std::complex<double> a = 1.0e5 / (1.0 + j * w * 1.0e5 / (2.0 * juce::MathConstants<double>::pi * 2.5e6));
                    const double expected = std::abs (vq * ideal / (1.0 + ideal / a));
                    logMessage ("Dist " + juce::String (dist, 1) + ", " + juce::String (freq, 0) + " Hz: measured " + juce::String (measured, 3) + "x, closed form " + juce::String (expected, 3) + "x");
                    expectWithinAbsoluteError (20.0 * std::log10 (measured / expected), 0.0, 0.3);
                }
        }

        beginTest ("IC3b's gain is a band-pass hump: near unity at the extremes, tens of dB in the mids (the Q010 bootstrapped branch)");
        {
            K k; k.dist = 0.0f;
            const double lowG = outDb (k, 50.0, 0.00002, (int) Probe::ic3b) - 20.0 * std::log10 (0.00004);
            const double midG = outDb (k, 800.0, 0.00002, (int) Probe::ic3b) - 20.0 * std::log10 (0.00004);
            const double highG = outDb (k, 12000.0, 0.00002, (int) Probe::ic3b) - 20.0 * std::log10 (0.00004);
            logMessage ("IC3b gain: 50 Hz " + juce::String (lowG, 1) + " dB, 800 Hz " + juce::String (midG, 1) + " dB, 12 kHz " + juce::String (highG, 1) + " dB");
            expect (lowG < 3.0 && midG > 25.0 && highG < midG - 15.0, "hump in the mids");
        }

        beginTest ("Dist raises the linear gain; Level raises the output monotonically");
        {
            double prev = -1.0e9;
            for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                K k; k.dist = d;
                const double v = outDb (k, 400.0, 0.00002);
                expect (v > prev, "gain rises with Dist");
                prev = v;
            }
            prev = -1.0e9;
            for (float l : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
            {
                K k; k.level = l;
                const double v = outDb (k, 400.0, 0.05);
                expect (v > prev, "output rises with Level");
                prev = v;
            }
        }

        beginTest ("High, Low, Middle and Mid Freq move their own bands the way the labels say (measured on the network)");
        {
            K base; base.dist = 0.0f;
            auto with = [&] (float K::*member, float v) { K k = base; k.*member = v; return k; };
            const double hiUp = outDb (with (&K::high, 1.0f), 6400.0) - outDb (with (&K::high, 0.0f), 6400.0);
            const double loUp = outDb (with (&K::low, 1.0f), 100.0) - outDb (with (&K::low, 0.0f), 100.0);
            const double midUp = outDb (with (&K::middle, 1.0f), 400.0) - outDb (with (&K::middle, 0.0f), 400.0);
            logMessage ("High 1 vs 0 at 6.4 kHz: " + juce::String (hiUp, 1) + " dB; Low 1 vs 0 at 100 Hz: " + juce::String (loUp, 1)
                        + " dB; Middle 1 vs 0 at 400 Hz: " + juce::String (midUp, 1) + " dB");
            expect (hiUp > 10.0 && loUp > 10.0 && midUp > 10.0, "each control has a wide range in its own band");

            // Mid Freq: with Middle at 1, the boost moves from ~400 Hz (knob 0.75) to above 1.6 kHz (knob 1)
            K m = base; m.middle = 1.0f;
            auto boost = [&] (float mf, double f) { K k = m; k.midFreq = mf; return outDb (k, f) - outDb (base, f); };
            const double lowMf = boost (0.0f, 400.0) - boost (0.0f, 3200.0), highMf = boost (1.0f, 400.0) - boost (1.0f, 3200.0);
            logMessage ("Mid boost, 400 Hz relative to 3.2 kHz: Mid Freq 0 = " + juce::String (lowMf, 1) + " dB, Mid Freq 1 = " + juce::String (highMf, 1) + " dB");
            expect (lowMf > highMf + 6.0, "Mid Freq clockwise moves the mid boost up in frequency");
        }

        if (juce::SystemStats::getEnvironmentVariable ("MZ_EXPLORE", {}).isNotEmpty())
        {
            beginTest ("explore (dev only)");
            const double freqs[] = { 50, 100, 200, 400, 800, 1600, 3200, 6400, 12000 };
            {
                K k; k.dist = 0.0f;
                juce::String l2;
                for (auto f : freqs)
                    l2 << juce::String (f, 0) << ": prev->P " << juce::String (outDb (k, f, 0.0001, (int) Probe::ic3aP) - outDb (k, f, 0.0001, (int) Probe::ic3aPrev), 2)
                       << " P->Q " << juce::String (outDb (k, f, 0.0001, (int) Probe::ic3aQ) - outDb (k, f, 0.0001, (int) Probe::ic3aP), 2) << " | ";
                logMessage (l2);
            }
            K flat;
            flat.dist = 0.0f;
            juce::String line;
            for (auto f : freqs)
                line << juce::String (f, 0) << ": " << juce::String (outDb (flat, f, 0.00002, (int) Probe::ic3b) - 20.0 * std::log10 (0.00004), 1) << "  ";
            logMessage ("IC3b gain dB (dist 0): " + line);
            for (float d : { 0.0f, 0.5f, 1.0f })
            {
                line = {};
                K k; k.dist = d;
                for (auto f : freqs)
                    line << juce::String (f, 0) << ": " << juce::String (outDb (k, f, 0.00002, (int) Probe::ic3a) - outDb (k, f, 0.00002, (int) Probe::ic3b), 1) << "  ";
                logMessage ("IC3a gain dB, dist " + juce::String (d, 1) + ": " + line);
            }
            for (float d : { 0.3f, 0.5f, 0.7f, 1.0f })
            {
                K k; k.dist = d;
                MetalZoneStyleDistortionProcessor p;
                p.prepare (sr, 128, 1);
                setKnobs (p, k);
                const double o = steadyPeakToPeak (p, 400.0, 0.05, -1, 100);
                logMessage ("Dist " + juce::String (d, 1) + ": 50 mV in (400 Hz) -> " + juce::String (o, 3) + " V p-p out; IC3b " + juce::String (p.debugVoltage (Probe::ic3b) - 4.5, 2)
                            + ", IC4b " + juce::String (p.debugVoltage (Probe::ic4b) - 4.5, 2) + " V");
            }
            for (int which = 0; which < 3; ++which)
            {
                for (float v : { 0.0f, 0.5f, 1.0f })
                {
                    line = {};
                    K k; k.dist = 0.0f;
                    if (which == 0) k.high = v; else if (which == 1) k.low = v; else k.middle = v;
                    for (auto f : freqs)
                        line << juce::String (f, 0) << ": " << juce::String (outDb (k, f, 0.0001), 1) << "  ";
                    logMessage (juce::String (which == 0 ? "High" : which == 1 ? "Low" : "Middle") + " " + juce::String (v, 1) + " (out dB re 1V p-p): " + line);
                }
            }
            for (float mf : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                line = {};
                K k; k.dist = 0.0f; k.middle = 1.0f; k.midFreq = mf;
                for (auto f : freqs)
                    line << juce::String (f, 0) << ": " << juce::String (outDb (k, f, 0.0001), 1) << "  ";
                logMessage ("Middle 1, Mid Freq " + juce::String (mf, 2) + ": " + line);
            }
        }

        beginTest ("hot input and random knob moves: finite, and the solver never fails");
        {
            MetalZoneStyleDistortionProcessor p;
            p.prepare (sr, 128, 2);
            juce::Random rng (99);
            juce::AudioBuffer<float> buf (2, 128);
            double phase = 0.0;
            bool finite = true;
            for (int b = 0; b < 1200; ++b)
            {
                if (b % 40 == 0)
                    setKnobs (p, { rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat(), rng.nextFloat() });
                const double amp = b % 300 < 40 ? 3.0 : 0.3;
                for (int i = 0; i < 128; ++i)
                {
                    phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / sr;
                    const float x = (float) (amp * std::sin (phase));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                for (int i = 0; i < 128; ++i)
                    finite = finite && std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 100.0f;
            }
            expect (finite, "output stays finite and bounded");
            logMessage ("solver failure rate " + juce::String (p.getSolveFailureRate(), 6) + ", " + juce::String (p.debugIterations(), 2) + " iterations/sample");
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static MetalZoneStyleDistortionProcessorTests metalZoneStyleDistortionProcessorTests;

} // namespace openguitarmultifx
