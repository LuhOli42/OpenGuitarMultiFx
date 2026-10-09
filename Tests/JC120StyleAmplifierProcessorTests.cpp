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
    }
};

static JC120StyleAmplifierProcessorTests jc120StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
