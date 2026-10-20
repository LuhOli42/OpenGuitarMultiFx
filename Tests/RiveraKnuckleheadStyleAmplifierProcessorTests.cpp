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
            for (float chSel : { 0.0f, 1.0f, 2.0f })
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
            for (float chSel : { 0.0f, 1.0f, 2.0f })
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
                              "power", "bias", "tube_feel", "speaker", "output",
                              "bright", "notch", "contour", "scoop", "boost", "ninja" })
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
            const double lead = level (2.0f, "gain", 0.25f, 440.0, 0.002);
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

            // pull-switches: CH1 boost and CH2/CH3 ninja each switch in a real extra triode stage
            auto levelPull = [] (float chSel, const char* id, float v, double hz, double amplitude)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", chSel);
                setParam (amp, juce::String (px) + "volume", 0.5f);
                setParam (amp, juce::String (px) + "gain", 0.5f);
                setParam (amp, juce::String (px) + id, v);
                return sineRms (amp, hz, amplitude);
            };
            const double bOff = levelPull (0.0f, "boost", 0.0f, 440.0, 0.01);
            const double bOn = levelPull (0.0f, "boost", 1.0f, 440.0, 0.01);
            logMessage ("CH1 boost off/on: " + juce::String (bOff, 5) + " / " + juce::String (bOn, 5));
            expectGreaterThan (bOn, bOff * 1.5);

            const double nOff = levelPull (1.0f, "ninja", 0.0f, 440.0, 0.005);
            const double nOn = levelPull (1.0f, "ninja", 1.0f, 440.0, 0.005);
            logMessage ("CH2 ninja off/on: " + juce::String (nOff, 5) + " / " + juce::String (nOn, 5));
            expectGreaterThan (nOn, nOff * 1.5);
        }

        beginTest ("legacy 2-channel presets migrate: a saved Lead (1) still loads as Lead");
        {
            auto channelValue = [] (Amp& amp)
            {
                for (auto* p : amp.getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == juce::String (px) + "channel")
                            return (double) f->get();
                return -1.0;
            };
            // a state written before the pull params existed (no knk_ninja) with channel=1 meant Lead
            Amp old;
            juce::XmlElement legacy ("EffectState");
            legacy.setAttribute (juce::String (px) + "channel", 1.0);
            old.setState (legacy);
            expectEquals (channelValue (old), 2.0);
            // a current-format state with channel=1 really is Crunch
            Amp now;
            juce::XmlElement current ("EffectState");
            current.setAttribute (juce::String (px) + "channel", 1.0);
            current.setAttribute (juce::String (px) + "ninja", 0.0);
            now.setState (current);
            expectEquals (channelValue (now), 1.0);
        }

        beginTest ("legacy snapshot blocks migrate: a snapshot-stored Lead (1) recalls as Lead");
        {
            // SnapshotPresetXml::applyBlock writes (id, value) pairs straight onto the
            // params, bypassing setState -- the migration has to happen on the pairs.
            Amp legacy;
            std::vector<std::pair<juce::String, float>> legacyParams {
                { juce::String (px) + "channel", 1.0f },
                { juce::String (px) + "gain", 0.5f },
            };
            legacy.migrateLegacySnapshotParams (legacyParams);
            float migrated = -1.0f;
            for (const auto& [id, v] : legacyParams)
                if (id == juce::String (px) + "channel")
                    migrated = v;
            expectEquals ((double) migrated, 2.0);

            Amp current;
            std::vector<std::pair<juce::String, float>> currentParams {
                { juce::String (px) + "channel", 1.0f },
                { juce::String (px) + "ninja", 0.0f },
            };
            current.migrateLegacySnapshotParams (currentParams);
            for (const auto& [id, v] : currentParams)
                if (id == juce::String (px) + "channel")
                    migrated = v;
            expectEquals ((double) migrated, 1.0);
        }

        beginTest ("Focus lifts bass without shifting PI cathode DC");
        {
            const auto cathodeAt = [] (float focus)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "focus", focus);
                amp.prepare (sr, 128, 1);
                return amp.debugVoltage (P::piCathode);
            };
            const double piK0 = cathodeAt (0.0f);
            const double piK1 = cathodeAt (1.0f);
            const double dcDeltaPercent = std::abs (piK1 - piK0) / juce::jmax (1.0e-9, std::abs (piK0)) * 100.0;
            logMessage ("PI cathode DC Focus 0 / 1: " + juce::String (piK0, 5) + " / " + juce::String (piK1, 5)
                        + " V (" + juce::String (dcDeltaPercent, 3) + "% change)");
            expectLessThan (dcDeltaPercent, 2.0);

            const auto response = [] (float focus, double hz)
            {
                Amp amp;
                setParam (amp, juce::String (px) + "channel", 0.0f);
                setParam (amp, juce::String (px) + "volume", 0.25f);
                setParam (amp, juce::String (px) + "gain", 0.25f);
                setParam (amp, juce::String (px) + "focus", focus);
                return sineRms (amp, hz, 0.01);
            };
            const double full80_0 = response (0.0f, 80.0);
            const double full80_1 = response (1.0f, 80.0);
            const double full3k_0 = response (0.0f, 3000.0);
            const double full3k_1 = response (1.0f, 3000.0);
            const double fullLift80Db = 20.0 * std::log10 (full80_1 / full80_0);
            const double fullDelta3kDb = 20.0 * std::log10 (full3k_1 / full3k_0);
            logMessage ("full-order Focus 0 / 1 RMS: 80 Hz " + juce::String (full80_0, 7) + " / "
                        + juce::String (full80_1, 7) + " V, 3 kHz " + juce::String (full3k_0, 7) + " / "
                        + juce::String (full3k_1, 7) + " V; changes " + juce::String (fullLift80Db, 3)
                        + " / " + juce::String (fullDelta3kDb, 3) + " dB");
            expectGreaterThan (fullLift80Db, 2.0);
            expectLessThan (std::abs (fullDelta3kDb), 1.0);

            const bool wasReduced = Amp::reducedOrder;
            Amp::reducedOrder = true;
            const double reduced80_0 = response (0.0f, 80.0);
            const double reduced80_1 = response (1.0f, 80.0);
            const double reduced3k_0 = response (0.0f, 3000.0);
            const double reduced3k_1 = response (1.0f, 3000.0);
            Amp::reducedOrder = wasReduced;
            const double reducedLift80Db = 20.0 * std::log10 (reduced80_1 / reduced80_0);
            const double reducedDelta3kDb = 20.0 * std::log10 (reduced3k_1 / reduced3k_0);
            logMessage ("reduced-order Focus 0 / 1 RMS: 80 Hz " + juce::String (reduced80_0, 7) + " / "
                        + juce::String (reduced80_1, 7) + " V, 3 kHz " + juce::String (reduced3k_0, 7) + " / "
                        + juce::String (reduced3k_1, 7) + " V; changes " + juce::String (reducedLift80Db, 3)
                        + " / " + juce::String (reducedDelta3kDb, 3) + " dB");
            expectGreaterThan (reducedLift80Db, 1.0);
            expectLessThan (std::abs (reducedDelta3kDb), 1.0);
        }
    }
};

static RiveraKnuckleheadStyleAmplifierProcessorTests riveraKnuckleheadStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
