#include "Effects/Acoustic360StyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The 360-style amplifier against the Acoustic 360 schematic's expected operating points
    (docs/circuits/Acoustic360.md says which numbers are verified and which are estimates).
*/
class Acoustic360StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    Acoustic360StyleAmplifierProcessorTests() : juce::UnitTest ("Acoustic360StyleAmplifier", "Effects") {}

private:
    using P = Acoustic360StyleAmplifierProcessor::Probe;

    static void setParam (Acoustic360StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (Acoustic360StyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
                           int probe = -1)
    {
        juce::AudioBuffer<float> buffer (1, 128);
        const double sr = 48000.0;
        const int warm = (int) (warmup * sr), meas = (int) (measure * sr);
        double peak = 0.0;
        for (int i = 0; i < warm + meas; ++i)
        {
            buffer.setSample (0, i % 128, (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr)));
            if (i % 128 == 127)
            {
                amp.process (buffer);
                if (i >= warm)
                    peak = juce::jmax (peak, (double) std::abs (probe >= 0 ? amp.debugVoltage ((P) probe) : buffer.getSample (0, 127)));
            }
        }
        return peak;
    }

    void runTest() override
    {
        Acoustic360StyleAmplifierProcessor::reducedOrder = false; // the unit suite always probes the full-order reference

        beginTest ("DC operating points land near the schematic's expected voltages (+-40%)");
        {
            Acoustic360StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstCollector, 13.0, 0.4, "Q1 collector" },
                { P::secondCollector, 13.0, 0.4, "Q2 collector" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, expected " + juce::String (pt.target));
            }
            expect (std::abs (amp.debugVoltage (P::speaker)) < 2.0, "speaker DC " + juce::String (amp.debugVoltage (P::speaker)));
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            Acoustic360StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double peak = runSine (amp, 100.0, 0.05, 1.5, 0.2);
            logMessage ("pre fails " + juce::String ((long long) amp.debugPreFailures())
                        + ", power fails " + juce::String ((long long) amp.debugPowerFailures())
                        + ", recoveries " + juce::String ((long long) amp.debugRecoveries()));
            expect (peak > 0.005, "output peak " + juce::String (peak));
            expectLessThan (amp.getSolveFailureRate(), 0.01);
            expectLessThan (amp.debugRecoveries(), 10);
        }

        beginTest ("bass knob swings 40 Hz at the tone stack");
        {
            auto levelAt = [&] (float bass, float treble)
            {
                Acoustic360StyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "acoustic360_bass", bass);
                setParam (amp, "acoustic360_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::variampNode);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("variamp select moves the resonant dip");
        {
            // Effect at max closes the trap's rheostat: the selected band dips at the preamp output.
            auto levelAt = [&] (float pos, double freq)
            {
                Acoustic360StyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "acoustic360_effect", 1.0f);
                setParam (amp, "acoustic360_variamp", pos);
                return runSine (amp, freq, 0.02, 1.0, 0.2, (int) P::variampNode);
            };
            const double lo390 = levelAt (0.0f, 390.0);   // position 1 trap ~390 Hz
            const double hi390 = levelAt (3.0f, 390.0);   // position 4 trap ~2200 Hz: 390 passes
            expectGreaterThan (hi390 / juce::jmax (1.0e-9, lo390), 1.2,
                               "390 Hz at variamp node: 2.2k pos " + juce::String (hi390) + " vs 390 pos " + juce::String (lo390));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            Acoustic360StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "acoustic360_volume", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0);
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::Acoustic360StyleAmplifierProcessorTests ampegB15StyleAmplifierProcessorTests;
