#include "EffectRegistry.h"
#include "Effects/DumbleSteelStringStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class DumbleSteelStringStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    DumbleSteelStringStyleAmplifierProcessorTests() : juce::UnitTest ("DumbleSteelStringStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = DumbleSteelStringStyleAmplifierProcessor::Probe;

    static void setParam (DumbleSteelStringStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        DumbleSteelStringStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            DumbleSteelStringStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPi(), 1) + ", V2 rail " + juce::String (amp.railV2(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1b plate " + juce::String (amp.debugVoltage (P::v1bPlate), 1)
                        + ", V2a plate " + juce::String (amp.debugVoltage (P::v2aPlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            expect (amp.railPlates() > 420.0 && amp.railPlates() < 540.0);
            expect (amp.railV2() > 100.0 && amp.railV2() < 450.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            DumbleSteelStringStyleAmplifierProcessor amp;
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
            DumbleSteelStringStyleAmplifierProcessor amp;
            setParam (amp, "sss_volume", 0.6f);
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
            DumbleSteelStringStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            setParam (amp, "sss_power", 0.3f);
            setParam (amp, "sss_bias", 0.7f);
            setParam (amp, "sss_tube_feel", 0.4f);
            setParam (amp, "sss_speaker", 0.0f); // 4 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            setParam (amp, "sss_speaker", 2.0f); // 16 ohm
            for (int b = 0; b < 100; ++b)
                amp.process (buf);
            expect (true);
        }

        beginTest ("front-panel switches stay stable and change the signal");
        {
            auto pluckRms = [] (DumbleSteelStringStyleAmplifierProcessor& amp, double& peak, bool& finite)
            {
                juce::AudioBuffer<float> buf (2, 128);
                double sumSq = 0.0;
                long long n = 0;
                finite = true;
                peak = 0.0;
                double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
                for (int b = 0; b < (int) (2.0 * sr / 128); ++b)
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
                        sumSq += v * v;
                        ++n;
                    }
                }
                return std::sqrt (sumSq / (double) n);
            };

            double basePeak = 0.0, swPeak = 0.0;
            bool baseFinite = true, swFinite = true;
            double baseRms;
            {
                DumbleSteelStringStyleAmplifierProcessor amp;
                setParam (amp, "sss_volume", 0.6f);
                amp.prepare (sr, 128, 2);
                baseRms = pluckRms (amp, basePeak, baseFinite);
            }
            expect (baseFinite);
            expect (baseRms > 1.0e-4);

            const struct { const char* id; float v; const char* label; } switches[] = {
                { "sss_bright", 1.0f, "bright" },
                { "sss_deep", 1.0f, "deep" },
                { "sss_accent", 1.0f, "accent" },
                { "sss_guitar_mic", 1.0f, "mic" },
                { "sss_reverb", 1.0f, "reverb" },
                { "sss_filter_hi", 3.0f, "filter hi pos4" },
                { "sss_filter_lo", 3.0f, "filter lo pos4" },
            };
            double micRms = -1.0;
            for (const auto& sw : switches)
            {
                DumbleSteelStringStyleAmplifierProcessor amp;
                setParam (amp, "sss_volume", 0.6f);
                setParam (amp, sw.id, sw.v);
                amp.prepare (sr, 128, 2);
                const double rms = pluckRms (amp, swPeak, swFinite);
                logMessage (juce::String (sw.label) + " rms " + juce::String (rms, 4)
                            + " (base " + juce::String (baseRms, 4) + "), peak " + juce::String (swPeak, 3)
                            + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
                expect (swFinite, sw.label);
                expectLessThan (swPeak, 5.0);
                expect (rms > 1.0e-4);
                expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
                if (juce::String (sw.id) == "sss_guitar_mic")
                    micRms = rms;
            }
            expect (std::abs (micRms / baseRms - 1.0) > 0.05); // Mic rewires the stack: the level must move
        }

        beginTest ("reverb: a tail after the note stops; Off keeps only what is already in the tank");
        {
            const auto tail = [&] (bool on, float send, float ret)
            {
                DumbleSteelStringStyleAmplifierProcessor amp;
                setParam (amp, "sss_reverb", on ? 1.0f : 0.0f);
                setParam (amp, "sss_reverb_send", send);
                setParam (amp, "sss_reverb_return", ret);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double e = 0.0;
                long long n = 0;
                for (int b = 0; b < (int) (1.5 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        const float x = n < (long long) (0.3 * sr) ? (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) n / sr)) : 0.0f;
                        buf.setSample (0, i, x);
                        buf.setSample (1, i, x);
                    }
                    amp.process (buf);
                    if (n > (long long) (0.45 * sr))
                        for (int i = 0; i < 128; ++i)
                            e += juce::square ((double) buf.getSample (0, i));
                }
                expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
                return e;
            };
            const double off = tail (false, 1.0f, 1.0f), half = tail (true, 0.7f, 0.7f), full = tail (true, 1.0f, 1.0f);
            logMessage ("tail energy: off " + juce::String (off, 10) + ", send/return 0.7 " + juce::String (half, 10)
                        + ", full " + juce::String (full, 10));
            expectGreaterThan (half, off * 2.0 + 1.0e-12);
            expectGreaterThan (full, half * 1.5);
        }

        beginTest ("reverb: at full Send and Return the wet level is about the dry level");
        {
            // Two amps in lockstep, reverb Off and On: below clipping the dry paths are identical, so the difference
            // is the wet signal.
            DumbleSteelStringStyleAmplifierProcessor dryAmp, wetAmp;
            for (auto* amp : { &dryAmp, &wetAmp })
            {
                setParam (*amp, "sss_reverb_send", 1.0f);
                setParam (*amp, "sss_reverb_return", 1.0f);
                setParam (*amp, "sss_master", 0.3f);
            }
            setParam (wetAmp, "sss_reverb", 1.0f);
            dryAmp.prepare (sr, 128, 2);
            wetAmp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> dryBuf (2, 128), wetBuf (2, 128);
            double drySum = 0.0, wetSum = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    const float x = (float) (0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (double) n / sr));
                    for (auto* buf : { &dryBuf, &wetBuf })
                    {
                        buf->setSample (0, i, x);
                        buf->setSample (1, i, x);
                    }
                }
                dryAmp.process (dryBuf);
                wetAmp.process (wetBuf);
                if (n > (long long) (1.5 * sr))
                    for (int i = 0; i < 128; ++i)
                    {
                        drySum += juce::square ((double) dryBuf.getSample (0, i));
                        wetSum += juce::square ((double) wetBuf.getSample (0, i) - (double) dryBuf.getSample (0, i));
                    }
            }
            const double ratio = std::sqrt (wetSum / juce::jmax (1.0e-30, drySum));
            logMessage ("1 kHz sustained, full Send / Return: wet / dry " + juce::String (ratio, 3));
            expectWithinAbsoluteError (ratio, 1.0, 0.25);
        }

        beginTest ("old presets: Voicing Jazz becomes Mic, PAB and Rock become Guitar");
        {
            for (int v = 0; v < 3; ++v)
            {
                DumbleSteelStringStyleAmplifierProcessor amp;
                juce::XmlElement state ("EffectState");
                state.setAttribute ("sss_voicing", v);
                state.setAttribute ("sss_mid_boost", 1.0);
                amp.setState (state);
                const auto saved = amp.getState();
                expectEquals (saved->getDoubleAttribute ("sss_guitar_mic"), v == 0 ? 1.0 : 0.0);

                std::vector<std::pair<juce::String, float>> snap { { "sss_voicing", (float) v } };
                amp.migrateLegacySnapshotParams (snap);
                expect (snap[0].first == "sss_guitar_mic");
                expectEquals (snap[0].second, v == 0 ? 1.0f : 0.0f);
            }
        }

        // ---- reducedOrder tests ----
        DumbleSteelStringStyleAmplifierProcessor::reducedOrder = true;

        beginTest ("reducedOrder: finite output");
        {
            DumbleSteelStringStyleAmplifierProcessor amp;
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
            DumbleSteelStringStyleAmplifierProcessor amp;
            setParam (amp, "sss_volume", 0.6f);
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
            DumbleSteelStringStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int sp = 0; sp <= 2; ++sp)
            {
                setParam (amp, "sss_speaker", (float) sp);
                for (int b = 0; b < 50; ++b)
                    amp.process (buf);
            }
            expect (true);
        }

        beginTest ("reducedOrder: CPU cost is bounded");
        {
            DumbleSteelStringStyleAmplifierProcessor amp;
            setParam (amp, "sss_volume", 0.7f);
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
            DumbleSteelStringStyleAmplifierProcessor::reducedOrder = false;
            beginTest ("ENV: full model diagnostic sweep");
            {
                DumbleSteelStringStyleAmplifierProcessor amp;
                setParam (amp, "sss_volume", 0.8f);
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

        DumbleSteelStringStyleAmplifierProcessor::reducedOrder = false;
    }
};

static DumbleSteelStringStyleAmplifierProcessorTests dualRectifierStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
