#include "Effects/AC15StyleAmplifierProcessor.h"
#include "Effects/AmpegV4BStyleAmplifierProcessor.h"
#include "Effects/BognerUberschallStyleAmplifierProcessor.h"
#include "Effects/DualRectifierStyleAmplifierProcessor.h"
#include "Effects/DumbleSteelStringStyleAmplifierProcessor.h"
#include "Effects/ENGLPowerballStyleAmplifierProcessor.h"
#include "Effects/EVH5150StyleAmplifierProcessor.h"
#include "Effects/FryetteDeliveranceD120StyleAmplifierProcessor.h"
#include "Effects/JCM800StyleAmplifierProcessor.h"
#include "Effects/MarkIICPlusStyleAmplifierProcessor.h"
#include "Effects/RockerverbStyleAmplifierProcessor.h"
#include "Effects/SLO100StyleAmplifierProcessor.h"
#include "Effects/SunnModelTStyleAmplifierProcessor.h"
#include "Effects/SuperLeadStyleAmplifierProcessor.h"
#include "Effects/TwinReverbStyleAmplifierProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdlib>

namespace openguitarmultifx
{

/**
    Silence-stability + reference-level probe for the amps whose full-netlist (reducedOrder=false)
    reference path was documented as gain-collapsed (the ~32-57 dB dead-NFB/low-gain states locked
    by AmpFullVsReducedConsistencyTests).

    Two checks per amp:
      * SILENCE: several seconds of zero input must stay quiet (the old defect grew a ~1-3 Hz
        limit cycle out of the idle point). Peak speaker volts is logged and bounded.
      * DRIVE: a 0.2 sine at the calibration point must land near the reduced path's own level --
        "the reference produces a sane output", not bit-identity with the fitted stage.

    Debug/env knobs while developing a fix:
      AMP_ONLY=jcm800,slo100  restrict to a subset
      AMP_SINE=0.2            driven sine amplitude
      AMP_SECONDS=6           silence soak length
*/
class AmpReferenceHealthTests : public juce::UnitTest
{
public:
    AmpReferenceHealthTests() : juce::UnitTest ("AmpReferenceHealth", "Probe") {}

    static constexpr double sr = 48000.0;

    struct Result
    {
        double silencePeak = 0.0;   // speaker volts
        double driveRmsFull = 0.0;  // emitted, probe scale
        double driveRmsRed = 0.0;
        double failRate = 0.0;
    };

    static void applyCalibParams (EffectProcessor& fx)
    {
        const auto pages = fx.getParameterPages();
        static const std::pair<const char*, float> rules[] = {
            { "_power", 1.0f }, { "_master", 1.0f }, { "_input", 0.0f },
            { "_volume", 0.8f }, { "_loud1", 0.7f }, { "_loud2", 0.7f },
            { "_vol_normal", 0.8f }, { "_vol_bright", 0.8f },
        };
        for (const auto& page : pages)
            for (auto* f : page)
                for (const auto& r : rules)
                    if (juce::String (f->paramID).endsWith (r.first))
                        *f = juce::jlimit (f->range.start, f->range.end, r.second);
    }

    template <typename Amp>
    static double silencePeak (double seconds)
    {
        const bool prev = Amp::reducedOrder;
        Amp::reducedOrder = false;
        double peak = 0.0;
        {
            Amp amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 1);
            // A healed-but-stiff netlist can ring for a few hundred ms while its DC point settles --
            // that decaying startup transient is not the defect being hunted (a limit cycle that
            // SUSTAINS or GROWS). Measure the peak over the last second of the soak only.
            const long long total = (long long) (seconds * sr), tail = (long long) (1.0 * sr);
            for (long long n = 0; n < total; ++n)
            {
                buf.clear();
                amp.process (buf);
                if (n >= total - tail)
                {
                    if constexpr (requires { amp.debugVoltage (Amp::Probe::speaker); })
                        peak = juce::jmax (peak, std::abs (amp.debugVoltage (Amp::Probe::speaker)));
                    else
                        peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, 0)));
                }
            }
        }
        Amp::reducedOrder = prev;
        return peak;
    }

    template <typename Amp>
    static double driveRms (bool reduced, double sineAmp, double* failRate)
    {
        const bool prev = Amp::reducedOrder;
        Amp::reducedOrder = reduced;
        double ss = 0.0;
        long long count = 0;
        {
            Amp amp;
            applyCalibParams (amp);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 1);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            const long long warm = (long long) (1.5 * sr), total = warm + (long long) (0.5 * sr);
            for (long long n = 0; n < total; ++n)
            {
                buf.setSample (0, 0, (float) (sineAmp * std::sin (twoPi * 220.0 * (double) n / sr)));
                buf.setSample (1, 0, buf.getSample (0, 0));
                amp.process (buf);
                if (n >= warm)
                {
                    double y;
                    if constexpr (requires { amp.debugVoltage (Amp::Probe::speaker); })
                        y = amp.debugVoltage (Amp::Probe::speaker);
                    else
                        y = buf.getSample (0, 0);
                    ss += y * y;
                    ++count;
                }
            }
            if (failRate != nullptr)
                *failRate = amp.getSolveFailureRate();
        }
        Amp::reducedOrder = prev;
        return std::sqrt (ss / (double) count);
    }

    template <typename Amp>
    void probeAmp (const char* name, double gapBoundDb = 12.0)
    {
        Result r;
        r.silencePeak = silencePeak<Amp> (seconds());
        r.driveRmsFull = driveRms<Amp> (false, sineAmp(), &r.failRate);
        r.driveRmsRed = driveRms<Amp> (true, sineAmp(), nullptr);
        const double deltaDb = 20.0 * std::log10 ((r.driveRmsRed + 1.0e-9) / (r.driveRmsFull + 1.0e-9));
        logMessage (juce::String (name)
                    + " silencePeak " + juce::String (r.silencePeak, 4)
                    + " fullRms " + juce::String (r.driveRmsFull, 4)
                    + " redRms " + juce::String (r.driveRmsRed, 4)
                    + " redMinusFullDb " + juce::String (deltaDb, 1)
                    + " failRate " + juce::String (r.failRate, 5));

        // Silence must stay quiet: a limit cycle or motorboating blows past a few volts on the
        // speaker node within seconds. The band-aided (kg1-collapsed) netlists passed this only
        // by being too weak to oscillate; a healed reference passes it at full gm.
        expect (r.silencePeak < 2.0,
                juce::String (name) + ": full reference oscillates on silence (peak "
                    + juce::String (r.silencePeak, 2) + " V)");

        // The healed reference should land within ~12 dB of the reduced path's level (the reduced
        // stage was fitted, so identity is not the goal -- a healthy, same-order output is).
        // v4b and rockerverb get a looser bound: v4b's 15.8:1 half-to-secondary OT ratio onto the
        // 4-ohm tap caps speaker volts by design, and rockerverb's PI is healthy but fed a weak
        // ~2 V signal by its faithful upstream network -- both are documented gaps vs the fitted
        // reduced stage, not collapses.
        expect (std::abs (deltaDb) < gapBoundDb,
                juce::String (name) + ": reference still collapsed (red-full " + juce::String (deltaDb, 1) + " dB)");
    }

    /** AMP_NODES=name renders the driven sine and logs RMS at a chain of internal probes --
        bisects WHERE the reference loses gain. Probe sets are per-amp (enums differ). */
    template <typename Amp>
    void probeNodes (const char* name)
    {
        const bool prev = Amp::reducedOrder;
        Amp::reducedOrder = false;
        struct R { double gridA = 0, plateA = 0, plateB = 0, pwrGrid = 0, pwrPlateA = 0, pwrPlateB = 0, speaker = 0,
                   toneOut = 0, v7b = 0; };
        R r;
        {
            Amp amp;
            applyCalibParams (amp);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 1);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            const long long warm = (long long) (1.5 * sr), total = warm + (long long) (0.5 * sr);
            double ss[9] = {}, mm[9] = {};
            for (long long n = 0; n < total; ++n)
            {
                buf.setSample (0, 0, (float) (sineAmp() * std::sin (twoPi * 220.0 * (double) n / sr)));
                buf.setSample (1, 0, buf.getSample (0, 0));
                amp.process (buf);
                if (n >= warm)
                {
                    double v[9] = {
                        amp.debugVoltage (Amp::Probe::phaseInverterGrid),
                        amp.debugVoltage (Amp::Probe::phaseInverterPlateA),
                        amp.debugVoltage (Amp::Probe::phaseInverterPlateB),
                        amp.debugVoltage (Amp::Probe::powerGridA),
                        amp.debugVoltage (Amp::Probe::powerPlateA),
                        amp.debugVoltage (Amp::Probe::powerPlateB),
                        amp.debugVoltage (Amp::Probe::speaker), 0.0, 0.0 };
                    if constexpr (requires { amp.debugVoltage (Amp::Probe::toneStackOut); })
                        v[7] = amp.debugVoltage (Amp::Probe::toneStackOut);
                    if constexpr (requires { amp.debugVoltage (Amp::Probe::v7bPlate); })
                        v[8] = amp.debugVoltage (Amp::Probe::v7bPlate);
                    for (int i = 0; i < 9; ++i) { ss[i] += v[i] * v[i]; mm[i] += v[i]; }
                }
            }
            auto ac = [&] (int i)
            {
                const double m = mm[i] / (total - warm);
                return std::sqrt (juce::jmax (0.0, ss[i] / (total - warm) - m * m));
            };
            r.gridA = ac (0); r.plateA = ac (1); r.plateB = ac (2);
            r.pwrGrid = ac (3); r.pwrPlateA = ac (4); r.pwrPlateB = ac (5); r.speaker = ac (6);
            r.toneOut = ac (7); r.v7b = ac (8);
        }
        Amp::reducedOrder = prev;
        logMessage (juce::String (name) + " NODES piGrid " + juce::String (r.gridA, 2)
                    + " piPlateA " + juce::String (r.plateA, 2) + " piPlateB " + juce::String (r.plateB, 2)
                    + " pwrGrid " + juce::String (r.pwrGrid, 2)
                    + " pwrPlateA " + juce::String (r.pwrPlateA, 2) + " pwrPlateB " + juce::String (r.pwrPlateB, 2)
                    + " spk " + juce::String (r.speaker, 2)
                    + " tone " + juce::String (r.toneOut, 2) + " v7b " + juce::String (r.v7b, 2));
    }

    /** AMP_DUMP=jcm800 renders the guitar-DI fixture through the full netlist (reducedOrder=false)
        at the calibration point and writes /tmp/<name>-full.wav -- so a healed reference can be
        heard, not just measured. Runs INSTEAD of the normal probes (same convention as AMP_NODES). */
    template <typename Amp>
    void dumpFull (const char* name)
    {
        const auto f = juce::File (OGMFX_TEST_FIXTURES_DIR).getChildFile ("fixtures").getChildFile ("di").getChildFile ("guitar-di.wav");
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr)
        {
            expect (false, "DI fixture missing -- check Tests/fixtures/di/guitar-di.wav");
            return;
        }
        const int n = (int) rd->lengthInSamples;
        juce::AudioBuffer<float> src ((int) rd->numChannels, n);
        rd->read (&src, 0, n, 0, true, true);
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float acc = 0.0f;
            for (int cIdx = 0; cIdx < src.getNumChannels(); ++cIdx) acc += src.getSample (cIdx, i);
            x[(size_t) i] = acc / (float) src.getNumChannels();
        }
        const bool prev = Amp::reducedOrder;
        Amp::reducedOrder = false;
        juce::AudioBuffer<float> out (2, n);
        {
            Amp amp;
            applyCalibParams (amp);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            for (int i = 0; i < n; i += 128)
            {
                const int num = juce::jmin (128, n - i);
                buf.clear();
                for (int j = 0; j < num; ++j)
                {
                    buf.setSample (0, j, x[(size_t) (i + j)]);
                    buf.setSample (1, j, x[(size_t) (i + j)]);
                }
                amp.process (buf);
                for (int j = 0; j < num; ++j)
                {
                    out.copyFrom (0, i + j, buf, 0, j, 1);
                    out.copyFrom (1, i + j, buf, 1, j, 1);
                }
            }
        }
        Amp::reducedOrder = prev;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (out.getSample (0, i)));
        if (peak > 1.0e-9f) out.applyGain (0.7f / peak);
        const auto outF = juce::File ("/tmp/" + juce::String (name) + "-full.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (outF.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> wr (wav.createWriterFor (os.get(), sr, 2, 16, {}, 0));
        os.release();
        if (wr != nullptr)
        {
            wr->writeFromAudioSampleBuffer (out, 0, n);
            wr.reset();
        }
        logMessage ("wrote " + outF.getFullPathName() + " (pre-normalize peak " + juce::String (peak, 1) + " V)");
    }

    static const char* nodesWanted()
    {
        static const juce::String s = std::getenv ("AMP_NODES") != nullptr ? std::getenv ("AMP_NODES") : "";
        return s.isEmpty() ? nullptr : static_cast<const char*> (s.getCharPointer());
    }

    static bool wants (const char* name)
    {
        static const juce::String only = std::getenv ("AMP_ONLY") != nullptr ? std::getenv ("AMP_ONLY") : "";
        return only.isEmpty() || juce::StringArray::fromTokens (only, ",", {}).contains (name);
    }
    static double sineAmp() { const char* e = std::getenv ("AMP_SINE"); return e != nullptr ? std::atof (e) : 0.2; }
    static double seconds() { const char* e = std::getenv ("AMP_SECONDS"); return e != nullptr ? std::atof (e) : 5.0; }

    static const char* dumpWanted()
    {
        static const juce::String s = std::getenv ("AMP_DUMP") != nullptr ? std::getenv ("AMP_DUMP") : "";
        return s.isEmpty() ? nullptr : static_cast<const char*> (s.getCharPointer());
    }

    void runTest() override
    {
        beginTest ("full-netlist references are silence-stable and level-sane");
        if (dumpWanted() != nullptr)
        {
            const juce::String sel (dumpWanted());
            if (sel == "rockerverb")  dumpFull<RockerverbStyleAmplifierProcessor> ("rockerverb");
            if (sel == "evh5150")     dumpFull<EVH5150StyleAmplifierProcessor> ("evh5150");
            if (sel == "v4b")         dumpFull<AmpegV4BStyleAmplifierProcessor> ("v4b");
            if (sel == "sunn")        dumpFull<SunnModelTStyleAmplifierProcessor> ("sunn");
            if (sel == "jcm800")      dumpFull<JCM800StyleAmplifierProcessor> ("jcm800");
            if (sel == "superlead")   dumpFull<SuperLeadStyleAmplifierProcessor> ("superlead");
            return;
        }
        if (nodesWanted() != nullptr)
        {
            const juce::String sel (nodesWanted());
            if (sel == "rockerverb")  probeNodes<RockerverbStyleAmplifierProcessor> ("rockerverb");
            if (sel == "evh5150")     probeNodes<EVH5150StyleAmplifierProcessor> ("evh5150");
            if (sel == "v4b")         probeNodes<AmpegV4BStyleAmplifierProcessor> ("v4b");
            if (sel == "sunn")        probeNodes<SunnModelTStyleAmplifierProcessor> ("sunn");
            if (sel == "superlead")   probeNodes<SuperLeadStyleAmplifierProcessor> ("superlead");
            if (sel == "jcm800")      probeNodes<JCM800StyleAmplifierProcessor> ("jcm800");
            return;
        }
        if (wants ("jcm800"))      probeAmp<JCM800StyleAmplifierProcessor> ("jcm800");
        if (wants ("slo100"))      probeAmp<SLO100StyleAmplifierProcessor> ("slo100");
        if (wants ("markiic"))     probeAmp<MarkIICPlusStyleAmplifierProcessor> ("markiic");
        if (wants ("dualrect"))    probeAmp<DualRectifierStyleAmplifierProcessor> ("dualrect");
        if (wants ("evh5150"))     probeAmp<EVH5150StyleAmplifierProcessor> ("evh5150");
        if (wants ("powerball"))   probeAmp<ENGLPowerballStyleAmplifierProcessor> ("powerball");
        if (wants ("rockerverb"))  probeAmp<RockerverbStyleAmplifierProcessor> ("rockerverb", 16.0);
        if (wants ("dumble"))      probeAmp<DumbleSteelStringStyleAmplifierProcessor> ("dumble");
        if (wants ("sunn"))        probeAmp<SunnModelTStyleAmplifierProcessor> ("sunn");
        if (wants ("deliverance")) probeAmp<FryetteDeliveranceD120StyleAmplifierProcessor> ("deliverance");
        if (wants ("uberschall"))  probeAmp<BognerUberschallStyleAmplifierProcessor> ("uberschall");
        if (wants ("v4b"))         probeAmp<AmpegV4BStyleAmplifierProcessor> ("v4b", 20.0);
        // healthy controls
        if (wants ("superlead"))   probeAmp<SuperLeadStyleAmplifierProcessor> ("superlead");
        if (wants ("twin"))        probeAmp<TwinReverbStyleAmplifierProcessor> ("twin");
    }
};

static AmpReferenceHealthTests ampReferenceHealthTests;

} // namespace openguitarmultifx
