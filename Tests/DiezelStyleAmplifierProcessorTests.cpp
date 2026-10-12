#include "../Source/Effects/DiezelStyleAmplifierProcessor.h"

namespace openguitarmultifx
{
namespace
{
constexpr double sr = 44100.0;
}

/** Diezel VH4: four channel netlists (Clean/Crunch/Mega/Lead), LTP PI, 4x KT77, NFB with Presence + Deep. */
class DiezelStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    using Amp = DiezelStyleAmplifierProcessor;
    using P = Amp::Probe;

    DiezelStyleAmplifierProcessorTests() : juce::UnitTest ("DiezelStyleAmplifier", "Amplifier") {}

    static constexpr const char* px = "dzl_";

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
        // reducedOrder is a static flag: an earlier suite that ran registerBuiltInEffects
        // leaves it true, and in reducedOrder the physical power stage is bypassed so every
        // power-section DC probe below reads 0. Pin the full netlist like the sibling suites do.
        Amp::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            Amp amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("rails: plates " + juce::String (amp.railPlates(), 1) + ", screens " + juce::String (amp.railScreens(), 1));
            amp.debugSetChannel (3);
            logMessage ("Lead V " + juce::String (amp.debugVoltage (P::chanPlate, 1), 1)
                        + " / " + juce::String (amp.debugVoltage (P::chanPlate, 4), 1)
                        + ", PI " + juce::String (amp.debugVoltage (P::piPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::piPlateB), 1)
                        + " k " + juce::String (amp.debugVoltage (P::piCathode), 1)
                        + ", bias " + juce::String (amp.debugVoltage (P::biasNode), 1)
                        + ", Ip " + juce::String (amp.plateCurrentA() * 1000.0, 1) + " / " + juce::String (amp.plateCurrentB() * 1000.0, 1) + " mA");
            // service notes: ~430 V plates, ~420 V screens, PI ~310 V, fixed bias around -44 V
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 460.0);
            expect (amp.railScreens() > 380.0 && amp.railScreens() < 440.0);
            expect (amp.debugVoltage (P::chanPlate, 1) > 60.0 && amp.debugVoltage (P::chanPlate, 1) < 280.0, "preamp plate");
            expect (amp.debugVoltage (P::piPlateA) > 150.0 && amp.debugVoltage (P::piPlateA) < 290.0, "PI plate");
            expect (amp.debugVoltage (P::biasNode) < -30.0 && amp.debugVoltage (P::biasNode) > -60.0, "fixed-bias node");
            expect (amp.plateCurrentA() > 0.030 && amp.plateCurrentA() < 0.140, "KT77 pair idle current");
            expect (std::abs (amp.plateCurrentA() - amp.plateCurrentB()) < 0.004, "matched pairs at idle");
        }

        beginTest ("silence stays silent: the global-NFB loop must not self-oscillate, on any channel");
        {
            for (float chSel : { 0.0f, 1.0f, 2.0f, 3.0f })
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

        beginTest ("a hot guitar-level signal stays finite, bounded and solved on every channel");
        {
            for (float chSel : { 0.0f, 1.0f, 2.0f, 3.0f })
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "volume", 1.0f);
                setParam (amp, juce::String (px) + "master", 1.0f);
                setParam (amp, juce::String (px) + "gain", 1.0f);
                const double rms = sineRms (amp, 196.0, 0.5);
                logMessage ("channel " + juce::String (chSel, 0) + " full gain, 0.5 V 196 Hz: out RMS " + juce::String (rms, 4)
                            + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                            + ", recoveries " + juce::String (amp.debugRecoveries()));
                expect (std::isfinite (rms) && rms > 0.02 && rms < 1.5);
                expectLessThan (amp.getSolveFailureRate(), 0.001);
            }
        }

        beginTest ("parameters exist and reach the circuit");
        {
            Amp probe;
            for (auto* id : { "channel", "volume", "gain", "treble", "middle", "bass", "master", "presence", "deep",
                              "power", "bias", "tube_feel", "speaker", "output" })
                expect (hasParam (probe, juce::String (px) + id), juce::String (id));

            auto level = [] (float chSel, const char* id, float v, double hz, double amplitude)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "volume", 0.25f);
                setParam (amp, juce::String (px) + "master", 0.5f);
                setParam (amp, juce::String (px) + "gain", 0.25f);
                setParam (amp, juce::String (px) + id, v);
                return sineRms (amp, hz, amplitude);
            };
            const double clean = level (0.0f, "volume", 0.25f, 440.0, 0.002);
            const double lead = level (3.0f, "gain", 0.25f, 440.0, 0.002);
            logMessage ("clean vs lead channel out at matched settings: " + juce::String (clean, 5) + " / " + juce::String (lead, 5));
            expectGreaterThan (lead, clean * 1.3);

            const double trLo = level (2.0f, "treble", 0.0f, 3000.0, 0.002), trHi = level (2.0f, "treble", 1.0f, 3000.0, 0.002);
            logMessage ("treble 0 / 1 at 3 kHz: " + juce::String (trLo, 5) + " / " + juce::String (trHi, 5));
            expectGreaterThan (trHi, trLo * 1.3);

            const double pLo = level (0.0f, "presence", 0.0f, 6000.0, 0.002), pHi = level (0.0f, "presence", 1.0f, 6000.0, 0.002);
            logMessage ("presence 0 / 1 at 6 kHz: " + juce::String (pLo, 5) + " / " + juce::String (pHi, 5));
            expectGreaterThan (pHi, pLo * 1.15);

            const double dLo = level (0.0f, "deep", 0.0f, 100.0, 0.002), dHi = level (0.0f, "deep", 1.0f, 100.0, 0.002);
            logMessage ("deep 0 / 1 at 100 Hz: " + juce::String (dLo, 5) + " / " + juce::String (dHi, 5));
            expectGreaterThan (dHi, dLo * 1.1);
        }

        beginTest ("the four channels voice differently: rising gain and distinct curves");
        {
            // matched settings per channel, hot enough input to hit each stage count
            auto chanLevel = [] (float chSel)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "gain", 0.7f);
                setParam (amp, juce::String (px) + "volume", 0.7f);
                setParam (amp, juce::String (px) + "master", 0.7f);
                return sineRms (amp, 220.0, 0.05);
            };
            const double l0 = chanLevel (0.0f), l1 = chanLevel (1.0f), l2 = chanLevel (2.0f), l3 = chanLevel (3.0f);
            logMessage ("per-channel out (clean/crunch/mega/lead): " + juce::String (l0, 5) + " / " + juce::String (l1, 5)
                        + " / " + juce::String (l2, 5) + " / " + juce::String (l3, 5));
            expectGreaterThan (l2, l0);       // mega is hotter than clean at matched knobs
            expectGreaterThan (l3, l1);       // lead is hotter than crunch
        }
    }
};

static DiezelStyleAmplifierProcessorTests diezelStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
