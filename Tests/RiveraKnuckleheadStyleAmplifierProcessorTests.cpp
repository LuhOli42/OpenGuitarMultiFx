#include "Effects/RiveraKnuckleheadStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Rivera Knucklehead-style amplifier (docs/circuits/RiveraKnucklehead.md). */
class RiveraKnuckleheadStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    RiveraKnuckleheadStyleAmplifierProcessorTests() : juce::UnitTest ("RiveraKnuckleheadStyleAmplifier", "Effects") {}

    using Amp = RiveraKnuckleheadStyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;
    static constexpr const char* px = "knk_";

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
            logMessage ("V1A " + juce::String (amp.debugVoltage (P::cleanPlate1), 1) + " / " + juce::String (amp.debugVoltage (P::cleanCath1), 2)
                        + ", V1B " + juce::String (amp.debugVoltage (P::leadPlate1), 1) + " / " + juce::String (amp.debugVoltage (P::leadCath1), 2)
                        + ", V2B " + juce::String (amp.debugVoltage (P::leadPlate3), 1)
                        + ", PI " + juce::String (amp.debugVoltage (P::piPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::piPlateB), 1)
                        + " k " + juce::String (amp.debugVoltage (P::piCathode), 1)
                        + ", bias " + juce::String (amp.debugVoltage (P::biasNode), 1)
                        + ", Ip " + juce::String (amp.plateCurrentA() * 1000.0, 1) + " / " + juce::String (amp.plateCurrentB() * 1000.0, 1) + " mA");
            // the drawing: ~460 V plates, ~445 V screens, TP4 318 V PI, bias TP41 -47 V
            expect (amp.railPlates() > 420.0 && amp.railPlates() < 500.0);
            expect (amp.railScreens() > 400.0 && amp.railScreens() < 480.0);
            expect (amp.debugVoltage (P::cleanPlate1) > 60.0 && amp.debugVoltage (P::cleanPlate1) < 240.0, "V1A plate");
            expect (amp.debugVoltage (P::piPlateA) > 150.0 && amp.debugVoltage (P::piPlateA) < 290.0, "PI plate");
            expect (amp.debugVoltage (P::biasNode) < -35.0 && amp.debugVoltage (P::biasNode) > -60.0, "fixed-bias node");
            expect (amp.plateCurrentA() > 0.030 && amp.plateCurrentA() < 0.120, "6L6 pair idle current");
            expect (std::abs (amp.plateCurrentA() - amp.plateCurrentB()) < 0.004, "matched pairs at idle");
        }

        beginTest ("silence stays silent: the global-NFB loop must not self-oscillate, on either channel");
        {
            for (float chSel : { 0.0f, 1.0f })
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
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
                logMessage ("channel " + juce::String (chSel, 0) + " silence AC RMS " + juce::String (acRms, 8));
                expectLessThan (acRms, 0.01);
                expectEquals (amp.getSolveFailureRate(), 0.0);
            }
        }

        beginTest ("a hot guitar-level signal stays finite, bounded and solved on both channels");
        {
            for (float chSel : { 0.0f, 1.0f })
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "volume", 1.0f);
                setParam (amp, juce::String (px) + "gain", 1.0f);
                const double rms = sineRms (amp, 196.0, 0.5);
                logMessage ("channel " + juce::String (chSel, 0) + " full gain, 0.5 V 196 Hz: out RMS " + juce::String (rms, 4)
                            + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                            + ", recoveries " + juce::String (amp.debugRecoveries()));
                expect (std::isfinite (rms) && rms > 0.05 && rms < 1.5);
                expectLessThan (amp.getSolveFailureRate(), 0.001);
            }
        }

        beginTest ("parameters exist and reach the circuit");
        {
            Amp probe;
            for (auto* id : { "channel", "volume", "gain", "treble", "middle", "bass", "master", "presence", "focus",
                              "power", "bias", "tube_feel", "speaker", "output" })
                expect (hasParam (probe, juce::String (px) + id), juce::String (id));

            auto level = [] (float chSel, const char* id, float v, double hz, double amplitude)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "volume", 0.25f);
                setParam (amp, juce::String (px) + "gain", 0.25f);
                setParam (amp, juce::String (px) + id, v);
                return sineRms (amp, hz, amplitude);
            };
            const double clean = level (0.0f, "volume", 0.25f, 440.0, 0.002);
            const double lead = level (1.0f, "gain", 0.25f, 440.0, 0.002);
            logMessage ("clean vs lead channel out at matched settings: " + juce::String (clean, 5) + " / " + juce::String (lead, 5));
            expectGreaterThan (lead, clean * 1.3);

            const double volLo = level (0.0f, "volume", 0.1f, 440.0, 0.001), volHi = level (0.0f, "volume", 0.5f, 440.0, 0.001);
            logMessage ("volume 0.1 / 0.5: " + juce::String (volLo, 5) + " / " + juce::String (volHi, 5));
            expectGreaterThan (volHi, volLo * 1.5);

            const double trLo = level (0.0f, "treble", 0.0f, 3000.0, 0.002), trHi = level (0.0f, "treble", 1.0f, 3000.0, 0.002);
            logMessage ("treble 0 / 1 at 3 kHz: " + juce::String (trLo, 5) + " / " + juce::String (trHi, 5));
            expectGreaterThan (trHi, trLo * 1.3);

            const double pLo = level (0.0f, "presence", 0.0f, 6000.0, 0.002), pHi = level (0.0f, "presence", 1.0f, 6000.0, 0.002);
            logMessage ("presence 0 / 1 at 6 kHz: " + juce::String (pLo, 5) + " / " + juce::String (pHi, 5));
            expectGreaterThan (pHi, pLo * 1.15);

            const double fLo = level (0.0f, "focus", 0.0f, 100.0, 0.002), fHi = level (0.0f, "focus", 1.0f, 100.0, 0.002);
            logMessage ("focus 0 / 1 at 100 Hz: " + juce::String (fLo, 5) + " / " + juce::String (fHi, 5));
            expectGreaterThan (fHi, fLo * 1.1);

            Amp hot, cold;
            setParam (hot, juce::String (px) + "bias", 1.0f);
            setParam (cold, juce::String (px) + "bias", 0.0f);
            hot.prepare (sr, 128, 1);
            cold.prepare (sr, 128, 1);
            expectGreaterThan (hot.plateCurrentA(), cold.plateCurrentA() * 1.3);
        }
    }
};

static RiveraKnuckleheadStyleAmplifierProcessorTests riveraKnuckleheadStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
