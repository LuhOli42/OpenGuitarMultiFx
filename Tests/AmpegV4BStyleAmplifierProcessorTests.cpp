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

        beginTest ("DC operating points land near the voltages marked on the V-4B drawing");
        {
            AmpegV4BStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 155.0, 0.25, "V1a plate" },
                { P::secondPlate, 230.0, 0.25, "V1b plate" },
                { P::recoveryPlate, 192.0, 0.25, "6K11 first plate" },
                { P::driverPlateA, 205.0, 0.25, "V3b plate" },
                { P::phaseInverterPlateA, 200.0, 0.25, "12AU7 LTP plate A" },
                { P::phaseInverterPlateB, 200.0, 0.25, "12AU7 LTP plate B" },
                { P::phaseInverterTail, 9.7, 0.35, "12AU7 LTP cathodes" },
                { P::powerGridA, -64.0, 0.1, "7027A grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, expected " + juce::String (pt.target));
            }
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1)
                        + ", LTP " + juce::String (amp.railDriver(), 1) + ", preamp " + juce::String (amp.railPreamp(), 1)
                        + "; 7027A Ip " + juce::String (amp.plateCurrentTotal() * 1000.0, 1) + " mA");
            expect (std::abs (amp.railPlates() - 545.0) < 0.1 * 545.0, "plate rail");
            expect (std::abs (amp.railScreens() - 540.0) < 0.1 * 540.0, "screen rail");
            expectGreaterThan (amp.plateCurrentTotal(), 0.04);
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

        beginTest ("the 6K11 Mid-Range pot boosts and cuts around the selected frequency");
        {
            // Small input: the stage's own level at the mid-section output S, 800 Hz selected.
            auto midAt = [&] (float middle, double hz)
            {
                AmpegV4BStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "ampegv4b_middle", middle);
                setParam (amp, "ampegv4b_volume", 0.2f);
                // Peak-to-peak: S sits on the follower's ~18 V DC.
                juce::AudioBuffer<float> buffer (1, 1);
                const int warm = 24000, meas = 4800;
                double lo = 1.0e9, hi = -1.0e9;
                for (int i = 0; i < warm + meas; ++i)
                {
                    buffer.setSample (0, 0, (float) (0.002 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0)));
                    amp.process (buffer);
                    if (i >= warm)
                    {
                        const double v = amp.debugVoltage (P::midNode);
                        lo = juce::jmin (lo, v);
                        hi = juce::jmax (hi, v);
                    }
                }
                return hi - lo;
            };
            const double boost = midAt (1.0f, 800.0), flat = midAt (0.5f, 800.0), cut = midAt (0.0f, 800.0);
            logMessage ("800 Hz at the mid output, Middle 1 / 0.5 / 0: " + juce::String (boost, 5) + " / "
                        + juce::String (flat, 5) + " / " + juce::String (cut, 5));
            expectGreaterThan (boost / juce::jmax (1.0e-9, flat), 2.0, "Middle up boosts 800 Hz");
            expectGreaterThan (flat / juce::jmax (1.0e-9, cut), 2.0, "Middle down cuts 800 Hz");
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
