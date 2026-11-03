#include "EffectRegistry.h"
#include "Effects/ENGLPowerballStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class ENGLPowerballStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    ENGLPowerballStyleAmplifierProcessorTests() : juce::UnitTest ("ENGLPowerballStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = ENGLPowerballStyleAmplifierProcessor::Probe;

    static void setParam (ENGLPowerballStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        ENGLPowerballStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", preamp rail " + juce::String (amp.railPreamp(), 1)
                        + ", U5A plate " + juce::String (amp.debugVoltage (P::u5aPlate), 1)
                        + ", U5B plate " + juce::String (amp.debugVoltage (P::u5bPlate), 1)
                        + ", U6A plate " + juce::String (amp.debugVoltage (P::u6aPlate), 1)
                        + ", U6B plate " + juce::String (amp.debugVoltage (P::u6bPlate), 1)
                        + ", U7A plate " + juce::String (amp.debugVoltage (P::u7aPlate), 1)
                        + ", U7B plate " + juce::String (amp.debugVoltage (P::u7bPlate), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1)
                        + ", idle plate current " + juce::String (1000.0 * amp.plateCurrentTotal(), 1) + " mA (both pairs)");
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 520.0);
            // The drawing's bias note: 28-34 mA each power tube (four tubes).
            expect (amp.plateCurrentTotal() > 4.0 * 0.020 && amp.plateCurrentTotal() < 4.0 * 0.045);
            expect (amp.railPreamp() > 100.0 && amp.railPreamp() < 480.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0, peakAt = 0.0, lastSecondPeak = 0.0, prevY = 0.0;
            int crossings = 0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double y = std::abs (buf.getSample (0, i));
                    if (y > peak) { peak = y; peakAt = (b * 128 + i) / sr; }
                    if (b * 128 > 9.0 * sr)
                    {
                        lastSecondPeak = juce::jmax (lastSecondPeak, y);
                        const double ys = buf.getSample (0, i);
                        if ((ys > 0.0) != (prevY > 0.0))
                            ++crossings;
                        prevY = ys;
                    }
                }
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + " at " + juce::String (peakAt, 3) + " s (last second "
                        + juce::String (lastSecondPeak, 6) + ", ~" + juce::String (crossings / 2) + " Hz), failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", speaker " + juce::String (amp.debugVoltage (P::speaker), 3) + " V, PI plates "
                        + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::phaseInverterPlateB), 1));
            expectLessThan (peak, 0.025);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note: finite, bounded, converges");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            setParam (amp, "engl_pb_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
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
                }
            }
            logMessage ("pluck peak " + juce::String (peak, 3) + ", finite " + juce::String (finite ? 1 : 0)
                        + ", failures " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (finite);
            expectLessThan (peak, 5.0);
        }

        beginTest ("every mode plays through its own path: finite, no failures, audible, logged levels");
        {
            struct Mode { const char* name; float channel, style, bottom; };
            const Mode modes[] = { { "Clean", 0, 0, 0 }, { "Crunch", 1, 0, 0 }, { "Lo Lead", 2, 0, 0 }, { "Hi Lead", 3, 0, 0 },
                                   { "Hi Lead Focused", 3, 1, 0 }, { "Hi Lead Bottom", 3, 0, 1 }, { "Crunch Bottom", 1, 0, 1 } };
            for (const auto& m : modes)
            {
                ENGLPowerballStyleAmplifierProcessor amp;
                setParam (amp, "engl_pb_channel", m.channel);
                setParam (amp, "engl_pb_style", m.style);
                setParam (amp, "engl_pb_bottom", m.bottom);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double phase = 0.0, sum = 0.0, lo = 1.0e9, hi = -1.0e9;
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
                        const double v = amp.debugVoltage (P::toneStackOut);
                        lo = juce::jmin (lo, v);
                        hi = juce::jmax (hi, v);
                    }
                }
                const double rmsDb = 20.0 * std::log10 (std::sqrt (sum / (double) juce::jmax (1LL, n)) + 1.0e-12);
                logMessage (juce::String (m.name) + ": out " + juce::String (rmsDb, 1) + " dBFS, volume out p-p "
                            + juce::String (hi - lo, 2) + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
                expect (finite);
                expect (amp.dcConverged());
                expectLessThan (amp.getSolveFailureRate(), 1.0e-4);
                expectGreaterThan (rmsDb, -60.0);
            }
        }

        beginTest ("page 2 parameters move without crashing");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "engl_pb_power", 0.3f);
            setParam (amp, "engl_pb_bias", 0.7f);
            setParam (amp, "engl_pb_tube_feel", 0.4f);
            setParam (amp, "engl_pb_speaker", 0.0f);
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            setParam (amp, "engl_pb_speaker", 2.0f);
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            expect (true);
        }

        // ---- reducedOrder tests ----
        ENGLPowerballStyleAmplifierProcessor::reducedOrder = true;

        beginTest ("reducedOrder: finite output");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
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
            ENGLPowerballStyleAmplifierProcessor amp;
            setParam (amp, "engl_pb_gain", 0.6f);
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
            ENGLPowerballStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int sp = 0; sp <= 2; ++sp)
            {
                setParam (amp, "engl_pb_speaker", (float) sp);
                for (int b = 0; b < 50; ++b)
                    amp.process (buf);
            }
            expect (true);
        }

        beginTest ("reducedOrder: CPU cost is bounded");
        {
            ENGLPowerballStyleAmplifierProcessor amp;
            setParam (amp, "engl_pb_gain", 0.7f);
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
            ENGLPowerballStyleAmplifierProcessor::reducedOrder = false;
            beginTest ("ENV: full model diagnostic sweep");
            {
                ENGLPowerballStyleAmplifierProcessor amp;
                setParam (amp, "engl_pb_gain", 0.8f);
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

        ENGLPowerballStyleAmplifierProcessor::reducedOrder = false;
    }
};

static ENGLPowerballStyleAmplifierProcessorTests englPowerballStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
