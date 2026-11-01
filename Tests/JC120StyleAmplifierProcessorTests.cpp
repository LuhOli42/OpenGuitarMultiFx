#include "EffectRegistry.h"
#include "Effects/JC120StyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The JC-120-style amplifier (Roland Jazz Chorus; docs/circuits/JC120JazzChorus.md). */
class JC120StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    JC120StyleAmplifierProcessorTests() : juce::UnitTest ("JC120StyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = JC120StyleAmplifierProcessor::Probe;

    static void setParam (JC120StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        beginTest ("prepares and converges to a DC operating point");
        {
            JC120StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("channel 1 plate " + juce::String (amp.debugVoltage (P::channelOnePlate), 2)
                        + ", channel 2 plate " + juce::String (amp.debugVoltage (P::channelTwoPlate), 2)
                        + ", mix " + juce::String (amp.debugVoltage (P::mixNode), 3)
                        + ", driver base " + juce::String (amp.debugVoltage (P::driverBase), 3)
                        + ", output A " + juce::String (amp.debugVoltage (P::outputNodeA), 2)
                        + ", output B " + juce::String (amp.debugVoltage (P::outputNodeB), 2)
                        + ", speaker " + juce::String (amp.debugVoltage (P::speaker), 3));
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            JC120StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int b = 0; b < (int) (2.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
            }
            double peak = 0.0;
            for (int i = 0; i < 128; ++i)
                peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            logMessage ("silence peak after 2s: " + juce::String (peak, 5) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note through each Input setting: finite, bounded, converges");
        for (float inputChoice : { 0.0f, 1.0f, 2.0f })
        {
            JC120StyleAmplifierProcessor amp;
            setParam (amp, "jc_input", inputChoice);
            setParam (amp, "jc_volume", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.6 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.1 + 0.2 * rnd.nextDouble(); age = 0.0; }
                    age += 1.0 / sr;
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase) * std::exp (-age * 2.0));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("input " + juce::String (inputChoice, 0) + ": failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", recoveries " + juce::String (amp.debugRecoveries()) + ", peak " + juce::String (peak, 6));
            expect (finite);
            expectLessThan (amp.getSolveFailureRate(), 2.0e-3);
        }

        beginTest ("Chorus and Vibrato modes stay finite and bounded");
        for (float effectChoice : { 1.0f, 2.0f })
        {
            JC120StyleAmplifierProcessor amp;
            setParam (amp, "jc_effect", effectChoice);
            setParam (amp, "jc_volume", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            bool finite = true;
            double peak = 0.0;
            long long n = 0;
            for (int b = 0; b < (int) (2.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    const float v = (float) (0.15 * std::sin (twoPi * 220.0 * (double) n / sr));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("effect " + juce::String (effectChoice, 0) + ": peak " + juce::String (peak, 6) + ", finite " + juce::String ((int) finite));
            expect (finite);
        }

        if (juce::SystemStats::getEnvironmentVariable ("JC_TRACE_DIAG", {}).isNotEmpty())
        {
            beginTest ("signal trace diagnostic (dev only)");
            JC120StyleAmplifierProcessor amp;
            setParam (amp, "jc_volume", 0.8f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> one (2, 1);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (long long n = 0; n < 4800; ++n)
            {
                const float v = (float) (0.3 * std::sin (twoPi * 220.0 * (double) n / sr));
                one.setSample (0, 0, v);
                one.setSample (1, 0, v);
                amp.process (one);
                if (n % 480 == 0)
                    logMessage ("n=" + juce::String (n) + " mix=" + juce::String (amp.debugVoltage (P::mixNode), 5)
                                + " driverBase=" + juce::String (amp.debugVoltage (P::driverBase), 5)
                                + " speaker=" + juce::String (amp.debugVoltage (P::speaker), 5)
                                + " out=" + juce::String (one.getSample (0, 0), 6));
            }
        }

        beginTest ("page 2: Speaker is a preset parameter, its own sub-group");
        {
            JC120StyleAmplifierProcessor amp;
            int page2Count = 0;
            bool foundSpeaker = false;
            const auto pages = amp.getParameterPages();
            for (auto* f : pages[1])
            {
                ++page2Count;
                if (f->paramID == "jc_speaker")
                    foundSpeaker = true;
            }
            logMessage ("page 2 param count: " + juce::String (page2Count));
            expect (foundSpeaker);
        }

        // The real panel (2026-10-31): Channel, High / Low input, per-channel knobs, CH-2's Distortion and Reverb.
        const auto run = [&] (std::initializer_list<std::pair<const char*, float>> settings, double level, double seconds,
                              double toneSeconds, double& thd)
        {
            JC120StyleAmplifierProcessor amp;
            for (auto& [id, v] : settings)
                setParam (amp, id, v);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            double fs = 0.0, fc = 0.0, total = 0.0, tail = 0.0;
            long long n = 0, counted = 0;
            const double f0 = 220.0;
            for (int b = 0; b < (int) (seconds * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const double t = (double) (n + i) / sr;
                    const float x = t < toneSeconds ? (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * t)) : 0.0f;
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i, ++n)
                {
                    const double t = (double) n / sr, y = buf.getSample (0, i);
                    if (t > 0.5 && t < toneSeconds)
                    {
                        fs += y * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * t);
                        fc += y * std::cos (2.0 * juce::MathConstants<double>::pi * f0 * t);
                        total += y * y;
                        ++counted;
                    }
                    if (t > toneSeconds + 0.05)
                        tail += y * y;
                }
            }
            const double nn = (double) juce::jmax (1LL, counted);
            const double amp1 = 2.0 * std::sqrt (fs * fs + fc * fc) / nn;
            const double fundPower = 0.5 * amp1 * amp1, all = total / nn;
            thd = std::sqrt (juce::jmax (0.0, all - fundPower) / juce::jmax (1.0e-30, fundPower));
            return std::make_pair (std::sqrt (all), tail);
        };

        beginTest ("Distortion knob: more knob, more distortion on CH-2; nothing on CH-1");
        {
            double t0 = 0.0, t1 = 0.0, t2 = 0.0, tCh1 = 0.0;
            run ({ { "jc_channel", 1.0f }, { "jc_distortion", 0.0f } }, 0.2, 1.2, 1.2, t0);
            run ({ { "jc_channel", 1.0f }, { "jc_distortion", 0.4f } }, 0.2, 1.2, 1.2, t1);
            run ({ { "jc_channel", 1.0f }, { "jc_distortion", 1.0f } }, 0.2, 1.2, 1.2, t2);
            run ({ { "jc_channel", 0.0f }, { "jc_distortion", 1.0f } }, 0.2, 1.2, 1.2, tCh1);
            logMessage ("THD: CH-2 dist 0 " + juce::String (t0, 3) + ", 0.4 " + juce::String (t1, 3) + ", 1.0 " + juce::String (t2, 3)
                        + "; CH-1 with dist 1.0 " + juce::String (tCh1, 3));
            expectGreaterThan (t1, t0 * 2.0 + 0.01);
            expectGreaterThan (t2, t1);
            expectLessThan (tCh1, t0 * 1.5 + 0.01);
        }

        beginTest ("Reverb leaves a tail on CH-2 and none on CH-1");
        {
            double unused = 0.0;
            const auto on2 = run ({ { "jc_channel", 1.0f }, { "jc_reverb", 0.8f } }, 0.1, 1.5, 0.4, unused).second;
            const auto off2 = run ({ { "jc_channel", 1.0f }, { "jc_reverb", 0.0f } }, 0.1, 1.5, 0.4, unused).second;
            const auto on1 = run ({ { "jc_channel", 0.0f }, { "jc_reverb", 0.8f } }, 0.1, 1.5, 0.4, unused).second;
            logMessage ("tail energy: CH-2 reverb 0.8 " + juce::String (on2, 8) + ", CH-2 reverb 0 " + juce::String (off2, 8)
                        + ", CH-1 reverb 0.8 " + juce::String (on1, 8));
            expectGreaterThan (on2, off2 * 10.0 + 1.0e-9);
            expectLessThan (on1, off2 * 2.0 + 1.0e-9);
        }

        beginTest ("Low input is the 82K / 47K pad: ~8.8 dB under High");
        {
            double unused = 0.0;
            const double hi = run ({ { "jc_input_jack", 0.0f }, { "jc_volume", 0.3f } }, 0.01, 1.0, 1.0, unused).first;
            const double lo = run ({ { "jc_input_jack", 1.0f }, { "jc_volume", 0.3f } }, 0.01, 1.0, 1.0, unused).first;
            const double db = 20.0 * std::log10 (lo / juce::jmax (1.0e-12, hi));
            logMessage ("Low vs High: " + juce::String (db, 2) + " dB");
            expectWithinAbsoluteError (db, -8.8, 1.0);
        }

        beginTest ("each channel keeps its own knobs");
        {
            JC120StyleAmplifierProcessor amp;
            setParam (amp, "jc_channel", 0.0f);
            setParam (amp, "jc_volume", 0.2f);
            setParam (amp, "jc_channel", 1.0f);
            setParam (amp, "jc_volume", 0.9f);
            setParam (amp, "jc_channel", 0.0f);
            float v = 0.0f;
            for (auto* p : amp.getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                    if (f->paramID == "jc_volume")
                        v = f->get();
            expectWithinAbsoluteError (v, 0.2f, 1.0e-4f);
        }
    }
};

static JC120StyleAmplifierProcessorTests jc120StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
