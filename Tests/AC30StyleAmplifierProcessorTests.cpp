#include "EffectRegistry.h"
#include "Effects/AC30StyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Vox AC30-style amplifier, Top Boost channel (docs/circuits/AC30TopBoost.md). */
class AC30StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    AC30StyleAmplifierProcessorTests() : juce::UnitTest ("AC30StyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = AC30StyleAmplifierProcessor::Probe;

    static void setParam (AC30StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        AC30StyleAmplifierProcessor::reducedOrder = false;

        beginTest ("the model converges to a sane DC operating point");
        {
            AC30StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", preamp rail " + juce::String (amp.railPreamp(), 1)
                        + ", preamp plate " + juce::String (amp.debugVoltage (P::preampPlate), 1)
                        + ", follower out " + juce::String (amp.debugVoltage (P::followerOut), 1)
                        + ", tone out " + juce::String (amp.debugVoltage (P::toneOut), 1)
                        + ", PI plate A/B " + juce::String (amp.debugVoltage (P::piPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::piPlateB), 1)
                        + ", power plate A/B " + juce::String (amp.debugVoltage (P::powerPlateA), 1) + " / " + juce::String (amp.debugVoltage (P::powerPlateB), 1)
                        + ", cathode bias " + juce::String (amp.debugVoltage (P::cathodeBias), 1)
                        + ", plate current total " + juce::String (amp.plateCurrentTotal() * 1000.0, 1) + " mA");
            expect (amp.railPlates() > 340.0 && amp.railPlates() < 410.0);
            expect (amp.debugVoltage (P::cathodeBias) > 3.0 && amp.debugVoltage (P::cathodeBias) < 30.0,
                    "a plausible class A cathode-bias voltage for the shared 4xEL84 resistor");
            expect (std::abs (amp.debugVoltage (P::piPlateA) - amp.debugVoltage (P::piPlateB)) < 30.0,
                    "the LTP's two plates should sit reasonably close together at idle (no signal)");
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            // 10 s, not 2: standing lesson from the JTM45/JCM800 investigations -- and this is a genuinely new
            // topology at THIS scale (4 EL84s as two parallel pairs, a real LTP, no NFB).
            AC30StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            double peak = 0.0;
            for (int b = 0; b < (int) (10.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            }
            logMessage ("10 s silence-settle peak " + juce::String (peak, 6) + ", failure rate " + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note through each Input (High/Low) setting: finite, bounded, converges");
        for (float inputChoice : { 0.0f, 1.0f })
        {
            AC30StyleAmplifierProcessor amp;
            setParam (amp, "a30_input", inputChoice);
            setParam (amp, "a30_volume", 0.6f);
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

        {
            beginTest ("reducedOrder power stage tracks the reference (level, and zero failures by construction)");
            AC30StyleAmplifierProcessor ref;
            setParam (ref, "a30_input", 0.0f);
            setParam (ref, "a30_volume", 0.8f);
            setParam (ref, "a30_power", 1.0f);
            ref.prepare (sr, 128, 2);
            AC30StyleAmplifierProcessor red;
            setParam (red, "a30_input", 0.0f);
            setParam (red, "a30_volume", 0.8f);
            setParam (red, "a30_power", 1.0f);
            AC30StyleAmplifierProcessor::reducedOrder = true;
            red.prepare (sr, 128, 2);
            AC30StyleAmplifierProcessor::reducedOrder = false;

            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 5.0e-3, 0.02, 0.05, 0.1, 0.2, 0.4, 0.6 })
            {
                const long long total = (long long) (1.5 * sr);
                const long long measureFrom = total - (long long) (0.1 * sr);
                double refOutSumSq = 0.0, redOutSumSq = 0.0;
                long long n2 = 0;
                juce::AudioBuffer<float> one (2, 1);
                AC30StyleAmplifierProcessor::reducedOrder = false;
                for (long long n = 0; n < total; ++n)
                {
                    const float v = (float) (level * std::sin (twoPi * 100.0 * (double) n / sr));
                    one.setSample (0, 0, v);
                    one.setSample (1, 0, v);
                    ref.process (one);
                    if (n >= measureFrom) { const double y = ref.debugVoltage (P::speaker); refOutSumSq += y * y; ++n2; }
                }
                n2 = 0;
                AC30StyleAmplifierProcessor::reducedOrder = true;
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
            AC30StyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud (the shipped default has no physical speaker to mismatch)");
            AC30StyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                AC30StyleAmplifierProcessor amp;
                setParam (amp, "a30_input", 0.0f);
                setParam (amp, "a30_volume", 0.8f);
                setParam (amp, "a30_speaker", speaker);
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
            AC30StyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: worst-block cost under a hot-pedal stress");
            AC30StyleAmplifierProcessor::reducedOrder = true;
            AC30StyleAmplifierProcessor amp;
            setParam (amp, "a30_input", 0.0f);
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
            AC30StyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("A30_SETTLE_DIAG", {}).isNotEmpty())
        {
            beginTest ("silence settle diagnostic (dev only)");
            AC30StyleAmplifierProcessor amp;
            if (juce::SystemStats::getEnvironmentVariable ("A30_FIXEDSUPPLY", {}).isNotEmpty())
                amp.debugFreezeSupplyCurrent (true);
            amp.prepare (sr, 128, 2);
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
                                + " plateA=" + juce::String (amp.debugVoltage (P::powerPlateA), 2)
                                + " plateB=" + juce::String (amp.debugVoltage (P::powerPlateB), 2)
                                + " cathodeBias=" + juce::String (amp.debugVoltage (P::cathodeBias), 3));
                }
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("A30_POWERCAL", {}).isNotEmpty())
        {
            beginTest ("power-stage calibration sweep (dev only)");
            AC30StyleAmplifierProcessor amp;
            setParam (amp, "a30_input", 0.0f);
            setParam (amp, "a30_volume", 0.8f);
            setParam (amp, "a30_power", 1.0f);
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            for (double level : { 2.0e-4, 1.0e-3, 3.0e-3, 7.0e-3, 0.012, 0.02, 0.03, 0.05, 0.08, 0.12, 0.18, 0.28, 0.4, 0.55, 0.75, 1.0, 1.5, 2.5, 4.0, 6.0, 9.0 })
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
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::followerOut)));
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

static AC30StyleAmplifierProcessorTests ac30StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
