#include "EffectRegistry.h"
#include "Effects/DualRectifierStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class DualRectifierStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    DualRectifierStyleAmplifierProcessorTests() : juce::UnitTest ("DualRectifierStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = DualRectifierStyleAmplifierProcessor::Probe;

    static void setParam (DualRectifierStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        DualRectifierStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            DualRectifierStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", V3 rail " + juce::String (amp.railV3(), 1)
                        + ", V2 rail " + juce::String (amp.railV2(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V2a plate " + juce::String (amp.debugVoltage (P::v2aPlate), 1)
                        + ", V2b plate " + juce::String (amp.debugVoltage (P::v2bPlate), 1)
                        + ", V3a plate " + juce::String (amp.debugVoltage (P::v3aPlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 520.0);
            expect (amp.railV2() > 100.0 && amp.railV2() < 450.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            DualRectifierStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.02);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note: finite, bounded, converges");
        {
            DualRectifierStyleAmplifierProcessor amp;
            setParam (amp, "drec_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0;
            for (int b = 0; b < (int) (5.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    double s = level * std::exp (-age / 0.8) * std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    age += 1.0 / sr;
                    buf.setSample (0, i, (float) s);
                    buf.setSample (1, i, (float) s);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double v = (double) buf.getSample (0, i);
                    if (! std::isfinite (v))
                        finite = false;
                    peak = juce::jmax (peak, std::abs (v));
                    ++n;
                }
            }
            logMessage ("pluck peak " + juce::String (peak, 3) + ", finite " + juce::String (finite ? 1 : 0)
                        + ", failures " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (finite);
            expectLessThan (peak, 5.0);
        }

        beginTest ("page 2 parameters move without crashing");
        {
            DualRectifierStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "drec_power", 0.3f);
            setParam (amp, "drec_bias", 0.7f);
            setParam (amp, "drec_tube_feel", 0.4f);
            setParam (amp, "drec_speaker", 0.0f); // 4 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            setParam (amp, "drec_speaker", 2.0f); // 16 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            expect (true);
        }

        beginTest ("every mode of the LDR truth table plays: finite, no failures, audible, logged levels");
        {
            struct Mode { const char* name; float channel, orangeGain, style; };
            const Mode modes[] = { { "Orange Normal", 0, 1, 0 }, { "Orange Clean", 0, 0, 0 }, { "Orange to Modern", 0, 1, 1 },
                                   { "Red Modern", 1, 1, 0 }, { "Red to Vintage", 1, 1, 2 } };
            for (const auto& m : modes)
            {
                DualRectifierStyleAmplifierProcessor amp;
                setParam (amp, "drec_channel", m.channel);
                setParam (amp, "drec_orange_gain", m.orangeGain);
                setParam (amp, "drec_style", m.style);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double phase = 0.0, sum = 0.0, v1 = 1.0e9, v1h = -1.0e9, f = 1.0e9, fh = -1.0e9;
                long long n = 0;
                bool finite = true;
                for (int b = 0; b < (int) (0.8 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        const float x = (float) (0.1 * std::sin (phase));
                        phase += juce::MathConstants<double>::twoPi * 165.0 / sr;
                        buf.setSample (0, i, x);
                        buf.setSample (1, i, x);
                    }
                    amp.process (buf);
                    if (b > (int) (0.4 * sr / 128))
                    {
                        for (int i = 0; i < 128; ++i)
                        {
                            const double y = buf.getSample (0, i);
                            finite = finite && std::isfinite (y);
                            sum += y * y;
                            ++n;
                        }
                        const double a = amp.debugVoltage (P::v1aPlate), t = amp.debugVoltage (P::toneStackOut);
                        v1 = juce::jmin (v1, a); v1h = juce::jmax (v1h, a);
                        f = juce::jmin (f, t); fh = juce::jmax (fh, t);
                    }
                }
                const double rmsDb = 20.0 * std::log10 (std::sqrt (sum / (double) juce::jmax (1LL, n)) + 1.0e-12);
                logMessage (juce::String (m.name) + ": out " + juce::String (rmsDb, 1) + " dBFS, V1a p-p " + juce::String (v1h - v1, 1)
                            + ", master bus p-p " + juce::String (fh - f, 2) + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
                expect (finite);
                expect (amp.dcConverged());
                expectLessThan (amp.getSolveFailureRate(), 1.0e-4);
                expectGreaterThan (rmsDb, -60.0);
            }
        }

        beginTest ("small-signal power-amp voicing per feedback mode (full vs reducedOrder): logged");
        {
            // Red, low Gain and Master so nothing clips: what is left between modes is the NFB loop and the tube type.
            struct Case { const char* name; float style, channel, orangeGain, tubes; };
            const Case cases[] = { { "Red Modern", 0, 1, 1, 0 }, { "Red Vintage", 2, 1, 1, 0 }, { "Orange Clean", 0, 0, 0, 0 },
                                   { "Red Modern EL34", 0, 1, 1, 1 }, { "Red Vintage EL34", 2, 1, 1, 1 } };
            for (const auto& cs : cases)
            {
                juce::String line = juce::String (cs.name) + ":";
                for (bool reduced : { false, true })
                {
                    DualRectifierStyleAmplifierProcessor::reducedOrder = reduced;
                    for (double f0 : { 100.0, 1000.0, 4000.0 })
                    {
                        DualRectifierStyleAmplifierProcessor amp;
                        setParam (amp, "drec_channel", cs.channel);
                        setParam (amp, "drec_orange_gain", cs.orangeGain);
                        setParam (amp, "drec_style", cs.style);
                        setParam (amp, "drec_bias_switch", cs.tubes);
                        setParam (amp, "drec_gain", 0.05f);
                        setParam (amp, "drec_master", 0.25f);
                        amp.prepare (sr, 128, 2);
                        juce::AudioBuffer<float> buf (2, 128);
                        double phase = 0.0, sum = 0.0;
                        long long n = 0;
                        for (int b = 0; b < (int) (0.6 * sr / 128); ++b)
                        {
                            for (int i = 0; i < 128; ++i)
                            {
                                const float x = (float) (0.003 * std::sin (phase));
                                phase += juce::MathConstants<double>::twoPi * f0 / sr;
                                buf.setSample (0, i, x);
                                buf.setSample (1, i, x);
                            }
                            amp.process (buf);
                            if (b > (int) (0.3 * sr / 128))
                                for (int i = 0; i < 128; ++i, ++n)
                                    sum += juce::square ((double) buf.getSample (0, i));
                        }
                        line << " " << (reduced ? "R" : "F") << juce::String ((int) f0) << " "
                             << juce::String (10.0 * std::log10 (sum / (double) juce::jmax (1LL, n) + 1.0e-30), 1);
                    }
                }
                logMessage (line);
            }
            DualRectifierStyleAmplifierProcessor::reducedOrder = false;
        }

        beginTest ("the Bias Switch: EL34s at -39 V idle sanely, and the switch survives a live change");
        {
            DualRectifierStyleAmplifierProcessor ref;
            ref.prepare (sr, 128, 2);
            logMessage ("6L6 idle at prepare " + juce::String (1000.0 * ref.plateCurrentTotal(), 1) + " mA");
            DualRectifierStyleAmplifierProcessor amp;
            setParam (amp, "drec_bias_switch", 1.0f);
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            const double el34 = amp.plateCurrentTotal();
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "drec_bias_switch", 0.0f);
            for (int b = 0; b < 200; ++b)
            {
                buf.clear();
                amp.process (buf);
            }
            const double l6 = amp.plateCurrentTotal();
            logMessage ("idle plate current (both pairs): EL34 " + juce::String (1000.0 * el34, 1) + " mA, 6L6 "
                        + juce::String (1000.0 * l6, 1) + " mA");
            expect (el34 > 0.12 && el34 < 0.2);   // four tubes at ~30-50 mA
            expect (l6 > 0.12 && l6 < 0.24);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-4);
        }

        // ---- reducedOrder tests ----
        DualRectifierStyleAmplifierProcessor::reducedOrder = true;

        beginTest ("reducedOrder: finite output");
        {
            DualRectifierStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int b = 0; b < 200; ++b)
                amp.process (buf);
            for (int i = 0; i < 128; ++i)
                expect (std::isfinite (buf.getSample (0, i)));
        }

        beginTest ("reducedOrder: plucked note");
        {
            DualRectifierStyleAmplifierProcessor amp;
            setParam (amp, "drec_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            double peak = 0.0, rms = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    double s = level * std::exp (-age / 0.8) * std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    age += 1.0 / sr;
                    buf.setSample (0, i, (float) s);
                    buf.setSample (1, i, (float) s);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double v = (double) buf.getSample (0, i);
                    peak = juce::jmax (peak, std::abs (v));
                    rms += v * v;
                    ++n;
                }
            }
            rms = std::sqrt (rms / (double) n);
            logMessage ("reducedOrder pluck peak " + juce::String (peak, 3) + ", rms " + juce::String (rms, 3));
            expectGreaterThan (peak, 0.01);
            expectLessThan (peak, 5.0);
        }

        beginTest ("reducedOrder: speaker impedance changes don't crash");
        {
            DualRectifierStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int sp = 0; sp <= 2; ++sp)
            {
                setParam (amp, "drec_speaker", (float) sp);
                for (int b = 0; b < 50; ++b)
                    amp.process (buf);
            }
            expect (true);
        }

        beginTest ("reducedOrder: CPU cost is bounded");
        {
            DualRectifierStyleAmplifierProcessor amp;
            setParam (amp, "drec_gain", 0.7f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            double f0 = 82.0, level = 0.2, age = 0.0, phase = 0.0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    double s = level * std::exp (-age / 1.5) * std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    age += 1.0 / sr;
                    buf.setSample (0, i, (float) s);
                    buf.setSample (1, i, (float) s);
                }
                amp.process (buf);
            }
            const auto t0 = juce::Time::getHighResolutionTicks();
            age = 0.0;
            phase = 0.0;
            constexpr int measureBlocks = 500;
            for (int b = 0; b < measureBlocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    double s = level * std::exp (-age / 1.5) * std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    age += 1.0 / sr;
                    buf.setSample (0, i, (float) s);
                    buf.setSample (1, i, (float) s);
                }
                amp.process (buf);
            }
            const double wall = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
            const double audio = (double) (measureBlocks * 128) / sr;
            const double pct = 100.0 * wall / audio;
            logMessage ("reducedOrder CPU " + juce::String (pct, 1) + "%");
            if (! testEnvironment::underEmulation())
                expectLessThan (pct, 50.0);
        }

        if (juce::SystemStats::getEnvironmentVariable ("OGMFX_DIAG", "").isNotEmpty())
        {
            DualRectifierStyleAmplifierProcessor::reducedOrder = false;
            beginTest ("ENV: full model diagnostic sweep");
            {
                DualRectifierStyleAmplifierProcessor amp;
                setParam (amp, "drec_gain", 0.8f);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double f0 = 82.0, level = 0.3, age = 0.0, phase = 0.0;
                for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        double s = level * std::exp (-age / 1.5) * std::sin (phase);
                        phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                        age += 1.0 / sr;
                        buf.setSample (0, i, (float) s);
                        buf.setSample (1, i, (float) s);
                    }
                    amp.process (buf);
                }
                logMessage ("diag: failures " + juce::String (amp.getSolveFailureRate(), 6)
                            + ", recoveries " + juce::String (amp.debugRecoveries())
                            + ", worst sane " + juce::String (amp.debugWorstSaneVolts(), 1));
            }
        }

        DualRectifierStyleAmplifierProcessor::reducedOrder = false;
    }
};

static DualRectifierStyleAmplifierProcessorTests dualRectifierStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
