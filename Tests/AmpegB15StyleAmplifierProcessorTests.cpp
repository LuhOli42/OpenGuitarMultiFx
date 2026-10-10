#include "Effects/AmpegB15StyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The B-15-style amplifier against the B-15N schematic's expected operating points
    (docs/circuits/AmpegB15.md says which numbers are verified and which are estimates).
*/
class AmpegB15StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    AmpegB15StyleAmplifierProcessorTests() : juce::UnitTest ("AmpegB15StyleAmplifier", "Effects") {}

private:
    using P = AmpegB15StyleAmplifierProcessor::Probe;

    static void setParam (AmpegB15StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (AmpegB15StyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
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
        AmpegB15StyleAmplifierProcessor::reducedOrder = false; // the unit suite always probes the full-order reference

        beginTest ("DC operating points land near the schematic's expected voltages (+-40%)");
        {
            AmpegB15StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 180.0, 0.4, "V1a plate" },
                { P::secondPlate, 200.0, 0.4, "V1b plate" },
                { P::driverPlate, 200.0, 0.5, "paraphase driver plate" },
                { P::inverterPlate, 200.0, 0.5, "paraphase inverter plate" },
                { P::powerGridA, -50.0, 0.35, "6L6 grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, expected " + juce::String (pt.target));
            }
            expect (std::abs (amp.railPreamp() - 330.0) < 0.25 * 330.0, "preamp rail " + juce::String (amp.railPreamp()));
            expectGreaterThan (amp.plateCurrentTotal(), 0.03); // two 6L6GCs idle ~70 mA
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            AmpegB15StyleAmplifierProcessor amp;
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
                AmpegB15StyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "ampegb15_bass", bass);
                setParam (amp, "ampegb15_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("bias knob moves the 6L6 idle current");
        {
            AmpegB15StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double cold = amp.plateCurrentTotal();
            setParam (amp, "ampegb15_bias", 1.0f);
            juce::AudioBuffer<float> b (1, 128);
            for (int i = 0; i < 20; ++i) amp.process (b);
            const double hot = amp.plateCurrentTotal();
            expectGreaterThan (hot / juce::jmax (1.0e-9, cold), 1.2,
                               "idle plate current hot " + juce::String (hot) + " vs cold " + juce::String (cold));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            AmpegB15StyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "ampegb15_volume", 1.0f);
            setParam (amp, "ampegb15_master", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0);
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::AmpegB15StyleAmplifierProcessorTests ampegB15StyleAmplifierProcessorTests;
