#include "Effects/GarnetHerzogStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Garnet Herzog-style tube overdrive unit (docs/circuits/GarnetHerzog.md). */
class GarnetHerzogStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    GarnetHerzogStyleAmplifierProcessorTests() : juce::UnitTest ("GarnetHerzogStyleAmplifier", "Effects") {}

    using Amp = GarnetHerzogStyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;
    static constexpr const char* px = "hzg_";

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
            logMessage ("V1A " + juce::String (amp.debugVoltage (P::stage1Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage1Cathode), 2)
                        + ", V1B " + juce::String (amp.debugVoltage (P::stage2Plate), 1) + " / " + juce::String (amp.debugVoltage (P::stage2Cathode), 2)
                        + ", 6V6 plate " + juce::String (amp.debugVoltage (P::powerPlate), 1)
                        + ", cathode " + juce::String (amp.debugVoltage (P::powerCathode), 1)
                        + ", Ip " + juce::String (amp.plateCurrent() * 1000.0, 1) + " mA");
            // the Garnet drawing: 320+ at the OT, 315+ screens, 295+ preamp
            expect (amp.railPlates() > 295.0 && amp.railPlates() < 350.0);
            expect (amp.railScreens() > 285.0 && amp.railScreens() < 335.0);
            expect (amp.debugVoltage (P::stage1Plate) > 120.0 && amp.debugVoltage (P::stage1Plate) < 280.0, "V1A plate");
            expect (amp.debugVoltage (P::stage1Cathode) > 0.5 && amp.debugVoltage (P::stage1Cathode) < 2.5, "V1A cathode");
            expect (amp.debugVoltage (P::powerCathode) > 8.0 && amp.debugVoltage (P::powerCathode) < 30.0, "6V6 cathode");
            expect (amp.plateCurrent() > 0.015 && amp.plateCurrent() < 0.055, "6V6 idle current");
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
            setParam (amp, juce::String (px) + "level", 1.0f);
            const double rms = sineRms (amp, 196.0, 0.5);
            logMessage ("full volume+level, 0.5 V 196 Hz: out RMS " + juce::String (rms, 4) + ", failure rate "
                        + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (std::isfinite (rms) && rms > 0.05 && rms < 1.5);
            expectLessThan (amp.getSolveFailureRate(), 0.001);
        }

        beginTest ("parameters exist and reach the circuit");
        {
            Amp probe;
            for (auto* id : { "volume", "deep", "level", "power", "tube_feel", "output" })
                expect (hasParam (probe, juce::String (px) + id), juce::String (id));

            auto level = [] (const char* id, float v, double hz, double amplitude)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "volume", 0.25f);
                setParam (amp, juce::String (px) + "level", 0.7f);
                setParam (amp, juce::String (px) + id, v);
                return sineRms (amp, hz, amplitude);
            };
            const double volLo = level ("volume", 0.1f, 440.0, 0.001), volHi = level ("volume", 0.5f, 440.0, 0.001);
            logMessage ("volume 0.1 / 0.5: " + juce::String (volLo, 5) + " / " + juce::String (volHi, 5));
            expectGreaterThan (volHi, volLo * 1.5);

            const double lvLo = level ("level", 0.1f, 440.0, 0.001), lvHi = level ("level", 0.9f, 440.0, 0.001);
            logMessage ("level 0.1 / 0.9: " + juce::String (lvLo, 5) + " / " + juce::String (lvHi, 5));
            expectGreaterThan (lvHi, lvLo * 1.5);

            const double dOff = level ("deep", 0.0f, 80.0, 0.001), dOn = level ("deep", 1.0f, 80.0, 0.001);
            logMessage ("deep off / on at 80 Hz: " + juce::String (dOff, 5) + " / " + juce::String (dOn, 5));
            expectGreaterThan (dOn, dOff * 1.15);
        }
    }
};

static GarnetHerzogStyleAmplifierProcessorTests garnetHerzogStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
