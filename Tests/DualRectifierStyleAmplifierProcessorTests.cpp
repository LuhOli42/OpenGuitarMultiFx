#include "EffectRegistry.h"
#include "Effects/DualRectifierStyleAmplifierProcessor.h"

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
