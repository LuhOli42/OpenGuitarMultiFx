#include "EffectRegistry.h"
#include "Effects/BassmanStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <cmath>
#include <limits>

namespace openguitarmultifx
{

/**
    The Bassman-style amplifier against the published analysis of the 5F6-A it is modelled on (Kuehnel, "The Fender
    Bassman 5F6A Circuit", pentodepress.com) and the schematic's own voltages. Every number quoted here is from
    those two sources; docs/circuits/Bassman5F6A.md says where the model deliberately differs.
*/
class BassmanStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    BassmanStyleAmplifierProcessorTests() : juce::UnitTest ("BassmanStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = BassmanStyleAmplifierProcessor::Probe;

    static void setParam (BassmanStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    struct Settings
    {
        float normal = 0.5f, bright = 0.5f, treble = 0.5f, middle = 0.5f, bass = 0.5f, presence = 0.5f, output = 0.5f;
    };

    static void apply (BassmanStyleAmplifierProcessor& amp, const Settings& s)
    {
        setParam (amp, "bm_vol_normal", s.normal);
        setParam (amp, "bm_vol_bright", s.bright);
        setParam (amp, "bm_treble", s.treble);
        setParam (amp, "bm_middle", s.middle);
        setParam (amp, "bm_bass", s.bass);
        setParam (amp, "bm_presence", s.presence);
        setParam (amp, "bm_output", s.output);
    }

    /** Runs a sine through the amp one sample at a time and returns the fundamental amplitude of each probe over the
        last whole number of cycles (relative to the DC each probe had after prepare()). */
    template <size_t N>
    static std::array<double, N> fundamentals (BassmanStyleAmplifierProcessor& amp, const std::array<P, N>& probes,
                                               double freq, double amplitude, double seconds = 0.6)
    {
        std::array<double, N> dc {}, sinSum {}, cosSum {};
        for (size_t k = 0; k < N; ++k)
            dc[k] = amp.debugVoltage (probes[k]);

        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) (seconds * sr);
        const long long len = (long long) std::llround (std::floor (0.25 * freq) * sr / freq);
        juce::AudioBuffer<float> one (2, 1);
        for (long long n = 0; n < total; ++n)
        {
            const double ph = twoPi * freq * (double) n / sr;
            const float v = (float) (amplitude * std::sin (ph));
            one.setSample (0, 0, v);
            one.setSample (1, 0, v);
            amp.process (one);
            if (n >= total - len)
                for (size_t k = 0; k < N; ++k)
                {
                    const double y = amp.debugVoltage (probes[k]) - dc[k];
                    sinSum[k] += y * std::sin (ph);
                    cosSum[k] += y * std::cos (ph);
                }
        }
        std::array<double, N> out {};
        for (size_t k = 0; k < N; ++k)
            out[k] = 2.0 * std::sqrt (sinSum[k] * sinSum[k] + cosSum[k] * cosSum[k]) / (double) len;
        return out;
    }

    static double db (double x) { return 20.0 * std::log10 (x); }

    /** RMS and peak of the amplifier output for a sine, over the last `measure` seconds. */
    static double runSine (BassmanStyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
                           double* peakOut = nullptr)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const int block = 128;
        const long long total = (long long) ((warmup + measure) * sr);
        juce::AudioBuffer<float> buf (2, block);
        double sum = 0.0, peak = 0.0;
        long long counted = 0;
        for (long long n = 0; n < total; n += block)
        {
            for (int i = 0; i < block; ++i)
            {
                const float v = (float) (level * std::sin (twoPi * freq * (double) (n + i) / sr));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            amp.process (buf);
            if (n >= (long long) (warmup * sr))
                for (int i = 0; i < block; ++i)
                {
                    const double y = buf.getSample (0, i);
                    sum += y * y;
                    peak = std::max (peak, std::abs (y));
                    ++counted;
                }
        }
        if (peakOut != nullptr)
            *peakOut = peak;
        return std::sqrt (sum / (double) counted);
    }

    /** Small-signal gain (dB) from the amplifier input to the speaker terminals. */
    static double speakerGainDb (const Settings& s, double freq)
    {
        BassmanStyleAmplifierProcessor amp;
        apply (amp, s);
        amp.prepare (sr, 128, 2);
        const double a = 0.002;
        return db (fundamentals (amp, std::array<P, 1> { P::speaker }, freq, a, 0.9)[0] / a);
    }

    void runTest() override
    {
        BassmanStyleAmplifierProcessor::reducedOrder = false;

        if (juce::SystemStats::getEnvironmentVariable ("BM_POWERCAL", {}).isNotEmpty())
        {
            // Calibration sweep for a behavioural power stage (2026-09-27), same methodology as the Super Lead's
            // SL_POWERCAL: drive the full reference model with a slow settled sine at a range of levels, let the supply
            // reach its own quasi-equilibrium at each one, and print (toneStackOut peak, speaker peak/rms, plate rail).
            beginTest ("power-stage calibration sweep (dev only)");
            BassmanStyleAmplifierProcessor amp;
            setParam (amp, "bm_input", 0.0f);
            setParam (amp, "bm_vol_normal", 0.8f);
            setParam (amp, "bm_treble", 0.5f);
            setParam (amp, "bm_middle", 0.5f);
            setParam (amp, "bm_bass", 0.5f);
            setParam (amp, "bm_power", 1.0f);
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 2.0e-5, 5.0e-5, 1.0e-4, 2.0e-4, 4.0e-4, 7.0e-4, 1.0e-3, 1.5e-3, 2.0e-3, 3.0e-3, 4.0e-3, 5.5e-3, 7.0e-3, 9.0e-3, 0.012, 0.016, 0.02, 0.03, 0.05, 0.08, 0.12, 0.18, 0.28, 0.4 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double drivePeak = 0.0, outPeak = 0.0, outSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom)
                    {
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::toneStackOut)));
                        const double y = amp.debugVoltage (P::speaker);
                        outPeak = juce::jmax (outPeak, std::abs (y));
                        outSumSq += y * y;
                        ++n2;
                    }
                }
                logMessage ("powercal drive_pk=" + juce::String (drivePeak, 6) + " out_pk=" + juce::String (outPeak, 4)
                            + " out_rms=" + juce::String (std::sqrt (outSumSq / (double) juce::jmax (1LL, n2)), 4) + " rail="
                            + juce::String (amp.railPlates(), 2) + " fails=" + juce::String (amp.getSolveFailureRate(), 6));
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("BM_NOON_DIAG", {}).isNotEmpty())
        {
            // Same diagnostic as SuperLeadStyleAmplifierProcessorTests' SL_NOON_DIAG: cross-check PedalUnityLevelTests'
            // own methodology (via the real registry factory) against raw ref/red gain to separate "reducedOrder's own
            // incremental error" from "the reference's own gain drifted since the trim was last calibrated".
            beginTest ("noon knobs diagnostic (dev only)");
            const auto gainAt = [] (bool reduced)
            {
                BassmanStyleAmplifierProcessor::reducedOrder = reduced;
                BassmanStyleAmplifierProcessor amp;
                const auto pages = amp.getParameterPages();
                for (auto* f : pages[0])
                    if (f->range.interval < 1.0f)
                        *f = juce::jlimit (f->range.start, f->range.end, 0.5f);
                amp.prepare (sr, 512, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi, f0 = 164.81, rmsIn = 0.1;
                double norm = 0.0;
                for (int k = 1; k <= 10; ++k) norm += 0.5 / (double) (k * k);
                const double scale = rmsIn / std::sqrt (norm);
                const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);
                juce::AudioBuffer<float> buf (1, 64);
                double sIn = 0.0, sOut = 0.0;
                for (long long base = 0; base < warm + len; base += 64)
                {
                    double x[64];
                    for (int i = 0; i < 64; ++i)
                    {
                        double v = 0.0;
                        for (int k = 1; k <= 10; ++k) v += std::sin (twoPi * f0 * k * (double) (base + i) / sr) / (double) k;
                        x[i] = scale * v;
                        buf.setSample (0, i, (float) x[i]);
                    }
                    amp.process (buf);
                    if (base >= warm)
                        for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                }
                return 10.0 * std::log10 (sOut / sIn);
            };
            logMessage ("ref (raw): " + juce::String (gainAt (false), 2) + " dB");
            logMessage ("red (raw): " + juce::String (gainAt (true), 2) + " dB");
            for (double freq : { 100.0, 165.0, 500.0, 1000.0, 1650.0 })
            {
                const auto pureToneGainAt = [&] (bool reduced)
                {
                    BassmanStyleAmplifierProcessor::reducedOrder = reduced;
                    BassmanStyleAmplifierProcessor amp;
                    setParam (amp, "bm_input", 0.0f);
                    setParam (amp, "bm_vol_normal", 0.5f);
                    setParam (amp, "bm_power", 1.0f);
                    amp.prepare (sr, 512, 1);
                    const double twoPi = 2.0 * juce::MathConstants<double>::pi, rmsIn = 0.1;
                    const int warm = (int) (5.0 * sr), len = (int) (1.0 * sr);
                    juce::AudioBuffer<float> buf (1, 64);
                    double sIn = 0.0, sOut = 0.0;
                    for (long long base = 0; base < warm + len; base += 64)
                    {
                        double x[64];
                        for (int i = 0; i < 64; ++i)
                        {
                            x[i] = rmsIn * std::sqrt (2.0) * std::sin (twoPi * freq * (double) (base + i) / sr);
                            buf.setSample (0, i, (float) x[i]);
                        }
                        amp.process (buf);
                        if (base >= warm)
                            for (int i = 0; i < 64; ++i) { sIn += x[i] * x[i]; sOut += (double) buf.getSample (0, i) * buf.getSample (0, i); }
                    }
                    return 10.0 * std::log10 (sOut / sIn);
                };
                logMessage ("pure tone " + juce::String (freq, 0) + " Hz: ref " + juce::String (pureToneGainAt (false), 2)
                            + " dB, red " + juce::String (pureToneGainAt (true), 2) + " dB");
            }
            BassmanStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            // Permanent regression test for behavioralPowerStage() (2026-09-27, the shipped default -- EffectRegistry.cpp
            // turns it on for the real app): drives BOTH a reference (full netlist) and a reducedOrder amp with the SAME
            // signal, asserting level tracks the reference within 1.5 dB (the sag curve's saturation region has a bit more
            // residual error here than the Super Lead's -- see docs/circuits/Bassman5F6A.md), and reducedOrder has zero
            // failures/recoveries by construction (no Newton solve left to have a bad day).
            beginTest ("reducedOrder power stage tracks the reference (level, and zero failures by construction)");
            BassmanStyleAmplifierProcessor::reducedOrder = false;
            BassmanStyleAmplifierProcessor ref;
            setParam (ref, "bm_input", 0.0f);
            setParam (ref, "bm_vol_normal", 0.8f);
            setParam (ref, "bm_power", 1.0f);
            ref.prepare (sr, 128, 2);
            BassmanStyleAmplifierProcessor::reducedOrder = true;
            BassmanStyleAmplifierProcessor red;
            setParam (red, "bm_input", 0.0f);
            setParam (red, "bm_vol_normal", 0.8f);
            setParam (red, "bm_power", 1.0f);
            red.prepare (sr, 128, 2);

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 5.0e-4, 2.0e-3, 6.0e-3, 0.012, 0.02, 0.05, 0.1, 0.2 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double refOutSumSq = 0.0, redOutSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    ref.process (one);
                    if (n >= measureFrom)
                    {
                        const double y = ref.debugVoltage (P::speaker);
                        refOutSumSq += y * y;
                        ++n2;
                    }
                }
                n2 = 0;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    red.process (one);
                    if (n >= measureFrom)
                    {
                        const double y = red.debugVoltage (P::speaker);
                        redOutSumSq += y * y;
                        ++n2;
                    }
                }
                const double refRms = std::sqrt (refOutSumSq / (double) juce::jmax (1LL, n2));
                const double redRms = std::sqrt (redOutSumSq / (double) juce::jmax (1LL, n2));
                const double dB = 20.0 * std::log10 (juce::jmax (1.0e-9, redRms) / juce::jmax (1.0e-9, refRms));
                logMessage ("level " + juce::String (level, 4) + ": ref " + juce::String (refRms, 3) + " Vrms vs reduced " + juce::String (redRms, 3)
                            + " Vrms, diff " + juce::String (dB, 2) + " dB, reduced fails " + juce::String (red.getSolveFailureRate(), 6) + ", recov " + juce::String (red.debugRecoveries()));
                expectLessThan (std::abs (dB), 1.5);
                expectLessThan (red.getSolveFailureRate(), 1.0e-6);
                expect (red.debugRecoveries() == 0);
            }
            BassmanStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            // Worst-block cost, reducedOrder vs the same hot-pedal stress the reference model was measured against
            // (docs/circuits/Bassman5F6A.md: reference worst blocks reach several ms / hundreds of iterations there).
            beginTest ("reducedOrder: worst-block cost under the same hot-pedal stress (dev metric, logged not asserted on the reference)");
            BassmanStyleAmplifierProcessor::reducedOrder = true;
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            double seconds = 0.0, worstPct = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                const auto t0 = juce::Time::getHighResolutionTicks();
                amp.process (buf);
                const double blockSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                seconds += blockSeconds;
                worstPct = juce::jmax (worstPct, 100.0 * blockSeconds / (128.0 / sr));
            }
            logMessage ("reducedOrder: " + juce::String (100.0 * seconds / 10.0, 2) + " % avg, worst block " + juce::String (worstPct, 1)
                        + " %, failures " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
            BassmanStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud (the shipped default has no physical speaker to mismatch)");
            BassmanStyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                BassmanStyleAmplifierProcessor amp;
                setParam (amp, "bm_input", 0.0f);
                setParam (amp, "bm_vol_normal", 0.8f);
                setParam (amp, "bm_speaker", speaker);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> one (2, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double sumSq = 0.0;
                long long n2 = 0;
                const long long total = (long long) (0.7 * sr), measureFrom = total - (long long) (0.1 * sr);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (0.02 * std::sin (twoPi * 200.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom) { const double y = amp.debugVoltage (P::speaker); sumSq += y * y; ++n2; }
                }
                return std::sqrt (sumSq / (double) juce::jmax (1LL, n2));
            };
            const double r4 = rmsAt (0.0f), r8 = rmsAt (1.0f), r16 = rmsAt (2.0f);
            logMessage ("reducedOrder speaker volts at 4 / 8 / 16 ohm: " + juce::String (r4, 2) + " / " + juce::String (r8, 2) + " / " + juce::String (r16, 2) + " V rms");
            expectLessThan (std::abs (20.0 * std::log10 (r4 / r8)), 0.1);
            expectLessThan (std::abs (20.0 * std::log10 (r16 / r8)), 0.1);
            BassmanStyleAmplifierProcessor::reducedOrder = false;
        }

        beginTest ("operating points match the schematic (its voltages are +-20%)");
        {
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            const auto near = [this] (double v, double target, const char* what)
            {
                expect (std::abs (v - target) < 0.2 * std::abs (target), juce::String (what) + " " + juce::String (v) + " V, schematic " + juce::String (target));
            };
            near (amp.debugVoltage (P::gainStagePlate), 180.0, "gain-stage plate");
            near (amp.debugVoltage (P::phaseInverterPlateA), 236.0, "phase inverter plate (82k)");
            near (amp.debugVoltage (P::phaseInverterPlateB), 230.0, "phase inverter plate (100k)");
            near (amp.debugVoltage (P::phaseInverterTail), 32.5, "phase inverter tail");
            near (amp.debugVoltage (P::powerGridA), -48.0, "power-tube bias");
            near (amp.railPlates(), 452.0, "+452 V rail");
            near (amp.railScreens(), 450.0, "+450 V rail");
            near (amp.railPhaseInverter(), 385.0, "+385 V rail");
            near (amp.railPreamp(), 325.0, "+325 V rail");
            logMessage ("idle plate current " + juce::String (amp.plateCurrentTotal() * 1e3, 1) + " mA (both tubes), screens " + juce::String (amp.screenCurrentTotal() * 1e3, 1) + " mA");
        }

        beginTest ("stage gains follow the published analysis");
        {
            // One channel at full volume, the other muted: V1 (12AY7, bypassed cathode) -32.2, the 12AX7 gain stage
            // -20.7 (the muted channel's 270k halves the signal at the mixing node), phase inverter -21.9 / +22.6.
            BassmanStyleAmplifierProcessor amp;
            Settings s;
            s.normal = 0.0f;
            s.bright = 1.0f;
            apply (amp, s);
            amp.prepare (sr, 128, 2);
            const std::array<P, 5> probes { P::brightPlate, P::gainStagePlate, P::phaseInverterGrid, P::phaseInverterPlateA, P::phaseInverterPlateB };
            const auto f = fundamentals (amp, probes, 1000.0, 0.001, 0.8);
            logMessage ("V1 " + juce::String (f[0] / 0.001, 1) + ", V2A " + juce::String (f[1] / f[0], 1) + ", PI (closed loop) " + juce::String (f[3] / f[2], 1) + " / " + juce::String (f[4] / f[2], 1));
            expect (std::abs (f[0] / 0.001 - 32.2) < 0.15 * 32.2, "V1 gain " + juce::String (f[0] / 0.001));
            expect (std::abs (f[1] / f[0] - 20.7) < 0.15 * 20.7, "V2A gain " + juce::String (f[1] / f[0]));

            // The phase inverter: open loop (the feedback path removed) it must be balanced like the real one
            // (|+22.6| / |-21.9| = 1.032) and within 3 dB of Kuehnel's figure (the Koren 12AX7 has a higher gm).
            amp.debugSetFeedbackResistance (1.0e12);
            const auto g = fundamentals (amp, probes, 1000.0, 0.001, 0.8);
            const double gainA = g[3] / g[2], gainB = g[4] / g[2];
            logMessage ("PI open loop " + juce::String (gainA, 1) + " / " + juce::String (gainB, 1) + " (published 21.9 / 22.6)");
            expect (std::abs (gainB / gainA - 22.6 / 21.9) < 0.03, "phase inverter balance " + juce::String (gainB / gainA));
            expect (gainA > 21.9 * 0.7 && gainA < 21.9 * 1.45, "phase inverter gain " + juce::String (gainA));
        }

        beginTest ("open-loop power stage reproduces the published transfer curve and distortion");
        {
            // Kuehnel: the 2-ohm output vs the AC voltage at the upper tube's grid, feedback disconnected, no sag:
            // ~13 V at 48 V, slope 0.175 at the centre rising to ~0.275 at the extremes, 5% third harmonic at full swing.
            struct Point { double gridAmp, gain, third; };
            std::array<Point, 4> results {};
            int idx = 0;
            for (double target : { 2.0, 10.0, 30.0, 48.0 })
            {
                double in = 0.0002, grid = 0.0, fund = 0.0, third = 0.0;
                for (int pass = 0; pass < 3; ++pass)
                {
                    BassmanStyleAmplifierProcessor amp;
                    Settings s;
                    s.normal = 1.0f;
                    s.bright = 0.0f;
                    apply (amp, s);
                    amp.prepare (sr, 128, 2);
                    amp.debugSetFeedbackResistance (1.0e12);
                    amp.debugSetResistiveLoad (8.0); // Kuehnel measured into a resistor
                    const double twoPi = 2.0 * juce::MathConstants<double>::pi, f = 1000.0;
                    const long long settle = (long long) (0.1 * sr), cycle = (long long) (sr / f);
                    double s1 = 0, c1 = 0, s3 = 0, c3 = 0, gpk = 0;
                    juce::AudioBuffer<float> one (2, 1);
                    for (long long n = 0; n < settle + 8 * cycle; ++n)
                    {
                        const double lvl = n < settle ? in * 0.1 : in; // low level first, then a step up: no supply sag yet
                        const double ph = twoPi * f * (double) n / sr;
                        one.setSample (0, 0, (float) (lvl * std::sin (ph)));
                        one.setSample (1, 0, (float) (lvl * std::sin (ph)));
                        amp.process (one);
                        if (n >= settle + 2 * cycle && n < settle + 6 * cycle)
                        {
                            const double y = one.getSample (0, 0) / BassmanStyleAmplifierProcessor::outputScale / 2.0; // volts at the original 2 ohm tap
                            s1 += y * std::sin (ph);
                            c1 += y * std::cos (ph);
                            s3 += y * std::sin (3 * ph);
                            c3 += y * std::cos (3 * ph);
                            gpk = std::max (gpk, std::abs (amp.debugVoltage (P::powerGridA) + 48.0));
                        }
                    }
                    grid = gpk;
                    fund = 2.0 * std::sqrt (s1 * s1 + c1 * c1) / (double) (4 * cycle);
                    third = 2.0 * std::sqrt (s3 * s3 + c3 * c3) / (double) (4 * cycle);
                    in *= target / std::max (grid, 1.0e-3);
                }
                results[(size_t) idx++] = { grid, fund / grid, third / fund };
                logMessage ("grid amplitude " + juce::String (grid, 1) + " V: output " + juce::String (fund, 2) + " V, gain " + juce::String (fund / grid, 3) + ", 3rd harmonic " + juce::String (100.0 * third / fund, 2) + " %");
            }
            expect (results[0].gain > 0.15 && results[0].gain < 0.21, "centre gain " + juce::String (results[0].gain));
            expect (results[3].gain > results[0].gain * 1.15, "gain must expand with drive (class AB)");
            expect (results[3].gain > 0.22 && results[3].gain < 0.30, "full-swing gain " + juce::String (results[3].gain));
            expect (results[3].gain * results[3].gridAmp > 10.5 && results[3].gain * results[3].gridAmp < 14.5, "output at 48 V of drive");
            expect (results[3].third > 0.035 && results[3].third < 0.075, "third harmonic " + juce::String (results[3].third));
            expect (results[0].third < 0.005, "clean at small signal");
        }

        beginTest ("supply sags under sustained full power like the real one (-57 V on the screens, 13 Hz ring)");
        {
            BassmanStyleAmplifierProcessor amp;
            Settings s;
            s.normal = 1.0f;
            s.bright = 0.0f;
            apply (amp, s);
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            juce::AudioBuffer<float> buf (2, 128);
            const double idle = amp.railScreens();
            long long n = 0;
            double deepest = 0.0, deepestAt = 0.0, sagAtHalfSecond = 0.0;
            while (n < (long long) (0.6 * sr))
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float v = (float) (0.05 * std::sin (twoPi * 200.0 * (double) (n + i) / sr));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                n += 128;
                const double sag = amp.railScreens() - idle;
                if (sag < deepest)
                {
                    deepest = sag;
                    deepestAt = (double) n / sr;
                }
                if (n >= (long long) (0.5 * sr) && n < (long long) (0.5 * sr) + 128)
                    sagAtHalfSecond = sag;
            }
            logMessage ("screens: deepest " + juce::String (deepest, 1) + " V at " + juce::String (deepestAt * 1000.0, 0) + " ms, settled " + juce::String (sagAtHalfSecond, 1) + " V");
            expect (sagAtHalfSecond < -45.0 && sagAtHalfSecond > -70.0, "steady-state sag " + juce::String (sagAtHalfSecond));
            expect (deepest < sagAtHalfSecond - 4.0, "the supply overshoots before it settles (choke and capacitors ring)");
            expect (deepestAt > 0.02 && deepestAt < 0.07, "first minimum at " + juce::String (deepestAt));
            expect (amp.railPlates() < 425.0, "plate rail sags too");
        }

        beginTest ("tone stack: each control acts in its own band");
        {
            const auto gain = [] (float bass, float mid, float treble, double f)
            {
                Settings s;
                s.bass = bass;
                s.middle = mid;
                s.treble = treble;
                s.normal = 1.0f;
                s.bright = 0.0f;
                BassmanStyleAmplifierProcessor amp;
                apply (amp, s);
                amp.prepare (sr, 128, 2);
                const std::array<P, 2> probes { P::followerOut, P::toneStackOut };
                const auto x = fundamentals (amp, probes, f, 0.0005, 0.7);
                return db (x[1] / x[0]);
            };
            const double bassUp = gain (1.0f, 0.5f, 0.5f, 80.0), bassDown = gain (0.0f, 0.5f, 0.5f, 80.0);
            const double trebleUp = gain (0.5f, 0.5f, 1.0f, 5000.0), trebleDown = gain (0.5f, 0.5f, 0.0f, 5000.0);
            const double midUp = gain (0.5f, 1.0f, 0.5f, 700.0), midDown = gain (0.5f, 0.0f, 0.5f, 700.0);
            const double trebleAtBass = gain (0.5f, 0.5f, 1.0f, 80.0), trebleAtBass0 = gain (0.5f, 0.5f, 0.0f, 80.0);
            logMessage ("bass " + juce::String (bassDown, 1) + " -> " + juce::String (bassUp, 1) + " dB at 80 Hz; treble " + juce::String (trebleDown, 1) + " -> " + juce::String (trebleUp, 1) + " dB at 5 kHz; middle " + juce::String (midDown, 1) + " -> " + juce::String (midUp, 1) + " dB at 700 Hz");
            expect (bassUp - bassDown > 8.0, "bass control range");
            expect (trebleUp - trebleDown > 9.0, "treble control range");
            expect (midUp - midDown > 6.0, "middle control range");
            expect (std::abs (trebleAtBass - trebleAtBass0) < 3.0, "treble control leaves the bass alone");
            // insertion loss of the stack at noon, 1 kHz: the published curves put it around -10 to -14 dB
            const double loss = gain (0.5f, 0.5f, 0.5f, 1000.0);
            expect (loss < -8.0 && loss > -18.0, "insertion loss " + juce::String (loss));
        }

        beginTest ("presence opens the top end, the bright channel adds treble at low volume");
        {
            Settings s;
            s.bright = 0.0f;
            s.presence = 0.0f;
            const double p0hf = speakerGainDb (s, 5000.0), p0lf = speakerGainDb (s, 80.0);
            s.presence = 1.0f;
            const double p1hf = speakerGainDb (s, 5000.0), p1lf = speakerGainDb (s, 80.0);
            logMessage ("presence 0 -> 1: " + juce::String (p1hf - p0hf, 1) + " dB at 5 kHz, " + juce::String (p1lf - p0lf, 1) + " dB at 80 Hz");
            expect (p1hf - p0hf > 3.0, "presence raises the top end");
            expect (std::abs (p1lf - p0lf) < 1.0, "presence leaves the bass alone");

            Settings n;
            n.normal = 0.3f;
            n.bright = 0.0f;
            Settings b;
            b.normal = 0.0f;
            b.bright = 0.3f;
            const double nTilt = speakerGainDb (n, 5000.0) - speakerGainDb (n, 400.0);
            const double bTilt = speakerGainDb (b, 5000.0) - speakerGainDb (b, 400.0);
            logMessage ("treble/mid tilt at volume 0.3: normal " + juce::String (nTilt, 1) + " dB, bright " + juce::String (bTilt, 1) + " dB");
            expect (bTilt - nTilt > 8.0, "the bright cap's boost");
        }


        beginTest ("output level: full power is ~50 W into 2 ohm and the solver never gives up, at any input level");
        {
            for (double level : { 0.01, 0.1, 1.0 })
            {
                BassmanStyleAmplifierProcessor amp;
                Settings s;
                s.normal = 1.0f;
                s.bright = 1.0f;
                apply (amp, s);
                amp.prepare (sr, 128, 2);
                double peak = 0.0;
                const auto t0 = std::chrono::steady_clock::now();
                const double rms = runSine (amp, 440.0, level, 0.5, 0.3, &peak);
                const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
                const double speakerRms = rms / BassmanStyleAmplifierProcessor::outputScale / 2.0; // volts at the original 2 ohm tap
                logMessage ("input " + juce::String (level) + ": speaker " + juce::String (speakerRms, 2) + " V rms (" + juce::String (speakerRms * speakerRms / 2.0, 1) + " W), peak " + juce::String (peak / BassmanStyleAmplifierProcessor::outputScale / 2.0, 1) + " V, cpu " + juce::String (100.0 * sec / 0.8, 1) + " % of a core at 1x");
                expectEquals (amp.getSolveFailureRate(), 0.0);
                expect (std::isfinite (rms));
                if (level > 0.05)
                {
                    expect (speakerRms > 8.0 && speakerRms < 12.5, "full-drive output " + juce::String (speakerRms) + " V rms");
                    expect (peak / BassmanStyleAmplifierProcessor::outputScale / 2.0 < 40.0, "peaks stay bounded (a speaker's inductance makes the terminal voltage peaky)");
                }
            }
        }

        beginTest ("every corner of the controls finds its operating point and runs");
        {
            int bad = 0;
            for (float bass : { 0.0f, 1.0f })
                for (float mid : { 0.0f, 1.0f })
                    for (float treble : { 0.0f, 1.0f })
                        for (float pres : { 0.0f, 1.0f })
                            for (float vol : { 0.0f, 1.0f })
                            {
                                BassmanStyleAmplifierProcessor amp;
                                Settings s;
                                s.bass = bass;
                                s.middle = mid;
                                s.treble = treble;
                                s.presence = pres;
                                s.normal = vol;
                                s.bright = vol;
                                apply (amp, s);
                                amp.prepare (sr, 128, 2);
                                const double tail = amp.debugVoltage (P::phaseInverterTail);
                                double peak = 0.0;
                                runSine (amp, 440.0, 0.3, 0.1, 0.05, &peak);
                                if (! amp.dcConverged() || std::abs (tail - 29.7) > 3.0 || amp.getSolveFailureRate() > 0.0)
                                {
                                    logMessage ("failure rate " + juce::String (amp.getSolveFailureRate()));
                                    ++bad;
                                    logMessage ("bad corner: bass " + juce::String (bass) + " mid " + juce::String (mid) + " treble " + juce::String (treble) + " pres " + juce::String (pres) + " vol " + juce::String (vol));
                                }
                            }
            expectEquals (bad, 0);
        }

        beginTest ("page 2: Power Drive is a master volume in front of the output tubes");
        {
            const auto rmsAt = [this] (float power, double level)
            {
                BassmanStyleAmplifierProcessor amp;
                Settings s;
                s.normal = 1.0f;
                s.bright = 1.0f;
                apply (amp, s);
                setParam (amp, "bm_power", power);
                amp.prepare (sr, 128, 2);
                const double rms = runSine (amp, 440.0, level, 0.4, 0.2) / BassmanStyleAmplifierProcessor::outputScale / 2.0;
                expectEquals (amp.getSolveFailureRate(), 0.0);
                return rms;
            };
            const double full = rmsAt (1.0f, 0.05), half = rmsAt (0.6f, 0.05), low = rmsAt (0.3f, 0.05);
            logMessage ("output at Power Drive 1.0 / 0.6 / 0.3: " + juce::String (full, 2) + " / " + juce::String (half, 2) + " / " + juce::String (low, 2) + " V");
            expect (half < full * 0.98 && low < full * 0.6, "the master turns the output down");
        }

        beginTest ("page 2: Bias moves the idle current, and stays stable hot and cold");
        {
            const auto idleAndFailures = [] (float bias)
            {
                BassmanStyleAmplifierProcessor amp;
                setParam (amp, "bm_bias", bias);
                amp.prepare (sr, 128, 2);
                const double idle = amp.plateCurrentTotal();
                runSine (amp, 220.0, 0.2, 0.3, 0.1);
                return std::pair<double, double> { idle, amp.getSolveFailureRate() };
            };
            const auto cold = idleAndFailures (1.0f), noon = idleAndFailures (0.5f), hot = idleAndFailures (0.0f);
            logMessage ("idle plate current cold / noon / hot: " + juce::String (cold.first * 1e3, 1) + " / " + juce::String (noon.first * 1e3, 1) + " / " + juce::String (hot.first * 1e3, 1) + " mA");
            expect (cold.first < noon.first * 0.65, "a colder bias draws less");
            expect (hot.first > noon.first * 1.6, "a hotter bias draws more");
            expect (cold.second < 0.001 && noon.second < 0.001 && hot.second < 0.001, "no solver trouble at either end");
        }

        beginTest ("page 2: Tube Feel scales the sag and the feedback");
        {
            const auto sagAndFailures = [] (float feel)
            {
                BassmanStyleAmplifierProcessor amp;
                Settings s;
                s.normal = 1.0f;
                s.bright = 0.0f;
                apply (amp, s);
                setParam (amp, "bm_tube_feel", feel);
                amp.prepare (sr, 128, 2);
                const double idle = amp.railScreens();
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                juce::AudioBuffer<float> buf (2, 128);
                double deepest = 0.0;
                for (long long n = 0; n < (long long) (0.4 * sr); n += 128)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        const float v = (float) (0.05 * std::sin (twoPi * 200.0 * (double) (n + i) / sr));
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                    amp.process (buf);
                    deepest = std::min (deepest, amp.railScreens() - idle);
                }
                return std::pair<double, double> { -deepest, amp.getSolveFailureRate() };
            };
            const auto solid = sagAndFailures (0.0f), tube = sagAndFailures (1.0f), mid = sagAndFailures (0.5f);
            logMessage ("deepest screen sag at Tube Feel 0 / 0.5 / 1: " + juce::String (solid.first, 1) + " / " + juce::String (mid.first, 1) + " / " + juce::String (tube.first, 1) + " V");
            expect (solid.first < 0.35 * tube.first, "a stiff supply barely sags");
            expect (mid.first > solid.first && mid.first < tube.first, "in between");
            expect (solid.second < 0.001 && tube.second < 0.001, "stable with the extra feedback");
            const auto smallGain = [] (float feel)
            {
                Settings s;
                s.bright = 0.0f;
                BassmanStyleAmplifierProcessor amp;
                apply (amp, s);
                setParam (amp, "bm_tube_feel", feel);
                amp.prepare (sr, 128, 2);
                return db (fundamentals (amp, std::array<P, 1> { P::speaker }, 1000.0, 0.002, 0.9)[0] / 0.002);
            };
            expect (smallGain (0.0f) < smallGain (1.0f) - 2.0, "more feedback means less gain");
        }

        beginTest ("page 2: Speaker is a real load -- resonance, voice-coil inductance, 4 / 8 / 16 ohm");
        {
            const auto gainAt = [] (float speaker, double f)
            {
                Settings s;
                s.bright = 0.0f;
                BassmanStyleAmplifierProcessor amp;
                apply (amp, s);
                setParam (amp, "bm_speaker", speaker);
                amp.prepare (sr, 128, 2);
                return db (fundamentals (amp, std::array<P, 1> { P::speaker }, f, 0.002, 0.9)[0] / 0.002);
            };
            BassmanStyleAmplifierProcessor resistive;
            Settings s;
            s.bright = 0.0f;
            apply (resistive, s);
            resistive.prepare (sr, 128, 2);
            resistive.debugSetResistiveLoad (8.0);
            const auto flat = [&] (double f) { return db (fundamentals (resistive, std::array<P, 1> { P::speaker }, f, 0.002, 0.9)[0] / 0.002); };
            const double resonanceBump = (gainAt (1.0f, 85.0) - gainAt (1.0f, 200.0)) - (flat (85.0) - flat (200.0));
            const double topLift = (gainAt (1.0f, 6000.0) - gainAt (1.0f, 500.0)) - (flat (6000.0) - flat (500.0));
            logMessage ("8 ohm speaker vs a resistor: resonance bump " + juce::String (resonanceBump, 1) + " dB at 85 Hz, top-end lift " + juce::String (topLift, 1) + " dB at 6 kHz");
            expect (resonanceBump > 1.5, "the cone resonance is audible in the amp's response");
            expect (topLift > 3.0, "the voice coil's inductance lifts the top end");

            const auto rmsFor = [] (float speaker)
            {
                Settings loud;
                loud.normal = 1.0f;
                loud.bright = 1.0f;
                BassmanStyleAmplifierProcessor amp;
                apply (amp, loud);
                setParam (amp, "bm_speaker", speaker);
                amp.prepare (sr, 128, 2);
                const double rms = runSine (amp, 200.0, 0.1, 0.4, 0.2);
                return std::pair<double, double> { rms / BassmanStyleAmplifierProcessor::outputScale / 2.0, amp.getSolveFailureRate() };
            };
            const auto r4 = rmsFor (0.0f), r8 = rmsFor (1.0f), r16 = rmsFor (2.0f);
            logMessage ("full-drive output at 4 / 8 / 16 ohm (level-compensated): " + juce::String (r4.first, 2) + " / " + juce::String (r8.first, 2) + " / " + juce::String (r16.first, 2) + " V rms");
            // The load changes the sound, the digital level is compensated to stay about the same (the raw volts differ by z^0.83)
            expect (std::abs (db (r4.first / r8.first)) < 3.0 && std::abs (db (r16.first / r8.first)) < 3.0, "loudness stays comparable across the loads");
            expect (r4.second < 0.001 && r8.second < 0.001 && r16.second < 0.001, "stable into all three");
        }

        beginTest ("page 2 lives in a sub-group; presets keep every parameter");
        {
            BassmanStyleAmplifierProcessor amp;
            auto pages = amp.getParameterPages();
            expectEquals ((int) pages.size(), 2);
            expectEquals ((int) pages[0].size(), 8); // Input, Volume Normal/Bright, Treble, Middle, Bass, Presence, Output
            expectEquals ((int) pages[1].size(), 4);
            setParam (amp, "bm_bias", 0.8f);
            setParam (amp, "bm_speaker", 2.0f);
            auto xml = amp.getState();
            BassmanStyleAmplifierProcessor other;
            other.setState (*xml);
            bool ok = true;
            for (auto* p : other.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                {
                    if (f->paramID == "bm_bias") ok = ok && std::abs (f->get() - 0.8f) < 1.0e-4f;
                    if (f->paramID == "bm_speaker") ok = ok && std::abs (f->get() - 2.0f) < 1.0e-4f;
                }
            expect (ok, "page-2 parameters round-trip through getState/setState");
        }


        beginTest ("Input selector: Normal / Jumped / Bright each drive only the channel(s) they say");
        {
            // Volume (Normal) all the way up, Volume (Bright) at 0 -- with the real amp's own topology (each channel's
            // grid stopper returns to its OWN jack, split 2026-09-22 from the single shared node they both used to share),
            // "Bright" input should be silent regardless of what Volume (Normal) is doing, and vice versa.
            auto levelWith = [] (float inputChoice, float volNormal, float volBright)
            {
                BassmanStyleAmplifierProcessor amp;
                setParam (amp, "bm_input", inputChoice);
                setParam (amp, "bm_vol_normal", volNormal);
                setParam (amp, "bm_vol_bright", volBright);
                amp.prepare (sr, 512, 1);
                juce::AudioBuffer<float> buf (1, 512);
                double peak = 0.0;
                for (int b = 0; b < 40; ++b)
                {
                    for (int i = 0; i < 512; ++i)
                        buf.setSample (0, i, (float) (0.15 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (b * 512 + i) / sr)));
                    amp.process (buf);
                    for (int i = 0; i < 512; ++i)
                        peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
                return peak;
            };

            // Normal's own Volume is held at 1 throughout (it is the connected channel); only BRIGHT's Volume moves, and Input
            // = Normal should make that move inaudible since nothing is patched into the Bright jack.
            const double normalOnly = levelWith (0.0f, 1.0f, 1.0f);
            const double normalOnlyQuiet = levelWith (0.0f, 1.0f, 0.0f);
            // Symmetric: Bright's own Volume held at 1, only NORMAL's Volume moves, Input = Bright.
            const double brightOnly = levelWith (2.0f, 1.0f, 1.0f);
            const double brightOnlyQuiet = levelWith (2.0f, 0.0f, 1.0f);
            const double jumped = levelWith (1.0f, 1.0f, 1.0f);       // Input = Jumped: both channels driven at once

            logMessage ("Normal-only (Bright vol 1 vs 0): " + juce::String (normalOnly, 4) + " / " + juce::String (normalOnlyQuiet, 4)
                        + ", Bright-only (Normal vol 1 vs 0): " + juce::String (brightOnly, 4) + " / " + juce::String (brightOnlyQuiet, 4)
                        + ", Jumped: " + juce::String (jumped, 4));

            // A small (<1%) difference is real and expected even with no signal into the disconnected channel's grid: its
            // triode's plate resistor and volume pot still LOAD the shared mixing node (a real tube sitting there does
            // too), independent of whatever its grid is doing. What must NOT happen is anything like the full-signal
            // level moving by patching that channel's Volume knob.
            expectWithinAbsoluteError (normalOnly, normalOnlyQuiet, normalOnlyQuiet * 0.02);
            expectWithinAbsoluteError (brightOnly, brightOnlyQuiet, brightOnlyQuiet * 0.02);
            expectGreaterThan (normalOnly, 0.01);
            expectGreaterThan (brightOnly, 0.01);
            // Jumped drives both channels' triodes from the same signal into the shared mixing node -- measurably louder
            // than either channel alone (the exact ratio depends on how the mixing resistors load each other; not
            // asserting a specific number, just that patching in the second channel does something).
            expectGreaterThan (jumped, std::max (normalOnly, brightOnly) * 1.005); // a PEAK at a saturated output: 1.5% above one channel since the triode below-cathode fix (2026-09-26), was 2%+
        }

        beginTest ("soak: ten seconds of plucked notes, hot settings, every speaker load: the solver never gets stuck");
        for (float speaker : { 0.0f, 1.0f, 2.0f })
        {
            BassmanStyleAmplifierProcessor amp;
            setParam (amp, "bm_speaker", speaker);
            setParam (amp, "bm_presence", 1.0f);
            setParam (amp, "bm_vol_normal", 0.8f);
            setParam (amp, "bm_vol_bright", 0.8f);
            amp.prepare (sr, 128, 2);
            juce::Random rnd (1234);
            juce::AudioBuffer<float> buf (2, 128);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {};
            long long n = 0;
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.45 * sr) == 0)
                    {
                        f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0);
                        level = 0.03 * std::pow (20.0, rnd.nextDouble());
                        age = 0.0;
                    }
                    age += 1.0 / sr;
                    double v = 0.0;
                    for (int k = 1; k <= 8; ++k)
                    {
                        phase[k] += twoPi * f0 * k / sr;
                        v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k));
                    }
                    buf.setSample (0, i, (float) (v * level));
                    buf.setSample (1, i, (float) (v * level));
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("speaker " + juce::String (speaker, 0) + ": failure rate " + juce::String (amp.getSolveFailureRate(), 6) + ", peak " + juce::String (peak, 2));
            expect (finite, "output stays finite");
            expect (amp.getSolveFailureRate() < 1.0e-4, "speaker " + juce::String (speaker, 0) + ": failure rate " + juce::String (amp.getSolveFailureRate()));
            expect (peak < 8.0, "output stays bounded");
        }

        beginTest ("a burst of garbage on the input (NaN, huge values) does not wedge the amp");
        {
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            for (int b = 0; b < 100; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    float v = (float) (0.1 * std::sin (0.06 * (double) (b * 128 + i)));
                    if (b >= 20 && b < 24)
                        v = i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : (i % 3 == 1 ? 1.0e6f : -1.0e6f);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
            }
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float v = (float) (0.1 * std::sin (0.06 * (double) (b * 128 + i)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            expect (finite, "finite after the garbage");
            expect (peak > 0.02 && peak < 4.0, "the amp plays normally again, peak " + juce::String (peak));
        }



        beginTest ("random knob settings, plucked notes, 8 and 16 ohm: no failed solves, no recoveries");
        {
            juce::Random rnd (2024);
            int bad = 0;
            for (int speaker : { 1, 2 })
                for (int combo = 0; combo < 20; ++combo)
                {
                    BassmanStyleAmplifierProcessor amp;
                    Settings s;
                    s.normal = rnd.nextFloat(); s.bright = rnd.nextFloat(); s.treble = rnd.nextFloat(); s.middle = rnd.nextFloat();
                    s.bass = rnd.nextFloat(); s.presence = rnd.nextFloat();
                    apply (amp, s);
                    setParam (amp, "bm_speaker", (float) speaker);
                    const float bias = rnd.nextFloat(), power = rnd.nextFloat(), feel = rnd.nextFloat();
                    setParam (amp, "bm_bias", bias);
                    setParam (amp, "bm_power", power);
                    setParam (amp, "bm_tube_feel", feel);
                    amp.prepare (sr, 128, 2);
                    juce::AudioBuffer<float> buf (2, 128);
                    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                    double f0 = 110.0, level = 0.2, age = 0.0, phase[9] = {}, peak = 0.0;
                    long long n = 0;
                    for (int b = 0; b < (int) (4.0 * sr / 128); ++b)
                    {
                        for (int i = 0; i < 128; ++i, ++n)
                        {
                            if (n % (long long) (0.45 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 3.0); level = 0.03 * std::pow (20.0, rnd.nextDouble()); age = 0.0; }
                            age += 1.0 / sr;
                            double v = 0.0;
                            for (int k = 1; k <= 8; ++k) { phase[k] += twoPi * f0 * k / sr; v += std::sin (phase[k]) / (double) k * std::exp (-age * (2.0 + k)); }
                            const double attack = std::min (1.0, age / 0.002); // a pick takes a couple of milliseconds
                            buf.setSample (0, i, (float) (v * level * attack));
                            buf.setSample (1, i, (float) (v * level * attack));
                        }
                        amp.process (buf);
                        for (int i = 0; i < 128; ++i)
                            peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                    }
                    if (amp.getSolveFailureRate() > 1.0e-4 || amp.debugRecoveries() > 0 || ! (peak < 40.0)) // a one-sample blip is held, inaudible; a streak is not
                    {
                        ++bad;
                        logMessage ("speaker " + juce::String (speaker) + " combo " + juce::String (combo) + " [n " + juce::String (s.normal, 2) + " b " + juce::String (s.bright, 2) + " t " + juce::String (s.treble, 2) + " m " + juce::String (s.middle, 2) + " ba " + juce::String (s.bass, 2) + " p " + juce::String (s.presence, 2) + " bias " + juce::String (bias, 2) + " power " + juce::String (power, 2) + " feel " + juce::String (feel, 2) + "]: failures " + juce::String (amp.getSolveFailureRate(), 5) + " recoveries " + juce::String (amp.debugRecoveries()) + " peak " + juce::String (peak, 2));
                    }
                }
            expectEquals (bad, 0);
        }

        beginTest ("a hot pedal into the amp (square-ish notes at up to 3 V): finite, bounded, no long failure streaks");
        {
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            double peak = 0.0;
            bool finite = true;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase))); // what a distortion pedal hands the amp
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = std::max (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("sanity rejects " + juce::String (amp.debugSanityRejects()) + ", worst sane speaker volts " + juce::String (amp.debugWorstSaneVolts(), 1)
                        + ", worst rejected " + juce::String (amp.debugWorstRejectedVolts(), 1));
            logMessage ("recoveries " + juce::String (amp.debugRecoveries()) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 5)
                        + ", peak " + juce::String (peak, 2) + ", Newton iterations/sample: preamp " + juce::String (amp.debugIterations (0), 2)
                        + ", power " + juce::String (amp.debugIterations (1), 2));
            // The audio thread has 2.67 ms per 128-sample block at 48 kHz and the power block costs ~1.5 us per Newton
            // iteration, so the average iteration count IS the CPU cost, deterministically (a wall-clock assert would be
            // flaky). A hot pedal into the amp used to leave bursts of failing samples burning 300 iterations each -- one
            // block took 13 ms and dropped out. See docs/circuits/Bassman5F6A.md.
            expectLessThan (amp.debugIterations (1), 12.0);
            expect (finite);
            expect (peak <= 150.0 * BassmanStyleAmplifierProcessor::outputScale * 2.0 + 1.0e-3); // a failing state never prints more than the speaker can
            expectLessThan (amp.getSolveFailureRate(), 2.0e-3);
        }

        beginTest ("stereo: identical channels are processed once and stay identical; different channels are independent");
        {
            BassmanStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const int n = 4096;
            juce::AudioBuffer<float> buf (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float v = (float) (0.1 * std::sin (0.05 * (double) i));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            amp.process (buf);
            bool same = true;
            for (int i = 0; i < n; ++i)
                same = same && juce::exactlyEqual (buf.getSample (0, i), buf.getSample (1, i));
            expect (same, "dual-mono output identical");

            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, (float) (0.1 * std::sin (0.05 * (double) i)));
                buf.setSample (1, i, 0.0f);
            }
            amp.process (buf);
            double left = 0.0, right = 0.0;
            for (int i = 0; i < n; ++i)
            {
                left += std::abs (buf.getSample (0, i));
                right += std::abs (buf.getSample (1, i));
            }
            expect (std::isfinite (left) && std::isfinite (right) && left > 2.0 * right, "the channels are independent once they differ");
        }
    }
};

static BassmanStyleAmplifierProcessorTests bassmanTests;

} // namespace openguitarmultifx
