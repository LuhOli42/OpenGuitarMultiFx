#include "EffectRegistry.h"
#include "Effects/DeluxeReverbStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Deluxe Reverb-style amplifier (Fender AB763, ~22 W; docs/circuits/DeluxeReverbAB763.md). */
class DeluxeReverbStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    DeluxeReverbStyleAmplifierProcessorTests() : juce::UnitTest ("DeluxeReverbStyleAmplifier", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = DeluxeReverbStyleAmplifierProcessor::Probe;

    static void setParam (DeluxeReverbStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        DeluxeReverbStyleAmplifierProcessor::reducedOrder = false;

        beginTest ("operating points match the schematic (its voltages are printed, +-20%)");
        {
            DeluxeReverbStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            const auto near = [this] (double v, double target, const char* what)
            {
                expect (std::abs (v - target) < 0.2 * std::abs (target), juce::String (what) + " " + juce::String (v) + " V, schematic " + juce::String (target));
            };
            near (amp.railPlates(), 415.0, "plate rail");
            near (amp.railScreens(), 415.0, "screen rail");
            near (amp.railPhaseInverter(), 325.0, "PI tail-bias rail");
            // The model's two channels share an identical triode/resistor network, so they land on the same DC point;
            // the schematic's printed 180/170 V split is real-world component tolerance the model can't reproduce.
            // 175 V (their average) is the fair target for both.
            near (amp.debugVoltage (P::channelNormalPlate), 175.0, "Normal channel plate (avg of printed 180/170)");
            near (amp.debugVoltage (P::channelVibratoPlate), 175.0, "Vibrato channel plate (avg of printed 180/170)");
            logMessage ("plate rail " + juce::String (amp.railPlates(), 1) + ", screen " + juce::String (amp.railScreens(), 1)
                        + ", PI rail " + juce::String (amp.railPhaseInverter(), 1)
                        + ", Normal plate " + juce::String (amp.debugVoltage (P::channelNormalPlate), 1)
                        + ", Vibrato plate " + juce::String (amp.debugVoltage (P::channelVibratoPlate), 1)
                        + ", PI plate A " + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 1)
                        + ", bias node " + juce::String (amp.debugVoltage (P::biasNode), 1));
        }

        beginTest ("the model settles: no drift and no failures with silence at the input");
        {
            DeluxeReverbStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int b = 0; b < (int) (2.0 * sr / 128); ++b)
            {
                buf.clear(); // silence in every block: without it the previous output is fed back in as input
                amp.process (buf);
            }
            double peak = 0.0;
            for (int i = 0; i < 128; ++i)
                peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a plucked note through each Input setting: finite, bounded, converges");
        for (float inputChoice : { 0.0f, 1.0f, 2.0f })
        {
            DeluxeReverbStyleAmplifierProcessor amp;
            setParam (amp, "dr_input", inputChoice);
            setParam (amp, "dr_volume", 0.6f);
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
            DeluxeReverbStyleAmplifierProcessor amp;
            int page2Count = 0;
            bool foundSpeaker = false;
            const auto pages = amp.getParameterPages();
            for (auto* f : pages[1])
            {
                ++page2Count;
                if (f->paramID == "dr_speaker")
                    foundSpeaker = true;
            }
            expect (page2Count == 5, "Power Drive, Bias, Tube Feel, Speaker, Output"); // getParameterPages() includes the group's own trunk param
            expect (foundSpeaker);
        }

        {
            beginTest ("reducedOrder power stage tracks the reference (level, and zero failures by construction)");
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = false;
            DeluxeReverbStyleAmplifierProcessor ref;
            setParam (ref, "dr_input", 1.0f);
            setParam (ref, "dr_volume", 0.8f);
            setParam (ref, "dr_power", 1.0f);
            ref.prepare (sr, 128, 2);
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = true;
            DeluxeReverbStyleAmplifierProcessor red;
            setParam (red, "dr_input", 1.0f);
            setParam (red, "dr_volume", 0.8f);
            setParam (red, "dr_power", 1.0f);
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
                expectLessThan (std::abs (dB), 1.5);
                expectLessThan (red.getSolveFailureRate(), 1.0e-6);
                expect (red.debugRecoveries() == 0);
            }
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: 4 / 8 / 16 ohm sound equally loud (the shipped default has no physical speaker to mismatch)");
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = true;
            const auto rmsAt = [] (float speaker)
            {
                DeluxeReverbStyleAmplifierProcessor amp;
                setParam (amp, "dr_input", 1.0f);
                setParam (amp, "dr_volume", 0.8f);
                setParam (amp, "dr_speaker", speaker);
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
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("reducedOrder: worst-block cost under a hot-pedal stress");
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = true;
            DeluxeReverbStyleAmplifierProcessor amp;
            setParam (amp, "dr_input", 1.0f);
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
            DeluxeReverbStyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("DR_SETTLE_DIAG", {}).isNotEmpty())
        {
            beginTest ("silence settle diagnostic (dev only)");
            DeluxeReverbStyleAmplifierProcessor amp;
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
                                + " speakerV=" + juce::String (amp.debugVoltage (P::speaker), 3)
                                + " PIplateA=" + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 2)
                                + " PIplateB=" + juce::String (amp.debugVoltage (P::phaseInverterPlateB), 2)
                                + " PIgridA=" + juce::String (amp.debugVoltage (P::phaseInverterGrid), 3)
                                + " biasNode=" + juce::String (amp.debugVoltage (P::biasNode), 3));
                }
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("DR_POWERCAL", {}).isNotEmpty())
        {
            // Calibration sweep for the reduced-order power stage, same methodology as the Super Lead/Bassman/Twin Reverb.
            beginTest ("power-stage calibration sweep (dev only)");
            DeluxeReverbStyleAmplifierProcessor amp;
            setParam (amp, "dr_input", 1.0f);
            setParam (amp, "dr_volume", 0.8f);
            setParam (amp, "dr_power", 1.0f);
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
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::mixNode)));
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

static DeluxeReverbStyleAmplifierProcessorTests deluxeReverbStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
