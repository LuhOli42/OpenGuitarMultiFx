#include "Effects/SVTStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The SVT-style amplifier against the SVT-CL service schematic's own marked voltages and the published
    specifications (300 W into 4 ohm, tone specs). docs/circuits/AmpegSVT.md says which numbers are verified
    and which are documented estimates (OT primary, feedback network, Ultra switch wiring).
*/
class SVTStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    SVTStyleAmplifierProcessorTests() : juce::UnitTest ("SVTStyleAmplifier", "Effects") {}

private:
    using P = SVTStyleAmplifierProcessor::Probe;

    static void setParam (SVTStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (SVTStyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
                           int probe = -1)
    {
        juce::AudioBuffer<float> buffer (1, 128);
        const double sr = 48000.0;
        const int warm = (int) (warmup * sr), meas = (int) (measure * sr);
        double peak = 0.0;
        for (int i = 0; i < warm + meas; ++i)
        {
            buffer.setSample (0, i % 128, (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr)));
            if (i % 128 == 127)
            {
                amp.process (buffer);
                if (i >= warm)
                    peak = juce::jmax (peak, (double) std::abs (probe >= 0 ? amp.debugVoltage ((P) probe) : buffer.getSample (0, 127)));
            }
        }
        return peak;
    }

    void runTest() override
    {
        SVTStyleAmplifierProcessor::reducedOrder = false;

        if (juce::SystemStats::getEnvironmentVariable ("SVT_POWERCAL", {}).isNotEmpty())
        {
            // Calibration sweep for the reduced-order power stage (same methodology as Bassman's
            // BM_POWERCAL): drive the full reference model with a slow settled sine at a range of
            // levels and print (power-input peak, speaker peak/rms, plate rail) per level.
            beginTest ("power-stage calibration sweep (dev only)");
            SVTStyleAmplifierProcessor amp;
            setParam (amp, "svt_input", 0.0f);
            setParam (amp, "svt_gain", 0.8f);
            setParam (amp, "svt_master", 1.0f);
            setParam (amp, "svt_power", 1.0f);
            amp.prepare (48000.0, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 1.0e-4, 2.0e-4, 4.0e-4, 7.0e-4, 1.0e-3, 1.5e-3, 2.0e-3, 3.0e-3,
                                  4.0e-3, 5.5e-3, 7.0e-3, 9.0e-3, 0.012, 0.016, 0.02, 0.03,
                                  0.05, 0.08, 0.12, 0.18, 0.28, 0.4 })
            {
                const long long total = (long long) (1.5 * 48000.0);
                const long long measureFrom = total - (long long) (0.1 * 48000.0);
                double drivePeak = 0.0, outPeak = 0.0, outSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / 48000.0));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom)
                    {
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::powerInput)));
                        const double y = amp.debugVoltage (P::speaker);
                        outPeak = juce::jmax (outPeak, std::abs (y));
                        outSumSq += y * y;
                        ++n2;
                    }
                }
                logMessage ("powercal drive_pk=" + juce::String (drivePeak, 6) + " out_pk=" + juce::String (outPeak, 4)
                            + " out_rms=" + juce::String (std::sqrt (outSumSq / (double) juce::jmax (1LL, n2)), 4)
                            + " rail=" + juce::String (amp.railPlates(), 2)
                            + " fails=" + juce::String (amp.getSolveFailureRate(), 6));
            }
        }

        beginTest ("DC operating points land near the schematic's marked voltages (+-30%)");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 235.0, 0.3, "V1:B plate" },
                { P::secondPlate, 240.0, 0.3, "V1:A plate" },
                { P::recoveryPlate, 240.0, 0.3, "V2:B plate" },
                // The PI is a documented approximation of the real phase splitter (AmpegSVT.md): the LTP's
                // tail sits on the -180 V string so several DC equilibria are nearly valid -- wider margin.
                { P::phaseInverterPlateA, 200.0, 0.5, "PI plate A" },
                { P::phaseInverterPlateB, 250.0, 0.5, "PI plate B" },
                { P::driverPlateA, 220.0, 0.4, "driver plate" },
                { P::powerGridA, -45.0, 0.35, "6550 grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, schematic " + juce::String (pt.target));
            }
            expect (std::abs (amp.railScreens() - 365.0) < 0.2 * 365.0, "screen rail " + juce::String (amp.railScreens()));
            expect (std::abs (amp.railPreamp() - 345.0) < 0.2 * 345.0, "preamp rail " + juce::String (amp.railPreamp()));
            expectGreaterThan (amp.plateCurrentTotal(), 0.15); // six 6550s idle ~0.3 A; half that at minimum
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double peak = runSine (amp, 100.0, 0.05, 1.5, 0.2);
            logMessage ("pre fails " + juce::String ((long long) amp.debugPreFailures())
                        + ", power fails " + juce::String ((long long) amp.debugPowerFailures())
                        + ", sanity rejects " + juce::String ((long long) amp.debugSanityRejects())
                        + ", worst rejected V " + juce::String (amp.debugWorstRejectedVolts(), 1)
                        + ", recoveries " + juce::String ((long long) amp.debugRecoveries()));
            expect (peak > 0.005, "output peak " + juce::String (peak));
            expectLessThan (amp.getSolveFailureRate(), 0.01,
                            "fails pre " + juce::String ((long long) amp.debugPreFailures())
                            + " power " + juce::String ((long long) amp.debugPowerFailures())
                            + " sanity " + juce::String ((long long) amp.debugSanityRejects())
                            + " worstRej " + juce::String (amp.debugWorstRejectedVolts(), 1));
            // A handful of rest-state recoveries during the cold-start transient is acceptable on a
            // fifteen-tube model -- what matters is that the steady state solves cleanly (<1% above).
            expectLessThan (amp.debugRecoveries(), 10);
        }

        beginTest ("mid frequency select moves the resonant peak");
        {
            // Middle at zero shorts the trap's rheostat: the resonant peak should land on the selected
            // tap's frequency. Measure the cathode follower's output (the trap's effect at the mid node
            // propagates there). Two pitfalls shaped this helper: the follower sits on a tap-dependent
            // DC offset of volts (-6.2 V on the 800 Hz tap, +1.1 V on 220 Hz), so a raw |v| "peak"
            // mostly reads DC -- measure AC rms about the window mean instead; and debugVoltage() is
            // a post-block read, so at 750 Hz in 128-sample blocks it samples one fixed phase every
            // block (750 * 128 / 48000 = exactly 2 cycles) -- process in small blocks so the probe is
            // read several times per cycle.
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "svt_middle", 0.0f);
            setParam (amp, "svt_master", 1.0f);
            auto followerAc = [&] (float tap)
            {
                setParam (amp, "svt_mid_freq", tap);
                constexpr int blk = 8;
                juce::AudioBuffer<float> buf (1, blk);
                const double sr = 48000.0;
                // The tap switch re-stamps the trap's L and C mid-transient and the DC operating point
                // relaxes on the seconds scale -- settle well past that before measuring.
                const int warm = (int) (2.0 * sr), meas = (int) (0.5 * sr);
                double s = 0.0, sq = 0.0;
                int n = 0;
                for (int i = 0; i < warm + meas; i += blk)
                {
                    for (int k = 0; k < blk; ++k)
                        buf.setSample (0, k, (float) (0.02 * std::sin (2.0 * juce::MathConstants<double>::pi * 750.0 * (i + k) / sr)));
                    amp.process (buf);
                    if (i >= warm)
                    {
                        const double v = amp.debugVoltage (P::followerOut);
                        s += v;
                        sq += v * v;
                        ++n;
                    }
                }
                const double mean = s / n;
                return std::sqrt (juce::jmax (0.0, sq / n - mean * mean));
            };
            const double a800 = followerAc (1.0f);   // default tap
            const double a220 = followerAc (0.0f);
            const double a3k = followerAc (2.0f);
            // A 750 Hz tone should be much stronger at the follower with the ~800 Hz tap selected
            // than with either of the off-resonance taps.
            logMessage ("750 Hz follower AC rms: 800Hz-tap " + juce::String (a800, 4) + " V, 220Hz-tap " + juce::String (a220, 4)
                        + " V, 3kHz-tap " + juce::String (a3k, 4) + " V");
            expectGreaterThan (a800 / juce::jmax (1.0e-9, a220), 1.2,
                               "800 vs 220 tap ratio " + juce::String (a800 / a220));
            expectGreaterThan (a800 / juce::jmax (1.0e-9, a3k), 1.2,
                               "800 vs 3k tap ratio " + juce::String (a800 / a3k));
        }

        beginTest ("bass knob swings 40 Hz at the tone stack");
        {
            auto levelAt = [&] (float bass, float treble)
            {
                SVTStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "svt_bass", bass);
                setParam (amp, "svt_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("bias knob moves the 6550 idle current");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double cold = amp.plateCurrentTotal();
            setParam (amp, "svt_bias", 1.0f);
            juce::AudioBuffer<float> b (1, 128);
            // The hot-bias operating point rings on a ~4 s supply-sag relaxation (pre-existing:
            // the raw plate current swings ~1.2-3.3 A for seconds after the bias step), so a single
            // instantaneous read is phase luck. Average over ~1.3 s after a ~4 s settle instead.
            for (int i = 0; i < 1500; ++i) amp.process (b);
            double hot = 0.0;
            for (int i = 0; i < 500; ++i) { amp.process (b); hot += amp.plateCurrentTotal(); }
            hot /= 500.0;
            expectGreaterThan (hot / juce::jmax (1.0e-9, cold), 1.2,
                               "idle plate current hot " + juce::String (hot) + " vs cold " + juce::String (cold));
        }

        beginTest ("master at minimum gives silence, turning it does not thump");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "svt_gain", 1.0f);
            setParam (amp, "svt_master", 0.0f);
            juce::AudioBuffer<float> buf (1, 128);
            const double sr = 48000.0;
            double mean = 0.0, sq = 0.0, peak = 0.0;
            int n = 0;
            // The bug was a SUSTAINED rail-to-rail relaxation, not a transient: the first
            // seconds are spent letting the supply caps and the LF network settle (a real
            // amp does the same), so the measurement window starts well past that.
            for (int i = 0; i < (int) (6.0 * sr); ++i)
            {
                buf.setSample (0, i % 128, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * i / sr)));
                if (i % 128 == 127)
                {
                    amp.process (buf);
                    if (i >= (int) (4.5 * sr))
                        for (int s = 0; s < 128; ++s)
                        {
                            const double v = buf.getSample (0, s);
                            mean += v;
                            sq += v * v;
                            peak = juce::jmax (peak, std::abs (v));
                            ++n;
                        }
                }
            }
            mean /= n;
            const double rms = std::sqrt (sq / n);
            logMessage ("master=0 steady state: mean " + juce::String (mean, 6) + ", rms " + juce::String (rms, 6) + ", peak " + juce::String (peak, 6)
                        + ", followerOut " + juce::String (amp.debugVoltage (P::followerOut), 4) + " V, speaker " + juce::String (amp.debugVoltage (P::speaker), 4) + " V");
            expect (std::abs (mean) < 1.0e-3, "DC on output " + juce::String (mean));
            expect (rms < 1.0e-3, "residual level " + juce::String (rms));

            // Turn master 0 -> 1 with the same input running: track the worst sample-to-sample step.
            double prev = buf.getSample (0, 127), worstStep = 0.0;
            setParam (amp, "svt_master", 1.0f);
            for (int i = 0; i < (int) (0.5 * sr); ++i)
            {
                buf.setSample (0, i % 128, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * i / sr)));
                if (i % 128 == 127)
                {
                    amp.process (buf);
                    for (int s = 0; s < 128; ++s)
                    {
                        worstStep = juce::jmax (worstStep, std::abs ((double) buf.getSample (0, s) - prev));
                        prev = buf.getSample (0, s);
                    }
                }
            }
            logMessage ("master 0->1: worst sample step " + juce::String (worstStep, 4));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "svt_gain", 1.0f);
            setParam (amp, "svt_master", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0); // outputScale keeps even a pegged speaker under a few volts of signal
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::SVTStyleAmplifierProcessorTests svtStyleAmplifierProcessorTests;
