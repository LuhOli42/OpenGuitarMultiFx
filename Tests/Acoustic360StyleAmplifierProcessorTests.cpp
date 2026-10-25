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
                // factory 360 (1006A): Q1 is an emitter follower biased by R3 1M / R4 4.7M off +25 V (~20 V at the
                // emitter); Q4 (R16 3.9K, R14 270K collector-base, R15 680) sits near 7.7 V.
                { P::firstCollector, 20.0, 0.2, "Q1 emitter" },
                { P::secondCollector, 7.7, 0.4, "Q4 collector" },
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
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("variamp: the RANGE selects the band, the depth pot cuts or boosts it");
        {
            // R24 at the collector end: the 1.5 H trap shunts Q5's output at resonance (cut); at the emitter end it
            // bypasses the emitter resistor at resonance (boost). RANGE 0 = 87 Hz, 2 = 411 Hz.
            auto levelAt = [&] (float pos, float effect, double freq)
            {
                Acoustic360StyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "acoustic360_effect", effect);
                setParam (amp, "acoustic360_variamp", pos);
                return runSine (amp, freq, 0.005, 1.0, 0.2, (int) P::preampOut);
            };
            const double cutAt411 = levelAt (2.0f, 0.0f, 411.0), cutAway = levelAt (0.0f, 0.0f, 411.0);
            expectGreaterThan (cutAway / juce::jmax (1.0e-9, cutAt411), 1.4,
                               "411 Hz, full cut: 87 Hz range " + juce::String (cutAway) + " vs 411 Hz range " + juce::String (cutAt411));
            const double boost = levelAt (2.0f, 1.0f, 411.0), flat = levelAt (2.0f, 0.5f, 411.0);
            expectGreaterThan (boost / juce::jmax (1.0e-9, flat), 1.4,
                               "411 Hz, 411 Hz range: boost " + juce::String (boost) + " vs centre " + juce::String (flat));
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
