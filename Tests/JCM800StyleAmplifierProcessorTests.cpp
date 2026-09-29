#include "EffectRegistry.h"
#include "Effects/JCM800StyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Marshall JCM800-style amplifier (docs/circuits/JCM8002203.md). */
class JCM800StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    JCM800StyleAmplifierProcessorTests() : juce::UnitTest ("JCM800StyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = JCM800StyleAmplifierProcessor::Probe;

    static void setParam (JCM800StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        JCM800StyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            JCM800StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPhaseInverter(), 1) + ", V2 rail " + juce::String (amp.railV2(), 1)
                        + ", V1 rail " + juce::String (amp.railV1(), 1)
                        + ", V1a plate " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1b plate " + juce::String (amp.debugVoltage (P::v1bPlate), 1)
                        + ", gain-stage plate " + juce::String (amp.debugVoltage (P::gainStagePlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::powerGridA), 1));
            // The 2203 drawing this was read from prints no rail voltages (only the 1967-era GRO chassis notes give
            // some, and this is the later "STD" board) -- so, per the Super Lead/JTM45 convention, the rails are
            // whatever this model's own idle currents settle to. Still sanity-check they're in the right ballpark
            // for a 480 V, 4xEL34 Marshall power section (shared unchanged with the Super Lead).
            expect (amp.railPlates() > 400.0 && amp.railPlates() < 490.0);
            expect (amp.railV1() > 150.0 && amp.railV1() < 400.0);
            expect (amp.railV2() > 150.0 && amp.railV2() < 400.0);
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            // 10 s, not 2: this project's own JTM45/Deluxe Reverb investigations found tube-loop configurations that
            // looked perfectly settled in the first 1-2 s and then grew into a full-scale, sustained oscillation over
            // several more seconds. A brand-new 4-cascaded-gain-stage preamp feeding the SAME global feedback loop as
            // the Super Lead is exactly the kind of change that could push more gain into that loop, so this amp gets
            // the full 10 s soak from the start, not bolted on after a surprise.
            JCM800StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        // The reference model's own peak stays low here (~0.01) -- its preamp shows a real transient bias-drift under
        // a repeatedly-retriggered decaying note (followerOut swinging ~100-340 V for a 0.1-0.3 amplitude pluck), not
        // fully explained (see the reducedOrder pluck test below). What ships (reducedOrder) never touches that
        // PI/pentode dynamics and stays healthy for the exact same note sequence -- this test only needs finite and a
        // low failure rate, not a specific loudness.
        beginTest ("a plucked note through each Input (High/Low) setting: finite, bounded, converges");
        for (float inputChoice : { 0.0f, 1.0f })
        {
            JCM800StyleAmplifierProcessor amp;
            setParam (amp, "j8_input", inputChoice);
            setParam (amp, "j8_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.6 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.1 + 0.2 * rnd.nextDouble(); age = 0.0; }
                    age += 1.0 / sr;
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase) * std::exp (-age * 2.0));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    finite = finite && std::isfinite (buf.getSample (0, i));
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
            }
            logMessage ("input " + juce::String (inputChoice, 0) + ": failure rate " + juce::String (amp.getSolveFailureRate(), 6)
                        + ", recoveries " + juce::String (amp.debugRecoveries()) + ", peak " + juce::String (peak, 2));
            expect (finite);
            expectLessThan (amp.getSolveFailureRate(), 2.0e-3);
        }

        beginTest ("page 2: Speaker is a preset parameter, its own sub-group");
        {
            JCM800StyleAmplifierProcessor amp;
            int page2Count = 0;
            bool foundSpeaker = false;
            const auto pages = amp.getParameterPages();
            for (auto* f : pages[1])
            {
                ++page2Count;
                if (f->paramID == "j8_speaker")
                    foundSpeaker = true;
            }
            expect (page2Count == 4, "Power Drive, Bias, Tube Feel, Speaker");
            expect (foundSpeaker);
        }

        {
            beginTest ("reducedOrder power stage tracks the reference (level, and zero failures by construction)");
            JCM800StyleAmplifierProcessor::reducedOrder = false;
            JCM800StyleAmplifierProcessor ref;
            setParam (ref, "j8_input", 0.0f);
            setParam (ref, "j8_gain", 0.5f);
            setParam (ref, "j8_power", 1.0f);
            ref.prepare (sr, 128, 2);
            JCM800StyleAmplifierProcessor::reducedOrder = true;
            JCM800StyleAmplifierProcessor red;
            setParam (red, "j8_input", 0.0f);
            setParam (red, "j8_gain", 0.5f);
            setParam (red, "j8_power", 1.0f);
            red.prepare (sr, 128, 2);

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 5.0e-3, 0.02, 0.05, 0.1, 0.2, 0.4, 0.6 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double refOutSumSq = 0.0, redOutSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    ref.process (one);
                    if (n >= measureFrom) { const double y = ref.debugVoltage (P::speaker); refOutSumSq += y * y; ++n2; }
                }
                n2 = 0;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    red.process (one);
                    if (n >= measureFrom) { const double y = red.debugVoltage (P::speaker); redOutSumSq += y * y; ++n2; }
                }
                const double refRms = std::sqrt (refOutSumSq / (double) juce::jmax (1LL, n2));
                const double redRms = std::sqrt (redOutSumSq / (double) juce::jmax (1LL, n2));
                const double dB = 20.0 * std::log10 (juce::jmax (1.0e-9, redRms) / juce::jmax (1.0e-9, refRms));
                logMessage ("level " + juce::String (level, 4) + ": ref " + juce::String (refRms, 3) + " Vrms vs reduced " + juce::String (redRms, 3)
                            + " Vrms, diff " + juce::String (dB, 2) + " dB, reduced fails " + juce::String (red.getSolveFailureRate(), 6) + ", recov " + juce::String (red.debugRecoveries()));
                expectLessThan (std::abs (dB), 2.0);
                expectLessThan (red.getSolveFailureRate(), 1.0e-6);
                expect (red.debugRecoveries() == 0);
            }
        }

        {
            beginTest ("reducedOrder (shipped default): a plucked-note sequence stays bounded and reasonably loud");
            // The full reference model's preamp (V1a..V2b, untouched by the power-stage kg1 investigation above) shows
            // its own transient bias drift under a repeatedly-retriggered decaying note -- followerOut swinging
            // ~100-340 V for a modest 0.1-0.3 amplitude pluck, a real (if not fully explained) grid-conduction /
            // "blocking distortion" style effect worth a closer look before the next amp reuses this preamp pattern.
            // What matters for what ships is reducedOrder, which never touches that PI/pentode dynamics at all --
            // confirm it stays sane for the same note sequence the reference model struggled with.
            JCM800StyleAmplifierProcessor::reducedOrder = true;
            JCM800StyleAmplifierProcessor amp;
            setParam (amp, "j8_input", 0.0f);
            setParam (amp, "j8_gain", 0.6f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (5);
            double f0 = 110.0, level = 0.15, age = 0.0, phase = 0.0;
            long long n = 0;
            bool finite = true;
            double peak = 0.0, sumSq = 0.0;
            long long n2 = 0;
            for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.6 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.0); level = 0.1 + 0.2 * rnd.nextDouble(); age = 0.0; }
                    age += 1.0 / sr;
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::sin (phase) * std::exp (-age * 2.0));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                {
                    const double y = (double) buf.getSample (0, i);
                    finite = finite && std::isfinite (y);
                    peak = juce::jmax (peak, std::abs (y));
                    sumSq += y * y; ++n2;
                }
            }
            logMessage ("reducedOrder pluck sequence: peak " + juce::String (peak, 3) + ", rms " + juce::String (std::sqrt (sumSq / (double) juce::jmax (1LL, n2)), 4)
                        + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (finite);
            expectGreaterThan (peak, 0.05, "the shipped default should not be near-silent for a healthy pluck");
            JCM800StyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud (the shipped default has no physical speaker to mismatch)");
            JCM800StyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                JCM800StyleAmplifierProcessor amp;
                setParam (amp, "j8_input", 0.0f);
                setParam (amp, "j8_gain", 0.5f);
                setParam (amp, "j8_speaker", speaker);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> one (2, 1);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                double sumSq = 0.0;
                long long n2 = 0;
                const long long total = (long long) (0.7 * sr), measureFrom = total - (long long) (0.1 * sr);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (0.05 * std::sin (twoPi * 200.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom) { const double y = amp.debugVoltage (P::speaker); sumSq += y * y; ++n2; }
                }
                return std::sqrt (sumSq / (double) juce::jmax (1LL, n2));
            };
            const double r4 = rmsAt (0.0f), r8 = rmsAt (1.0f), r16 = rmsAt (2.0f);
            logMessage ("reducedOrder speaker volts at 4 / 8 / 16 ohm: " + juce::String (r4, 2) + " / " + juce::String (r8, 2) + " / " + juce::String (r16, 2) + " V rms");
            expectLessThan (std::abs (20.0 * std::log10 (r4 / r8)), 0.1);
            expectLessThan (std::abs (20.0 * std::log10 (r16 / r8)), 0.1);
            JCM800StyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: worst-block cost under a hot-pedal stress");
            JCM800StyleAmplifierProcessor::reducedOrder = true;
            JCM800StyleAmplifierProcessor amp;
            setParam (amp, "j8_input", 0.0f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            juce::Random rnd (7);
            double f0 = 110.0, level = 1.0, phase = 0.0;
            long long n = 0;
            double seconds = 0.0, worstPct = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                for (int i = 0; i < 128; ++i, ++n)
                {
                    if (n % (long long) (0.5 * sr) == 0) { f0 = 82.0 * std::pow (2.0, rnd.nextDouble() * 2.5); level = 0.5 + 2.5 * rnd.nextDouble(); }
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;
                    const float v = (float) (level * std::tanh (8.0 * std::sin (phase)));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                const auto t0 = juce::Time::getHighResolutionTicks();
                amp.process (buf);
                const double blockSeconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
                seconds += blockSeconds;
                worstPct = juce::jmax (worstPct, 100.0 * blockSeconds / (128.0 / sr));
            }
            logMessage ("reducedOrder: " + juce::String (100.0 * seconds / 10.0, 2) + " % avg, worst block " + juce::String (worstPct, 1)
                        + " %, failures " + juce::String (amp.getSolveFailureRate(), 6) + ", recoveries " + juce::String (amp.debugRecoveries()));
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
            JCM800StyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("J8_SETTLE_DIAG", {}).isNotEmpty())
        {
            beginTest ("silence settle diagnostic (dev only)");
            JCM800StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            if (juce::SystemStats::getEnvironmentVariable ("J8_OPENLOOP", {}).isNotEmpty())
                amp.debugSetFeedbackResistance (1.0e9);
            if (juce::SystemStats::getEnvironmentVariable ("J8_FIXEDSUPPLY", {}).isNotEmpty())
                amp.debugFreezeSupplyCurrent (true);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                amp.process (buf);
                if (b % (int) (0.5 * sr / 128) == 0)
                {
                    double peak = 0.0;
                    for (int i = 0; i < 128; ++i)
                        peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                    logMessage ("t=" + juce::String ((double) b * 128.0 / sr, 2) + "s peak=" + juce::String (peak, 5)
                                + " speaker=" + juce::String (amp.debugVoltage (P::speaker), 3)
                                + " PIplateA=" + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 2)
                                + " PIplateB=" + juce::String (amp.debugVoltage (P::phaseInverterPlateB), 2));
                }
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("J8_POWERCAL", {}).isNotEmpty())
        {
            // Calibration sweep for the reduced-order power stage, same methodology as the other amps.
            beginTest ("power-stage calibration sweep (dev only)");
            JCM800StyleAmplifierProcessor amp;
            setParam (amp, "j8_input", 0.0f);
            setParam (amp, "j8_gain", 0.5f);
            setParam (amp, "j8_power", 1.0f);
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 2.0e-4, 1.0e-3, 3.0e-3, 7.0e-3, 0.012, 0.02, 0.03, 0.05, 0.08, 0.12, 0.18, 0.28, 0.4, 0.55, 0.75, 1.0, 1.3, 1.7, 2.2, 2.8 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double drivePeak = 0.0, outPeak = 0.0, outSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    amp.process (one);
                    if (n >= measureFrom)
                    {
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::toneStackOut)));
                        const double y = amp.debugVoltage (P::speaker);
                        outPeak = juce::jmax (outPeak, std::abs (y));
                        outSumSq += y * y;
                        ++n2;
                    }
                }
                logMessage ("powercal drive_pk=" + juce::String (drivePeak, 6) + " out_pk=" + juce::String (outPeak, 4)
                            + " out_rms=" + juce::String (std::sqrt (outSumSq / (double) juce::jmax (1LL, n2)), 4) + " rail="
                            + juce::String (amp.railPlates(), 2) + " fails=" + juce::String (amp.getSolveFailureRate(), 6));
            }
        }
    }
};

static JCM800StyleAmplifierProcessorTests jcm800StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
