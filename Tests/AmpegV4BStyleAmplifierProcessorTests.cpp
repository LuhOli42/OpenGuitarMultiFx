#include "Effects/AmpegV4BStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The V4B-style amplifier against the V4B schematic's expected operating points
    (docs/circuits/AmpegV4B.md says which numbers are verified and which are estimates).
*/
class AmpegV4BStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    AmpegV4BStyleAmplifierProcessorTests() : juce::UnitTest ("AmpegV4BStyleAmplifier", "Effects") {}

private:
    using P = AmpegV4BStyleAmplifierProcessor::Probe;

    static void setParam (AmpegV4BStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (AmpegV4BStyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
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
        AmpegV4BStyleAmplifierProcessor::reducedOrder = false; // the unit suite always probes the full-order reference

        beginTest ("DC operating points land near the schematic's expected voltages (+-40%)");
        {
            AmpegV4BStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 235.0, 0.4, "first plate" },
                { P::secondPlate, 240.0, 0.4, "second plate" },
                { P::recoveryPlate, 240.0, 0.4, "recovery plate" },
                { P::phaseInverterPlateA, 200.0, 0.5, "PI plate A" },
                { P::phaseInverterPlateB, 250.0, 0.5, "PI plate B" },
                { P::driverPlateA, 220.0, 0.5, "driver plate" },
                { P::powerGridA, -50.0, 0.35, "7027A grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, expected " + juce::String (pt.target));
            }
            expect (std::abs (amp.railScreens() - 340.0) < 0.25 * 340.0, "screen rail " + juce::String (amp.railScreens()));
            expect (std::abs (amp.railPreamp() - 300.0) < 0.25 * 300.0, "preamp rail " + juce::String (amp.railPreamp()));
            expectGreaterThan (amp.plateCurrentTotal(), 0.002); // the kg1 softening that stabilises the
            // NFB loop (see the processor) drops the reference idle far below the real ~160 mA -- the
            // documented trade-off shared by the JCM800/SLO-100/Mark IIC+ family of models
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            AmpegV4BStyleAmplifierProcessor amp;
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
                AmpegV4BStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "ampegv4b_bass", bass);
                setParam (amp, "ampegv4b_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("bias knob moves the 6L6 idle current");
        {
            AmpegV4BStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double cold = amp.plateCurrentTotal();
            setParam (amp, "ampegv4b_bias", 1.0f);
            juce::AudioBuffer<float> b (1, 128);
            for (int i = 0; i < 20; ++i) amp.process (b);
            const double hot = amp.plateCurrentTotal();
            expectGreaterThan (hot / juce::jmax (1.0e-9, cold), 1.2,
                               "idle plate current hot " + juce::String (hot) + " vs cold " + juce::String (cold));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            AmpegV4BStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "ampegv4b_volume", 1.0f);
            setParam (amp, "ampegv4b_master", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0);
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::AmpegV4BStyleAmplifierProcessorTests ampegB15StyleAmplifierProcessorTests;
