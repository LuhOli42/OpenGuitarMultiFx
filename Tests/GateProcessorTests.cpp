#include "Effects/NoiseGates/GateProcessor.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class GateProcessorTests : public juce::UnitTest
{
public:
    GateProcessorTests() : juce::UnitTest ("GateProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    // Feeds `seconds` of a constant signal whose level wobbles +/- wobbleDb
    // around centreDb at 2 Hz, and counts how many times the gate
    // transitions open -> closed, judged per 5 ms block on the
    // output/input RMS ratio (internal gain isn't exposed). A constant
    // level, not a carrier, so the detector sees the wobble itself rather
    // than carrier-rate ripple between peaks.
    static int countOpenToCloseTransitions (GateProcessor& gate, double centreDb, double wobbleDb,
                                            double seconds = 2.0, int blockSize = 240)
    {
        const int total = (int) (seconds * sr);
        const double wobble = 2.0 * juce::MathConstants<double>::pi * 2.0 / sr;
        juce::AudioBuffer<float> buf (1, blockSize);
        bool wasOpen = false;
        int transitions = 0;
        for (int n = 0; n < total; n += blockSize)
        {
            double eIn = 0.0, eOut = 0.0;
            for (int i = 0; i < blockSize; ++i)
            {
                const float x = (float) std::pow (10.0, (centreDb + wobbleDb * std::sin (wobble * (n + i))) / 20.0);
                buf.setSample (0, i, x);
                eIn += (double) x * x;
            }
            gate.process (buf);
            for (int i = 0; i < blockSize; ++i)
                eOut += (double) buf.getSample (0, i) * buf.getSample (0, i);

            const bool open = eOut > 0.25 * eIn;
            if (wasOpen && ! open)
                ++transitions;
            wasOpen = open;
        }
        return transitions;
    }

    void runTest() override
    {
        beginTest ("a loud sustained signal ends up passing through near unity");
        {
            GateProcessor gate;
            gate.prepare (48000.0, 512, 1);

            // gate_threshold defaults to -50dB; feed a loud 0.5-amplitude
            // signal for long enough (attack default 1ms) that it settles open.
            juce::AudioBuffer<float> buffer (1, 4096);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.5f);

            gate.process (buffer);

            expectWithinAbsoluteError (buffer.getSample (0, buffer.getNumSamples() - 1), 0.5f, 0.02f);
        }

        beginTest ("a quiet signal well below threshold gets substantially attenuated");
        {
            GateProcessor gate;
            gate.prepare (48000.0, 512, 1);

            // -50dB threshold; feed a -70dB signal for long enough to close.
            const float quietAmplitude = juce::Decibels::decibelsToGain (-70.0f);
            juce::AudioBuffer<float> buffer (1, 8192);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, quietAmplitude);

            gate.process (buffer);

            const float outputLevel = std::abs (buffer.getSample (0, buffer.getNumSamples() - 1));
            expect (outputLevel < quietAmplitude * 0.5f);
        }

        beginTest ("defaults: Hysteresis 6 dB, Hold 10 ms");
        {
            GateProcessor gate;
            auto params = gate.getParameters()->getParameters (true);
            expectEquals (params.size(), 5);
            auto* hyst = dynamic_cast<juce::AudioParameterFloat*> (params[3]);
            auto* hold = dynamic_cast<juce::AudioParameterFloat*> (params[4]);
            expect (hyst != nullptr && hold != nullptr);
            expectEquals (hyst->paramID, juce::String ("gate_hysteresis"));
            expectEquals (hold->paramID, juce::String ("gate_hold"));
            expectWithinAbsoluteError (hyst->get(), 6.0f, 1.0e-6f);
            expectWithinAbsoluteError (hold->get(), 10.0f, 1.0e-6f);
        }

        beginTest ("hysteresis stops the gate chattering on a signal hovering at the threshold");
        {
            GateProcessor withHyst;
            withHyst.prepare (sr, 512, 1);
            setParams (withHyst, { -50.0f, 1.0f, 10.0f, 6.0f, 0.0f });
            const int t6 = countOpenToCloseTransitions (withHyst, -50.0, 2.0);

            GateProcessor noHyst;
            noHyst.prepare (sr, 512, 1);
            setParams (noHyst, { -50.0f, 1.0f, 10.0f, 0.0f, 0.0f });
            const int t0 = countOpenToCloseTransitions (noHyst, -50.0, 2.0);

            logMessage ("open->close transitions on a +/-2 dB wobble around -50 dB: "
                        "hysteresis 6 dB -> " + juce::String (t6) + ", 0 dB -> " + juce::String (t0));
            expectLessOrEqual (t6, 1);
            expectGreaterThan (t0, 1);
        }

        beginTest ("hold keeps the gate open for the hold time after the signal stops");
        {
            // gate_open isn't observable directly, so probe with a signal
            // below every threshold (-60 dB): it can't (re)open the gate or
            // reset the hold, so its output/input ratio is the current gate
            // gain itself.
            auto feed = [] (GateProcessor& g, float level, double ms)
            {
                const int n = (int) (ms * 0.001 * sr);
                juce::AudioBuffer<float> buf (1, n);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, level);
                g.process (buf);
            };
            auto probeGain = [&feed] (GateProcessor& g)
            {
                // -60 dB, 10 ms. Output RMS / input RMS = mean gate gain.
                const int n = (int) (0.010 * sr);
                juce::AudioBuffer<float> buf (1, n);
                const float level = juce::Decibels::decibelsToGain (-60.0f);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, level);
                g.process (buf);
                double eOut = 0.0;
                for (int i = 0; i < n; ++i)
                    eOut += (double) buf.getSample (0, i) * buf.getSample (0, i);
                return std::sqrt (eOut / ((double) n * level * level));
            };

            GateProcessor held;
            held.prepare (sr, 512, 1);
            setParams (held, { -50.0f, 1.0f, 10.0f, 0.0f, 100.0f });
            feed (held, 0.5f, 300.0);          // open
            feed (held, 0.0f, 50.0);           // 50 ms into the 100 ms hold
            expectWithinAbsoluteError (probeGain (held), 1.0, 0.02);
            feed (held, 0.0f, 130.0);          // past 100 ms hold + 10 ms release + margin
            expectLessThan (probeGain (held), 0.01); // < -40 dB

            GateProcessor immediate;
            immediate.prepare (sr, 512, 1);
            setParams (immediate, { -50.0f, 1.0f, 10.0f, 0.0f, 0.0f });
            feed (immediate, 0.5f, 300.0);
            feed (immediate, 0.0f, 60.0);      // hold 0: 60 ms >> 10 ms release, already closed
            expectLessThan (probeGain (immediate), 0.01);
        }
    }
};

static GateProcessorTests gateProcessorTests;

} // namespace openguitarmultifx
