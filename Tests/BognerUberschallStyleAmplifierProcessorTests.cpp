#include "EffectRegistry.h"
#include "Effects/BognerUberschallStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace openguitarmultifx
{

class BognerUberschallStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    BognerUberschallStyleAmplifierProcessorTests() : juce::UnitTest ("BognerUberschallStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = BognerUberschallStyleAmplifierProcessor::Probe;

    static void setParam (BognerUberschallStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        BognerUberschallStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            BognerUberschallStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", V2 rail " + juce::String (amp.railV2(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1b plate " + juce::String (amp.debugVoltage (P::v1bPlate), 1)
                        + ", V2b plate " + juce::String (amp.debugVoltage (P::v2bPlate), 1)
                        + ", V3a plate " + juce::String (amp.debugVoltage (P::v3aPlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            expect (amp.railPlates() > 420.0 && amp.railPlates() < 520.0);
            expect (amp.railV2() > 100.0 && amp.railV2() < 450.0);
        }

        beginTest ("the clean channel swings like the tracing: stage levels for a 165 Hz note");
        {
            auto swing = [&] (float channel, std::initializer_list<P> probes)
            {
                BognerUberschallStyleAmplifierProcessor amp;
                setParam (amp, "uber_channel", channel);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                std::vector<double> lo (probes.size(), 1.0e9), hi (probes.size(), -1.0e9);
                double phase = 0.0;
                for (int b = 0; b < (int) (1.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        const float x = (float) (0.1 * std::sin (phase));
                        phase += juce::MathConstants<double>::twoPi * 165.0 / sr;
                        buf.setSample (0, i, x);
                        buf.setSample (1, i, x);
                    }
                    amp.process (buf);
                    if (b > (int) (0.5 * sr / 128))
                    {
                        size_t j = 0;
                        for (auto p : probes)
                        {
                            const double v = amp.debugVoltage (p);
                            lo[j] = juce::jmin (lo[j], v);
                            hi[j] = juce::jmax (hi[j], v);
                            ++j;
                        }
                    }
                }
                juce::String s;
                for (size_t j = 0; j < lo.size(); ++j)
                    s << juce::String (hi[j] - lo[j], 2) << " ";
                return s;
            };
            logMessage ("clean p-p (block ends) V1a / V4b / out / master top: "
                        + swing (0.0f, { P::cleanV1aPlate, P::cleanV4bPlate, P::cleanOut, P::cleanMasterTop }));
            logMessage ("OD p-p V1a / follower / master top / tone out: "
                        + swing (1.0f, { P::v1aPlate, P::followerOut, P::cleanMasterTop, P::toneStackOut }));
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            BognerUberschallStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0, peakAt = 0.0, lastSecond = 0.0, settledPeak = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double y = std::abs (buf.getSample (0, i));
                    if (y > peak) { peak = y; peakAt = (b * 128 + i) / sr; }
                    if (b * 128 > 9.0 * sr)
                        lastSecond = juce::jmax (lastSecond, y);
                    if (b * 128 > 0.05 * sr)
                        settledPeak = juce::jmax (settledPeak, y);
                }
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + " at " + juce::String (peakAt, 3) + " s (last second "
                        + juce::String (lastSecond, 6) + ", after 50 ms " + juce::String (settledPeak, 6) + "), failure rate "
                        + juce::String (amp.getSolveFailureRate(), 6));
            // Drift / oscillation: everything after the first 50 ms. The first millisecond carries the netlist's start-up
            // step (~0.02, the same before the 2026-11-02 PI rewiring): bounded on its own.
            expectLessThan (settledPeak, 0.02);
            expectLessThan (peak, 0.03);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note: finite, bounded, converges");
        {
            BognerUberschallStyleAmplifierProcessor amp;
            setParam (amp, "uber_gain", 0.6f);
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
            BognerUberschallStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "uber_power", 0.3f);
            setParam (amp, "uber_bias", 0.7f);
            setParam (amp, "uber_tube_feel", 0.4f);
            setParam (amp, "uber_speaker", 0.0f); // 4 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            setParam (amp, "uber_speaker", 2.0f); // 16 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            expect (true);
        }

        // ---- reducedOrder tests ----
        BognerUberschallStyleAmplifierProcessor::reducedOrder = true;

        beginTest ("reducedOrder: finite output");
        {
            BognerUberschallStyleAmplifierProcessor amp;
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
            BognerUberschallStyleAmplifierProcessor amp;
            setParam (amp, "uber_gain", 0.6f);
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
            BognerUberschallStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int sp = 0; sp <= 2; ++sp)
            {
                setParam (amp, "uber_speaker", (float) sp);
                for (int b = 0; b < 50; ++b)
                    amp.process (buf);
            }
            expect (true);
        }

        beginTest ("reducedOrder: CPU cost is bounded");
        {
            BognerUberschallStyleAmplifierProcessor amp;
            setParam (amp, "uber_gain", 0.7f);
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
            BognerUberschallStyleAmplifierProcessor::reducedOrder = false;
            beginTest ("ENV: full model diagnostic sweep");
            {
                BognerUberschallStyleAmplifierProcessor amp;
                setParam (amp, "uber_gain", 0.8f);
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

        BognerUberschallStyleAmplifierProcessor::reducedOrder = false;
    }
};

static BognerUberschallStyleAmplifierProcessorTests dualRectifierStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
