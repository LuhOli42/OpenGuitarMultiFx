#include "Effects/DividedBy13FTR37StyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Divided by 13 FTR 37-style amplifier (docs/circuits/DividedBy13FTR37.md). */
class DividedBy13FTR37StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    DividedBy13FTR37StyleAmplifierProcessorTests() : juce::UnitTest ("DividedBy13FTR37StyleAmplifier", "Amp models") {}

    using P = DividedBy13FTR37StyleAmplifierProcessor::Probe;

    static void setParam (DividedBy13FTR37StyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        DividedBy13FTR37StyleAmplifierProcessor::reducedOrder = false;
        const double sr = 48000.0;

        {
            beginTest ("DC operating point converges to sane tube voltages");
            DividedBy13FTR37StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            logMessage ("dcOk " + juce::String (amp.dcConverged() ? 1 : 0)
                        + " rail " + juce::String (amp.railPlates(), 1) + "/" + juce::String (amp.railScreens(), 1)
                        + " ch1p " + juce::String (amp.debugVoltage (P::ch1Plate), 1)
                        + " ch1k2 " + juce::String (amp.debugVoltage (P::ch1K2), 2)
                        + " ch2p1 " + juce::String (amp.debugVoltage (P::ch2Plate1), 1)
                        + " ch2p2 " + juce::String (amp.debugVoltage (P::ch2Plate2), 1)
                        + " mix " + juce::String (amp.debugVoltage (P::mixNode), 2)
                        + " piA " + juce::String (amp.debugVoltage (P::piPlateA), 1)
                        + " piK " + juce::String (amp.debugVoltage (P::piCathode), 1)
                        + " ppA " + juce::String (amp.debugVoltage (P::powerPlateA), 1)
                        + " gA " + juce::String (amp.debugVoltage (P::powerGridA), 1)
                        + " bias " + juce::String (amp.debugVoltage (P::biasNode), 1)
                        + " ip " + juce::String (amp.plateCurrentTotal(), 4));
            expect (amp.dcConverged());
            expectWithinAbsoluteError (amp.railPlates(), 410.0, 60.0);
            expectWithinAbsoluteError (amp.railScreens(), 395.0, 60.0);
            // Channel 1: 5879 stage plate + second 12AX7 cathode
            expectWithinAbsoluteError (amp.debugVoltage (P::ch1Plate), 210.0, 100.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::ch1K2), 1.5, 1.2);
            // Channel 2 stages
            expectWithinAbsoluteError (amp.debugVoltage (P::ch2Plate1), 200.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::ch2Plate2), 200.0, 90.0);
            // PI + power tubes
            expectWithinAbsoluteError (amp.debugVoltage (P::piPlateA), 210.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::piCathode), 40.0, 25.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerPlateA), 405.0, 70.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerGridA), -32.0, 10.0);
            const double ip = amp.plateCurrentTotal();
            expect (ip > 0.020 && ip < 0.300); // four 6V6s near the manual's 25 mA each
        }

        {
            beginTest ("stays silent on silence");
            DividedBy13FTR37StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            for (int i = 0; i < (int) (1.5 * sr / 128); ++i)
            {
                buf.clear(); // process() is in-place: previous output must not become the next input
                amp.process (buf);
            }
            double sumSq = 0.0;
            long long n = 0;
            for (int i = 0; i < (int) (1.5 * sr / 128); ++i)
            {
                buf.clear();
                amp.process (buf);
                for (int s = 0; s < 128; ++s)
                {
                    const double v = buf.getSample (0, s);
                    sumSq += v * v;
                    ++n;
                }
            }
            expect (std::sqrt (sumSq / (double) n) < 0.01);
            expect (amp.getSolveFailureRate() == 0.0);
        }

        {
            beginTest ("produces audible output and sane response");
            DividedBy13FTR37StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            juce::AudioBuffer<float> buf (2, 128);
            double sumSq = 0.0, peak = 0.0;
            long long n = 0;
            for (int i = 0; i < (int) (1.0 * sr / 128); ++i)
            {
                for (int s = 0; s < 128; ++s)
                    buf.setSample (0, s, (float) (0.05 * std::sin (twoPi * 220.0 * (i * 128 + s) / sr)));
                buf.copyFrom (1, 0, buf, 0, 0, 128);
                amp.process (buf);
                if (i > (int) (0.5 * sr / 128))
                    for (int s = 0; s < 128; ++s)
                    {
                        const double v = buf.getSample (0, s);
                        sumSq += v * v;
                        peak = juce::jmax (peak, std::abs (v));
                        ++n;
                    }
            }
            const double rms = std::sqrt (sumSq / (double) n);
            logMessage ("FTR37 audible: rms=" + juce::String (rms, 4) + " peak=" + juce::String (peak, 3));
            expect (rms > 0.005);
            expect (peak < 2.0);
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
        }

        {
            beginTest ("knobs move the response in the right direction");
            auto response = [&] (const char* id, float v, double freq)
            {
                DividedBy13FTR37StyleAmplifierProcessor amp;
                setParam (amp, id, v);
                amp.prepare (sr, 128, 2);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                juce::AudioBuffer<float> buf (2, 128);
                double sumSq = 0.0;
                long long n = 0;
                for (int i = 0; i < (int) (0.8 * sr / 128); ++i)
                {
                    for (int s = 0; s < 128; ++s)
                        buf.setSample (0, s, (float) (0.05 * std::sin (twoPi * freq * (i * 128 + s) / sr)));
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf);
                    if (i > (int) (0.4 * sr / 128))
                        for (int s = 0; s < 128; ++s)
                        {
                            const double v2 = buf.getSample (0, s);
                            sumSq += v2 * v2;
                            ++n;
                        }
                }
                return std::sqrt (sumSq / (double) n);
            };
            expect (response ("f37_ch2_volume", 0.9f, 400.0) > response ("f37_ch2_volume", 0.2f, 400.0));
            expect (response ("f37_treble", 0.9f, 4000.0) > response ("f37_treble", 0.1f, 4000.0));
            expect (response ("f37_bass", 0.9f, 120.0) > response ("f37_bass", 0.1f, 120.0));
            expect (response ("f37_power", 0.9f, 400.0) > response ("f37_power", 0.3f, 400.0));
            expect (response ("f37_speaker", 0.0f, 400.0) != response ("f37_speaker", 2.0f, 400.0));

            // Ch1 path responds when selected
            auto ch1Response = [&] (float click)
            {
                DividedBy13FTR37StyleAmplifierProcessor amp;
                setParam (amp, "f37_input", 0.0f);
                setParam (amp, "f37_click", click);
                setParam (amp, "f37_ch1_volume", 0.8f);
                amp.prepare (sr, 128, 2);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                juce::AudioBuffer<float> buf (2, 128);
                double sumSq = 0.0;
                long long n = 0;
                for (int i = 0; i < (int) (0.8 * sr / 128); ++i)
                {
                    for (int s = 0; s < 128; ++s)
                        buf.setSample (0, s, (float) (0.05 * std::sin (twoPi * 6000.0 * (i * 128 + s) / sr)));
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf);
                    if (i > (int) (0.4 * sr / 128))
                        for (int s = 0; s < 128; ++s)
                        {
                            const double v2 = buf.getSample (0, s);
                            sumSq += v2 * v2;
                            ++n;
                        }
                }
                return std::sqrt (sumSq / (double) n);
            };
            // The click switch's highest position shunts the most top end.
            expect (ch1Response (0.0f) > ch1Response (5.0f));
        }

        {
            beginTest ("Full/Half switch changes the 6V6 idle current");
            auto idle = [&] (float half)
            {
                DividedBy13FTR37StyleAmplifierProcessor amp;
                setParam (amp, "f37_half_power", half);
                amp.prepare (sr, 128, 2);
                return amp.plateCurrentTotal();
            };
            const double full = idle (0.0f), half = idle (1.0f);
            expect (half < 0.75 * full);
            expect (half > 0.25 * full);
        }

        {
            beginTest ("bias knob moves the fixed-bias idle current");
            auto idle = [&] (float b)
            {
                DividedBy13FTR37StyleAmplifierProcessor amp;
                setParam (amp, "f37_bias", b);
                amp.prepare (sr, 128, 2);
                return amp.plateCurrentTotal();
            };
            expect (idle (0.9f) > idle (0.1f)); // hotter bias -> more idle current
        }

        {
            beginTest ("reducedOrder mode: bounded output, zero failures");
            DividedBy13FTR37StyleAmplifierProcessor::reducedOrder = true;
            DividedBy13FTR37StyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            juce::AudioBuffer<float> buf (2, 128);
            double seconds = 0.0, worstPct = 0.0;
            for (int i = 0; i < (int) (10.0 * sr / 128); ++i)
            {
                for (int s = 0; s < 128; ++s)
                    buf.setSample (0, s, (float) (0.1 * std::sin (twoPi * 220.0 * (i * 128 + s) / sr)));
                buf.copyFrom (1, 0, buf, 0, 0, 128);
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
            DividedBy13FTR37StyleAmplifierProcessor::reducedOrder = false;
        }

        {
            beginTest ("Full/Half switch lowers the reduced-order power ceiling");
            DividedBy13FTR37StyleAmplifierProcessor::reducedOrder = true;
            const double twoPi = 2.0 * juce::MathConstants<double>::pi;
            auto level = [&] (float half)
            {
                DividedBy13FTR37StyleAmplifierProcessor amp;
                setParam (amp, "f37_half_power", half);
                setParam (amp, "f37_ch2_volume", 0.9f);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double sum = 0.0;
                int n = 0;
                for (int i = 0; i < (int) (2.0 * sr / 128); ++i)
                {
                    for (int s = 0; s < 128; ++s)
                        buf.setSample (0, s, (float) (0.5 * std::sin (twoPi * 220.0 * (i * 128 + s) / sr)));
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf); // in-place: buf now holds the output
                    if (i * 128 >= (int) sr)
                        for (int s = 0; s < 128; ++s) { const double v = buf.getSample (0, s); sum += v * v; ++n; }
                }
                return std::sqrt (sum / juce::jmax (1, n));
            };
            expect (level (1.0f) < level (0.0f));
            DividedBy13FTR37StyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("F37_POWERCAL", {}).isNotEmpty())
        {
            beginTest ("power-stage calibration sweep (dev only)");
            DividedBy13FTR37StyleAmplifierProcessor amp;
            setParam (amp, "f37_input", 1.0f);
            setParam (amp, "f37_ch2_volume", 0.8f);
            setParam (amp, "f37_power", 1.0f);
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
                        drivePeak = juce::jmax (drivePeak, std::abs (amp.debugVoltage (P::pOut)));
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

static DividedBy13FTR37StyleAmplifierProcessorTests ftr37Tests;

} // namespace openguitarmultifx
