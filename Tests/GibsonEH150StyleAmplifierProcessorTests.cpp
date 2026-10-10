#include "Effects/GibsonEH150StyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Gibson EH-150-style amplifier (docs/circuits/GibsonEH150.md). */
class GibsonEH150StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    GibsonEH150StyleAmplifierProcessorTests() : juce::UnitTest ("GibsonEH150StyleAmplifier", "Effects") {}

    using Amp = GibsonEH150StyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;
    static constexpr const char* px = "eh150_";

    static void setParam (Amp& amp, const juce::String& id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    static bool hasParam (Amp& amp, const juce::String& id)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    return true;
        return false;
    }

    /** RMS of the output over the last second of a 1.5 s sine burst (mono). */
    static double sineRms (Amp& amp, double hz, double amplitude)
    {
        amp.prepare (sr, 256, 1);
        juce::AudioBuffer<float> buf (1, 256);
        const int blocks = (int) (1.5 * sr) / 256, skip = (int) (0.5 * sr) / 256;
        double sumSq = 0.0;
        long long n = 0, t = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i, ++t)
                buf.setSample (0, i, (float) (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) t / sr)));
            amp.process (buf);
            if (b >= skip)
                for (int i = 0; i < 256; ++i, ++n)
                    sumSq += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        return std::sqrt (sumSq / (double) juce::jmax (1LL, n));
    }

    void runTest() override
    {
        beginTest ("the model converges to a sane DC operating point");
        {
            Amp amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1));
            logMessage ("V1 " + juce::String (amp.debugVoltage (P::stage1Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage1Cathode), 2)
                        + ", V2 " + juce::String (amp.debugVoltage (P::stage2Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage2Cathode), 2)
                        + ", 6N7 " + juce::String (amp.debugVoltage (P::piPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::piPlateB), 1)
                        + " k " + juce::String (amp.debugVoltage (P::piCathode), 1)
                        + ", 6L6 plates " + juce::String (amp.debugVoltage (P::powerPlateA), 1)
                        + ", cathode " + juce::String (amp.debugVoltage (P::powerCathode), 1)
                        + ", Ip " + juce::String (amp.plateCurrentA() * 1000.0, 1) + " / " + juce::String (amp.plateCurrentB() * 1000.0, 1) + " mA");
            expect (amp.railPlates() > 330.0 && amp.railPlates() < 400.0);
            expect (amp.railScreens() > 290.0 && amp.railScreens() < 360.0);
            expect (amp.debugVoltage (P::stage1Plate) > 100.0 && amp.debugVoltage (P::stage1Plate) < 230.0, "V1 plate");
            expect (amp.debugVoltage (P::stage1Cathode) > 0.4 && amp.debugVoltage (P::stage1Cathode) < 2.5, "V1 cathode");
            expect (amp.debugVoltage (P::piPlateA) > 100.0 && amp.debugVoltage (P::piPlateA) < 260.0, "6N7 plate A");
            expect (amp.debugVoltage (P::piPlateB) > 100.0 && amp.debugVoltage (P::piPlateB) < 260.0, "6N7 plate B");
            expect (amp.debugVoltage (P::powerCathode) > 12.0 && amp.debugVoltage (P::powerCathode) < 45.0, "6L6 shared cathode");
            expect (amp.plateCurrentA() > 0.010 && amp.plateCurrentA() < 0.060, "6L6 idle current");
            expect (std::abs (amp.plateCurrentA() - amp.plateCurrentB()) < 0.002, "matched pair at idle");
        }

        beginTest ("silence stays silent: no self-oscillation");
        {
            Amp amp;
            amp.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            double sum = 0.0, sumSq = 0.0;
            long long n = 0;
            const int warm = (int) (1.5 * sr) / 512, meas = (int) (1.5 * sr) / 512;
            for (int b = 0; b < warm + meas; ++b)
            {
                buf.clear();
                amp.process (buf);
                if (b >= warm)
                    for (int i = 0; i < 512; ++i, ++n)
                    {
                        sum += buf.getSample (0, i);
                        sumSq += (double) buf.getSample (0, i) * buf.getSample (0, i);
                    }
            }
            const double mean = sum / (double) n;
            const double acRms = std::sqrt (juce::jmax (0.0, sumSq / (double) n - mean * mean));
            logMessage ("silence AC RMS " + juce::String (acRms, 8));
            expectLessThan (acRms, 0.01);
            expectEquals (amp.getSolveFailureRate(), 0.0);
        }

        beginTest ("a hot guitar-level signal stays finite, bounded and solved");
        {
            Amp amp;
            setParam (amp, juce::String (px) + "volume", 1.0f);
            const double rms = sineRms (amp, 196.0, 0.5);
            logMessage ("full volume, 0.5 V 196 Hz: out RMS " + juce::String (rms, 4) + ", failure rate "
                        + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (std::isfinite (rms) && rms > 0.05 && rms < 1.5);
            expectLessThan (amp.getSolveFailureRate(), 0.001);
        }

        beginTest ("parameters exist and reach the circuit");
        {
            Amp probe;
            for (auto* id : { "volume", "tone", "power", "bias", "tube_feel", "speaker", "output" })
                expect (hasParam (probe, juce::String (px) + id), juce::String (id));

            auto level = [] (const char* id, float v, double hz, double amplitude)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "volume", 0.25f);
                setParam (amp, juce::String (px) + id, v);
                return sineRms (amp, hz, amplitude);
            };
            const double volLo = level ("volume", 0.1f, 440.0, 0.001), volHi = level ("volume", 0.5f, 440.0, 0.001);
            logMessage ("volume 0.1 / 0.5: " + juce::String (volLo, 5) + " / " + juce::String (volHi, 5));
            expectGreaterThan (volHi, volLo * 1.5);

            const double tLo = level ("tone", 0.0f, 3000.0, 0.002), tHi = level ("tone", 1.0f, 3000.0, 0.002);
            logMessage ("tone 0 / 1 at 3 kHz: " + juce::String (tLo, 5) + " / " + juce::String (tHi, 5));
            expectGreaterThan (tHi, tLo * 1.3);

            Amp speakerAmp;
            setParam (speakerAmp, juce::String (px) + "volume", 0.25f);
            const double at8Ohm = sineRms (speakerAmp, 85.0, 0.002);
            setParam (speakerAmp, juce::String (px) + "speaker", 2.0f);
            const double at16Ohm = sineRms (speakerAmp, 85.0, 0.002);
            logMessage ("speaker 8 / 16 ohm at 85 Hz: " + juce::String (at8Ohm, 5) + " / " + juce::String (at16Ohm, 5));
            expect (std::abs (at16Ohm - at8Ohm) > juce::jmax (at8Ohm, at16Ohm) * 0.01,
                    "changing the speaker selector changes the output");

            Amp hot, cold;
            setParam (hot, juce::String (px) + "bias", 1.0f);
            setParam (cold, juce::String (px) + "bias", 0.0f);
            hot.prepare (sr, 128, 1);
            cold.prepare (sr, 128, 1);
            expectGreaterThan (hot.plateCurrentA(), cold.plateCurrentA() * 1.3);
        }
    }
};

static GibsonEH150StyleAmplifierProcessorTests gibsonEH150StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
