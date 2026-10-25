#include "Effects/MesaBass400PlusStyleAmplifierProcessor.h"
#include "TestEnvironment.h"

namespace openguitarmultifx
{

/**
    The Bass 400+-style amplifier against the Bass 400+ schematic's expected operating points
    (docs/circuits/MesaBass400Plus.md says which numbers are verified and which are estimates).
*/
class MesaBass400PlusStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    MesaBass400PlusStyleAmplifierProcessorTests() : juce::UnitTest ("MesaBass400PlusStyleAmplifier", "Effects") {}

private:
    using P = MesaBass400PlusStyleAmplifierProcessor::Probe;

    static void setParam (MesaBass400PlusStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static double runSine (MesaBass400PlusStyleAmplifierProcessor& amp, double freq, double level, double warmup, double measure,
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
        MesaBass400PlusStyleAmplifierProcessor::reducedOrder = false; // the unit suite always probes the full-order reference

        beginTest ("DC operating points land near the voltages marked on the Bass 400 drawing");
        {
            MesaBass400PlusStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            expect (amp.dcConverged());
            struct { P p; double target; double tol; const char* what; } pts[] = {
                { P::firstPlate, 247.0, 0.25, "V1A plate" },
                { P::secondPlate, 224.0, 0.25, "V2A plate" },
                { P::recoveryPlate, 172.0, 0.25, "V2B cathode" },
                { P::phaseInverterPlateA, 350.0, 0.15, "V3A plate" },
                { P::phaseInverterPlateB, 350.0, 0.15, "V3B plate" },
                { P::driverPlateA, 276.0, 0.2, "V4A plate" },
                { P::powerGridA, -62.0, 0.1, "6L6 grid bias" },
            };
            for (auto& pt : pts)
            {
                const double v = amp.debugVoltage (pt.p);
                expect (std::abs (v - pt.target) < pt.tol * std::abs (pt.target),
                        juce::String (pt.what) + " " + juce::String (v) + " V, expected " + juce::String (pt.target));
            }
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1)
                        + ", B " + juce::String (amp.railBuffer(), 1) + ", C " + juce::String (amp.railPreamp(), 1)
                        + "; 6L6 Ip " + juce::String (amp.plateCurrentTotal() * 1000.0, 1) + " mA");
            expect (std::abs (amp.railPlates() - 540.0) < 0.1 * 540.0, "plate rail");
            expect (std::abs (amp.railScreens() - 526.0) < 0.1 * 526.0, "screen rail");
            expect (std::abs (amp.railPreamp() - 370.0) < 0.1 * 370.0, "C rail");
            expectGreaterThan (amp.plateCurrentTotal(), 0.1);
        }

        beginTest ("clean tone: sine in, sine out, no solve failures");
        {
            MesaBass400PlusStyleAmplifierProcessor amp;
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
                MesaBass400PlusStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "mesa400p_bass", bass);
                setParam (amp, "mesa400p_treble", treble);
                return runSine (amp, 40.0, 0.02, 1.0, 0.2, (int) P::toneStackOut);
            };
            const double up = levelAt (1.0f, 0.5f);
            const double dn = levelAt (0.0f, 0.5f);
            expectGreaterThan (up / juce::jmax (1.0e-9, dn), 1.2, "40 Hz tone out, bass up/down " + juce::String (up) + " / " + juce::String (dn));
        }

        beginTest ("bias knob moves the 6L6 idle current");
        {
            MesaBass400PlusStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            const double cold = amp.plateCurrentTotal();
            setParam (amp, "mesa400p_bias", 1.0f);
            juce::AudioBuffer<float> b (1, 128);
            for (int i = 0; i < 20; ++i) amp.process (b);
            const double hot = amp.plateCurrentTotal();
            expectGreaterThan (hot / juce::jmax (1.0e-9, cold), 1.2,
                               "idle plate current hot " + juce::String (hot) + " vs cold " + juce::String (cold));
        }

        beginTest ("graphic EQ slider boosts its band");
        {
            auto levelAt = [&] (float slider)
            {
                MesaBass400PlusStyleAmplifierProcessor amp;
                amp.prepare (48000.0, 128, 1);
                setParam (amp, "mesa400p_eq4", slider); // 320 Hz band
                setParam (amp, "mesa400p_master", 1.0f);
                return runSine (amp, 320.0, 0.02, 1.0, 0.2, (int) P::eqOut);
            };
            const double up = levelAt (1.0f);
            const double mid = levelAt (0.5f);
            const double dn = levelAt (0.0f);
            expectGreaterThan (up / juce::jmax (1.0e-9, mid), 1.2, "320 Hz EQ boost " + juce::String (up) + " vs flat " + juce::String (mid));
            expectGreaterThan (mid / juce::jmax (1.0e-9, dn), 1.2, "320 Hz EQ flat " + juce::String (mid) + " vs cut " + juce::String (dn));
        }

        beginTest ("hot input still terminates: bounded output, low failure rate");
        {
            MesaBass400PlusStyleAmplifierProcessor amp;
            amp.prepare (48000.0, 128, 1);
            setParam (amp, "mesa400p_volume", 1.0f);
            setParam (amp, "mesa400p_master", 1.0f);
            const double peak = runSine (amp, 60.0, 1.0, 1.5, 0.3);
            expect (std::isfinite (peak));
            expectLessThan (peak, 4.0);
            expectLessThan (amp.getSolveFailureRate(), 0.05);
        }
    }
};

} // namespace openguitarmultifx

static openguitarmultifx::MesaBass400PlusStyleAmplifierProcessorTests ampegB15StyleAmplifierProcessorTests;
