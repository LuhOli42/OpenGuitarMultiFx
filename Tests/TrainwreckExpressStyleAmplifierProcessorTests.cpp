#include "Effects/TrainwreckExpressStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Trainwreck Express-style amplifier (docs/circuits/TrainwreckExpress.md). */
class TrainwreckExpressStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    TrainwreckExpressStyleAmplifierProcessorTests() : juce::UnitTest ("TrainwreckExpressStyleAmplifier", "Effects") {}

    using Amp = TrainwreckExpressStyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;
    static constexpr const char* px = "twx_";

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
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1)
                        + ", PI " + juce::String (amp.railPhaseInverter(), 1) + ", preamp " + juce::String (amp.railPreamp(), 1));
            logMessage ("V1B " + juce::String (amp.debugVoltage (P::stage1Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage1Cathode), 2)
                        + ", V1A " + juce::String (amp.debugVoltage (P::stage2Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage2Cathode), 2)
                        + ", V2A " + juce::String (amp.debugVoltage (P::stage3Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage3Cathode), 2)
                        + ", PI " + juce::String (amp.debugVoltage (P::piPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::piPlateB), 1)
                        + " k " + juce::String (amp.debugVoltage (P::piCathode), 1)
                        + ", EL34 plates " + juce::String (amp.debugVoltage (P::powerPlateA), 1)
                        + ", grid " + juce::String (amp.debugVoltage (P::powerGridA), 1)
                        + ", Ip " + juce::String (amp.plateCurrentA() * 1000.0, 1) + " / " + juce::String (amp.plateCurrentB() * 1000.0, 1) + " mA");
            // the Express drawings: 395 V plates, 380 V screens, ~295 V PI, 267-282 V preamp; builders read ~175-203 / ~257 / ~223-232 V plates
            expect (amp.railPlates() > 370.0 && amp.railPlates() < 420.0);
            expect (amp.railScreens() > 350.0 && amp.railScreens() < 400.0);
            expect (amp.railPhaseInverter() > 260.0 && amp.railPhaseInverter() < 320.0);
            expect (amp.debugVoltage (P::stage1Plate) > 140.0 && amp.debugVoltage (P::stage1Plate) < 230.0, "V1B plate");
            expect (amp.debugVoltage (P::stage1Cathode) > 0.8 && amp.debugVoltage (P::stage1Cathode) < 2.2, "V1B cathode");
            expect (amp.debugVoltage (P::stage2Cathode) > 1.0 && amp.debugVoltage (P::stage2Cathode) < 3.0, "V1A cathode");
            expect (amp.debugVoltage (P::stage3Plate) > 220.0 && amp.debugVoltage (P::stage3Plate) < 285.0, "V2A plate");
            expect (amp.debugVoltage (P::stage3Cathode) > 1.5 && amp.debugVoltage (P::stage3Cathode) < 4.5, "V2A cathode");
            expect (amp.debugVoltage (P::piPlateA) > 150.0 && amp.debugVoltage (P::piPlateA) < 260.0, "PI plate");
            expect (amp.debugVoltage (P::powerGridA) < -30.0 && amp.debugVoltage (P::powerGridA) > -50.0, "fixed-bias EL34 grid");
            expect (amp.plateCurrentA() > 0.015 && amp.plateCurrentA() < 0.065, "EL34 idle current");
            expect (std::abs (amp.plateCurrentA() - amp.plateCurrentB()) < 0.002, "matched pair at idle");
        }

        beginTest ("silence stays silent: no self-oscillation (no global NFB, but the power stage must still be stable)");
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
            for (auto* id : { "volume", "treble", "middle", "bass", "presence", "power", "bias", "tube_feel", "speaker", "output" })
                expect (hasParam (probe, juce::String (px) + id), juce::String (id));

            // small signals at a low Volume so the cascaded stages stay out of clipping and the controls' linear effect shows
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

            const double trLo = level ("treble", 0.0f, 3000.0, 0.002), trHi = level ("treble", 1.0f, 3000.0, 0.002);
            logMessage ("treble 0 / 1 at 3 kHz: " + juce::String (trLo, 5) + " / " + juce::String (trHi, 5));
            expectGreaterThan (trHi, trLo * 1.3);

            const double bLo = level ("bass", 0.0f, 100.0, 0.002), bHi = level ("bass", 1.0f, 100.0, 0.002);
            logMessage ("bass 0 / 1 at 100 Hz: " + juce::String (bLo, 5) + " / " + juce::String (bHi, 5));
            expectGreaterThan (bHi, bLo * 1.3);

            const double pLo = level ("presence", 0.0f, 6000.0, 0.002), pHi = level ("presence", 1.0f, 6000.0, 0.002);
            logMessage ("presence 0 / 1 at 6 kHz: " + juce::String (pLo, 5) + " / " + juce::String (pHi, 5));
            expectGreaterThan (pHi, pLo * 1.2);

            Amp hot, cold;
            setParam (hot, juce::String (px) + "bias", 1.0f);
            setParam (cold, juce::String (px) + "bias", 0.0f);
            hot.prepare (sr, 128, 1);
            cold.prepare (sr, 128, 1);
            expectGreaterThan (hot.plateCurrentA(), cold.plateCurrentA() * 1.3);
        }
    }
};

static TrainwreckExpressStyleAmplifierProcessorTests trainwreckExpressStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
