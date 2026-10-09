#include "Effects/SVTStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The SVT-style amplifier against the SVT-CL service schematic's own marked voltages and the published
    specifications (300 W into 4 ohm, tone specs). docs/circuits/AmpegSVT.md says which numbers are verified
    and which are documented estimates (OT primary, feedback network, Ultra switch wiring).
*/
class SVTStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    SVTStyleAmplifierProcessorTests() : juce::UnitTest ("SVTStyleAmplifier", "Effects") {}

private:
    using P = SVTStyleAmplifierProcessor::Probe;

    static void setParam (SVTStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (SVTStyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
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
        beginTest ("DC operating points land near the schematic's marked voltages (+-30%)");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 235.0, 0.3, "V1:B plate" },
                { P::secondPlate, 240.0, 0.3, "V1:A plate" },
                { P::recoveryPlate, 240.0, 0.3, "V2:B plate" },
                // The PI is a documented approximation of the real phase splitter (AmpegSVT.md): the LTP's
                // tail sits on the -180 V string so several DC equilibria are nearly valid -- wider margin.
                { P::phaseInverterPlateA, 200.0, 0.5, "PI plate A" },
                { P::phaseInverterPlateB, 250.0, 0.5, "PI plate B" },
                { P::driverPlateA, 220.0, 0.4, "driver plate" },
                { P::powerGridA, -45.0, 0.35, "6550 grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, schematic " + juce::String (pt.target));
            }
            expect (std::abs (amp.railScreens() - 365.0) < 0.2 * 365.0, "screen rail " + juce::String (amp.railScreens()));
            expect (std::abs (amp.railPreamp() - 345.0) < 0.2 * 345.0, "preamp rail " + juce::String (amp.railPreamp()));
            expectGreaterThan (amp.plateCurrentTotal(), 0.15); // six 6550s idle ~0.3 A; half that at minimum
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double peak = runSine (amp, 100.0, 0.05, 1.5, 0.2);
            logMessage ("pre fails " + juce::String ((long long) amp.debugPreFailures())
                        + ", power fails " + juce::String ((long long) amp.debugPowerFailures())
                        + ", sanity rejects " + juce::String ((long long) amp.debugSanityRejects())
                        + ", worst rejected V " + juce::String (amp.debugWorstRejectedVolts(), 1)
                        + ", recoveries " + juce::String ((long long) amp.debugRecoveries()));
            expect (peak > 0.005, "output peak " + juce::String (peak));
            expectLessThan (amp.getSolveFailureRate(), 0.01,
                            "fails pre " + juce::String ((long long) amp.debugPreFailures())
                            + " power " + juce::String ((long long) amp.debugPowerFailures())
                            + " sanity " + juce::String ((long long) amp.debugSanityRejects())
                            + " worstRej " + juce::String (amp.debugWorstRejectedVolts(), 1));
            // A handful of rest-state recoveries during the cold-start transient is acceptable on a
            // fifteen-tube model -- what matters is that the steady state solves cleanly (<1% above).
            expectLessThan (amp.debugRecoveries(), 10);
        }

        beginTest ("mid frequency select moves the resonant peak");
        {
            // Middle at zero shorts the trap's rheostat: the resonant peak should land on the selected
            // tap's frequency. Measure the cathode follower's output (the trap's effect at the mid node
            // propagates there, and unlike midNode it has ~0 V DC so a peak reading sees the AC).
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "svt_middle", 0.0f);
            setParam (amp, "svt_master", 1.0f);
            const double r800 = runSine (amp, 750.0, 0.02, 1.0, 0.2, (int) P::followerOut);
            setParam (amp, "svt_mid_freq", 0.0f);
            const double r220 = runSine (amp, 750.0, 0.02, 0.5, 0.2, (int) P::followerOut);
            // A 750 Hz tone should be stronger with the ~800 Hz tap selected than the ~220 Hz one.
            expectGreaterThan (r800 / juce::jmax (1.0e-9, r220), 1.2,
                               "750 Hz follower out: 800Hz-tap " + juce::String (r800) + " vs 220Hz-tap " + juce::String (r220));
        }

        beginTest ("bass knob swings 40 Hz at the tone stack");
        {
            auto levelAt = [&] (float bass, float treble)
            {
                SVTStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "svt_bass", bass);
                setParam (amp, "svt_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("bias knob moves the 6550 idle current");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double cold = amp.plateCurrentTotal();
            setParam (amp, "svt_bias", 1.0f);
            juce::AudioBuffer<float> b (1, 128); // let the new bias settle
            for (int i = 0; i < 20; ++i) amp.process (b);
            const double hot = amp.plateCurrentTotal();
            expectGreaterThan (hot / juce::jmax (1.0e-9, cold), 1.2,
                               "idle plate current hot " + juce::String (hot) + " vs cold " + juce::String (cold));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            SVTStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "svt_gain", 1.0f);
            setParam (amp, "svt_master", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0); // outputScale keeps even a pegged speaker under a few volts of signal
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::SVTStyleAmplifierProcessorTests svtStyleAmplifierProcessorTests;
