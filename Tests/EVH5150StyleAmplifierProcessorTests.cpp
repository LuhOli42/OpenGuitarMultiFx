#include "EffectRegistry.h"
#include "Effects/EVH5150StyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class EVH5150StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    EVH5150StyleAmplifierProcessorTests() : juce::UnitTest ("EVH5150StyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = EVH5150StyleAmplifierProcessor::Probe;

    static void setParam (EVH5150StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        EVH5150StyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            EVH5150StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", V2 rail " + juce::String (amp.railV2(), 1)
                        + ", V1 rail " + juce::String (amp.railV1(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1b plate " + juce::String (amp.debugVoltage (P::v1bPlate), 1)
                        + ", V2a plate " + juce::String (amp.debugVoltage (P::v2aPlate), 1)
                        + ", V2b plate " + juce::String (amp.debugVoltage (P::v2bPlate), 1)
                        + ", V5b plate " + juce::String (amp.debugVoltage (P::v5bPlate), 1)
                        + ", V5a plate " + juce::String (amp.debugVoltage (P::v5aPlate), 1)
                        + ", recovery plate " + juce::String (amp.debugVoltage (P::recoveryPlate), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 520.0);
            expect (amp.railV1() > 100.0 && amp.railV1() < 480.0);
            // V2A is the cold clipper: 39K unbypassed cathode, a fraction of a milliamp, its plate near the rail --
            // the stage that a misplaced 39K (as a grid resistor) turned into one more hot gain stage.
            expectGreaterThan (amp.debugVoltage (P::v2aPlate), amp.debugVoltage (P::v1bPlate) + 50.0);
            expectGreaterThan (amp.debugVoltage (P::v2aPlate), amp.railV1() - 40.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            EVH5150StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0, peakAt = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    if (std::abs (buf.getSample (0, i)) > peak) { peak = std::abs (buf.getSample (0, i)); peakAt = (b * 128 + i) / sr; }
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + " at " + juce::String (peakAt, 3) + " s, failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.025);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note: finite, bounded, converges");
        {
            EVH5150StyleAmplifierProcessor amp;
            setParam (amp, "evh_gain", 0.6f);
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

        beginTest ("the global feedback is negative: closing it lowers the gain");
        {
            const auto rmsAt = [&] (double feedbackOhms)
            {
                EVH5150StyleAmplifierProcessor amp;
                setParam (amp, "evh_gain", 0.0f);
                setParam (amp, "evh_post", 0.3f);
                amp.prepare (sr, 128, 2);
                amp.debugSetFeedbackResistance (feedbackOhms);
                juce::AudioBuffer<float> buf (2, 128);
                double sum = 0.0;
                long long n = 0, counted = 0;
                for (int b = 0; b < (int) (1.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        const float s = (float) (0.003 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) n / sr));
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    amp.process (buf);
                    if (b > (int) (0.5 * sr / 128))
                        for (int i = 0; i < 128; ++i, ++counted)
                            sum += juce::square ((double) buf.getSample (0, i));
                }
                return std::sqrt (sum / (double) juce::jmax (1LL, counted));
            };
            const double closed = rmsAt (39.0e3), open = rmsAt (1.0e12);
            logMessage ("NFB closed " + juce::String (closed, 5) + ", open " + juce::String (open, 5)
                        + " (" + juce::String (20.0 * std::log10 (closed / juce::jmax (1.0e-12, open)), 2) + " dB)");
            expectLessThan (closed, open * 0.9);
        }

        beginTest ("page 2 parameters move without crashing");
        {
            EVH5150StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "evh_power", 0.3f);
            setParam (amp, "evh_bias", 0.7f);
            setParam (amp, "evh_tube_feel", 0.4f);
            setParam (amp, "evh_speaker", 0.0f); // 4 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            setParam (amp, "evh_speaker", 2.0f); // 16 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            expect (true);
        }

        // ---- reducedOrder tests ----
        EVH5150StyleAmplifierProcessor::reducedOrder = true;

        beginTest ("reducedOrder: finite output");
        {
            EVH5150StyleAmplifierProcessor amp;
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
            EVH5150StyleAmplifierProcessor amp;
            setParam (amp, "evh_gain", 0.6f);
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
            EVH5150StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int sp = 0; sp <= 2; ++sp)
            {
                setParam (amp, "evh_speaker", (float) sp);
                for (int b = 0; b < 50; ++b)
                    amp.process (buf);
            }
            expect (true);
        }

        beginTest ("reducedOrder: CPU cost is bounded");
        {
            EVH5150StyleAmplifierProcessor amp;
            setParam (amp, "evh_gain", 0.7f);
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
            EVH5150StyleAmplifierProcessor::reducedOrder = juce::SystemStats::getEnvironmentVariable ("OGMFX_DIAG", "") == "reduced";
            beginTest ("ENV: full model diagnostic sweep");
            {
                EVH5150StyleAmplifierProcessor amp;
                setParam (amp, "evh_gain", 0.8f);
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
                logMessage ("diag: iterations A " + juce::String (amp.debugIterations (0), 2) + " B " + juce::String (amp.debugIterations (1), 2)
                            + " C " + juce::String (amp.debugIterations (2), 2) + " D " + juce::String (amp.debugIterations (3), 2));
                logMessage ("diag: failures " + juce::String (amp.getSolveFailureRate(), 6)
                            + ", recoveries " + juce::String (amp.debugRecoveries())
                            + ", worst sane " + juce::String (amp.debugWorstSaneVolts(), 1));
            }
        }

        EVH5150StyleAmplifierProcessor::reducedOrder = false;
    }
};

static EVH5150StyleAmplifierProcessorTests evh5150StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
