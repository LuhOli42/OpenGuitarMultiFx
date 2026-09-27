#include "Effects/DS201StyleNoiseGateProcessor.h"
#include "Effects/NS2StyleNoiseSuppressorProcessor.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class NoiseGateTests : public juce::UnitTest
{
public:
    NoiseGateTests() : juce::UnitTest ("NoiseGates", "Effects") {}

    static constexpr double sr = 48000.0;

    /** Runs `seconds` of a sine (frequency, peak level in dB) through the processor and returns the RMS gain of the last `tail` seconds. */
    static double run (EffectProcessor& p, double freq, double levelDb, double seconds, double tail = 0.1, double* finalGainDb = nullptr)
    {
        const int total = (int) (seconds * sr), start = total - (int) (tail * sr);
        const double amp = std::pow (10.0, levelDb / 20.0);
        juce::AudioBuffer<float> buf (1, 1);
        double eIn = 0.0, eOut = 0.0;
        for (int n = 0; n < total; ++n)
        {
            const double x = amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) n / sr);
            buf.setSample (0, 0, (float) x);
            p.process (buf);
            if (n >= start)
            {
                eIn += x * x;
                eOut += (double) buf.getSample (0, 0) * buf.getSample (0, 0);
            }
        }
        (void) finalGainDb;
        return 10.0 * std::log10 (juce::jmax (1.0e-30, eOut) / juce::jmax (1.0e-30, eIn));
    }

    void runTest() override
    {
        // ---- DS201 ----
        beginTest ("DS201: above the threshold the gate is open at unity; below it the signal drops by the Range");
        {
            DS201StyleNoiseGateProcessor g;
            g.prepare (sr, 512, 1);
            setParams (g, { -50.0f, -60.0f, 1.0f, 20.0f, 20.0f, 25.0f, 35000.0f, 0.0f });
            expectWithinAbsoluteError (run (g, 1000.0, -20.0, 0.5), 0.0, 0.05);
            const double closed = run (g, 1000.0, -70.0, 1.0);
            logMessage ("open " + juce::String (0.0, 1) + " dB; a -70 dB signal with Range -60: " + juce::String (closed, 2) + " dB");
            expectWithinAbsoluteError (closed, -60.0, 1.5);
        }

        beginTest ("DS201: Range 0 dB never attenuates");
        {
            DS201StyleNoiseGateProcessor g;
            g.prepare (sr, 512, 1);
            setParams (g, { -50.0f, 0.0f, 1.0f, 20.0f, 20.0f, 25.0f, 35000.0f, 0.0f });
            expectWithinAbsoluteError (run (g, 1000.0, -70.0, 0.5), 0.0, 0.05);
        }

        beginTest ("DS201: Hold keeps the gate open for the hold time after the signal falls, then Decay closes it over the decay time");
        for (float holdMs : { 50.0f, 400.0f })
        {
            DS201StyleNoiseGateProcessor g;
            g.prepare (sr, 512, 1);
            setParams (g, { -50.0f, -60.0f, 1.0f, holdMs, 200.0f, 25.0f, 35000.0f, 0.0f });
            run (g, 1000.0, -20.0, 0.3);                       // open
            // then silence: count samples until the gain starts to fall and until it reaches the floor
            juce::AudioBuffer<float> buf (1, 1);
            int startFall = -1, atFloor = -1;
            for (int n = 0; n < (int) (3.0 * sr); ++n)
            {
                buf.setSample (0, 0, 0.0f);
                g.process (buf);
                if (startFall < 0 && g.debugGainDb() < -0.01)
                    startFall = n;
                if (atFloor < 0 && g.debugGainDb() <= -59.99)
                    atFloor = n;
            }
            const double holdSeen = 1000.0 * startFall / sr, decaySeen = 1000.0 * (atFloor - startFall) / sr;
            logMessage ("Hold " + juce::String (holdMs, 0) + " ms: gain starts falling after " + juce::String (holdSeen, 1) + " ms, reaches -60 dB after " + juce::String (decaySeen, 1) + " ms more");
            expectWithinAbsoluteError (holdSeen, (double) holdMs, 5.0 + 0.05 * holdMs);
            expectWithinAbsoluteError (decaySeen, 200.0, 12.0);
        }

        beginTest ("DS201: the side-chain filters: a 60 Hz hum above the threshold does not open the gate with the L.F. filter at 400 Hz");
        {
            DS201StyleNoiseGateProcessor g;
            g.prepare (sr, 512, 1);
            setParams (g, { -50.0f, -60.0f, 1.0f, 20.0f, 20.0f, 400.0f, 35000.0f, 0.0f });
            const double hum = run (g, 60.0, -30.0, 1.0);
            DS201StyleNoiseGateProcessor h;
            h.prepare (sr, 512, 1);
            setParams (h, { -50.0f, -60.0f, 1.0f, 20.0f, 20.0f, 25.0f, 35000.0f, 0.0f });
            const double open = run (h, 60.0, -30.0, 1.0);
            logMessage ("60 Hz at -30 dB: L.F. 400 Hz -> " + juce::String (hum, 1) + " dB (gated), L.F. 25 Hz -> " + juce::String (open, 1) + " dB");
            expectLessThan (hum, -40.0);
            expectWithinAbsoluteError (open, 0.0, 0.1);
        }

        beginTest ("DS201: Key Listen outputs the filtered side-chain");
        {
            DS201StyleNoiseGateProcessor g;
            g.prepare (sr, 512, 1);
            setParams (g, { -50.0f, -60.0f, 1.0f, 20.0f, 20.0f, 400.0f, 35000.0f, 1.0f });
            const double low = run (g, 60.0, -20.0, 0.5), mid = run (g, 2000.0, -20.0, 0.5);
            logMessage ("key listen: 60 Hz " + juce::String (low, 1) + " dB, 2 kHz " + juce::String (mid, 1) + " dB");
            expectLessThan (low, -20.0);
            expectWithinAbsoluteError (mid, 0.0, 0.5);
        }

        // ---- NS-2 ----
        beginTest ("NS-2: above the threshold it is transparent; below it, a 1:3 expander (10 dB under = 20 dB more attenuation), at most -60 dB");
        {
            NS2StyleNoiseSuppressorProcessor n;
            n.prepare (sr, 512, 1);
            setParams (n, { -60.0f, 0.2f, 0.0f });
            expectWithinAbsoluteError (run (n, 1000.0, -30.0, 0.5), 0.0, 0.1);
            const double atMinus70 = run (n, 1000.0, -70.0, 2.0);
            const double atMinus80 = run (n, 1000.0, -80.0, 2.0);
            logMessage ("threshold -60: -70 dB in -> " + juce::String (atMinus70, 1) + " dB gain; -80 dB in -> " + juce::String (atMinus80, 1) + " dB gain");
            expectWithinAbsoluteError (atMinus70, -20.0, 3.0);
            expectWithinAbsoluteError (atMinus80, -40.0, 4.0);
            expectLessThan (run (n, 1000.0, -100.0, 2.0), -55.0);
        }

        beginTest ("NS-2: Decay sets how slowly it closes; Mute silences the output");
        {
            auto closeMs = [] (float decay)
            {
                NS2StyleNoiseSuppressorProcessor n;
                n.prepare (sr, 512, 1);
                setParams (n, { -60.0f, decay, 0.0f });
                run (n, 1000.0, -30.0, 0.3);
                juce::AudioBuffer<float> buf (1, 1);
                for (int k = 0; k < (int) (4.0 * sr); ++k)
                {
                    buf.setSample (0, 0, 0.0f);
                    n.process (buf);
                    if (n.debugGainDb() <= -30.0)
                        return 1000.0 * k / sr;
                }
                return 4000.0;
            };
            const double fast = closeMs (0.0f), slow = closeMs (1.0f);
            logMessage ("time to -30 dB: Decay 0 -> " + juce::String (fast, 0) + " ms, Decay 1 -> " + juce::String (slow, 0) + " ms");
            expectGreaterThan (slow, fast * 10.0);

            NS2StyleNoiseSuppressorProcessor n;
            n.prepare (sr, 512, 1);
            setParams (n, { -60.0f, 0.2f, 1.0f });
            expectLessThan (run (n, 1000.0, -20.0, 0.3), -60.0);
        }
    }
};

static NoiseGateTests noiseGateTests;

} // namespace openguitarmultifx
