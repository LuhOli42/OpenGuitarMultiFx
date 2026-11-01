#include "Effects/JimKelleyStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Jim Kelley Single Channel Reverb-style amplifier (docs/circuits/JimKelley.md). */
class JimKelleyStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    JimKelleyStyleAmplifierProcessorTests() : juce::UnitTest ("JimKelleyStyleAmplifier", "Amp models") {}

    using P = JimKelleyStyleAmplifierProcessor::Probe;

    static void setParam (JimKelleyStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        JimKelleyStyleAmplifierProcessor::reducedOrder = false;
        const double sr = 48000.0;

        {
            beginTest ("DC operating point converges to sane tube voltages");
            JimKelleyStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            // rails (marked ~477 V plates, ~375 V screens/PI node, ~245 V preamp)
            expectWithinAbsoluteError (amp.railPlates(), 477.0, 60.0);
            expectWithinAbsoluteError (amp.railScreens(), 420.0, 80.0);
            // preamp stages
            expectWithinAbsoluteError (amp.debugVoltage (P::stage1Plate), 130.0, 80.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::stage1Cathode), 1.4, 1.2);
            expectWithinAbsoluteError (amp.debugVoltage (P::clipPlate), 160.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::clipCathode), 1.6, 1.4);
            expectWithinAbsoluteError (amp.debugVoltage (P::recPlate), 216.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::recCathode), 2.0, 1.6);
            // mixer + cathodyne: marked ~209 V plate, ~265 V PI plate, ~110 V PI cathode
            expectWithinAbsoluteError (amp.debugVoltage (P::mixPlate), 209.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::piPlate), 265.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::piCathode), 110.0, 60.0);
            // power tubes: fixed bias ~-50 V, plate near rail
            expectWithinAbsoluteError (amp.debugVoltage (P::biasNode), -50.0, 20.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerPlateA), 470.0, 70.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerGridA), -50.0, 20.0);
            const double ip = amp.plateCurrentA();
            logMessage (juce::String::formatted ("JK dc: rail=%.1f scr=%.1f s1=%.1f/%.2f clip=%.1f/%.2f rec=%.1f/%.2f mix=%.1f pi=%.1f/%.1f bias=%.1f pp=%.1f g=%.1f ip=%.4f",
                        amp.railPlates(), amp.railScreens(),
                        amp.debugVoltage (P::stage1Plate), amp.debugVoltage (P::stage1Cathode),
                        amp.debugVoltage (P::clipPlate), amp.debugVoltage (P::clipCathode),
                        amp.debugVoltage (P::recPlate), amp.debugVoltage (P::recCathode),
                        amp.debugVoltage (P::mixPlate), amp.debugVoltage (P::piPlate), amp.debugVoltage (P::piCathode),
                        amp.debugVoltage (P::biasNode), amp.debugVoltage (P::powerPlateA),
                        amp.debugVoltage (P::powerGridA), ip));
            // The schematic marks -50 V at the bias rail; at that grid voltage the Koren 6V6 sits near
            // cutoff (~2 mA for the pair). That is the amp's real character -- Jim Kelley's design keeps
            // the output stage cold and lets the power tubes clip when driven, unlike a Fender's warm idle.
            expect (ip > 0.0005 && ip < 0.150); // converged, positive, pair-sized
        }

        {
            beginTest ("stays silent on silence");
            JimKelleyStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            for (int i = 0; i < (int) (1.5 * sr / 128); ++i)
            {
                buf.clear();
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
            const double rms = std::sqrt (sumSq / (double) n);
            logMessage (juce::String::formatted ("JK silence: rms=%.4f preFail=%lld powFail=%lld rec=%d spk=%.2f pOut=%.3f",
                        rms, amp.debugPreFailures(), amp.debugPowerFailures(), amp.debugRecoveries(),
                        amp.debugVoltage (P::speaker), amp.debugVoltage (P::pOut)));
            expect (rms < 0.01);
            expect (amp.getSolveFailureRate() == 0.0);
        }

        {
            beginTest ("produces audible output and sane response");
            JimKelleyStyleAmplifierProcessor amp;
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
            logMessage ("audible: rms=" + juce::String (rms, 4) + " peak=" + juce::String (peak, 3)
                        + " fail=" + juce::String (amp.getSolveFailureRate(), 6)
                        + " rec=" + juce::String (amp.debugRecoveries()));
            expect (rms > 0.005);
            expect (peak < 2.0);
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
        }

        {
            beginTest ("knobs move the response in the right direction");
            auto response = [&] (const char* id, float v, double freq)
            {
                JimKelleyStyleAmplifierProcessor amp;
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
            expect (response ("jk_gain", 0.9f, 400.0) > response ("jk_gain", 0.2f, 400.0));
            expect (response ("jk_volume", 0.9f, 400.0) > response ("jk_volume", 0.2f, 400.0));
            expect (response ("jk_treble", 0.9f, 4000.0) > response ("jk_treble", 0.1f, 4000.0));
            expect (response ("jk_bass", 0.9f, 120.0) > response ("jk_bass", 0.1f, 120.0));
            expect (response ("jk_power", 0.9f, 400.0) > response ("jk_power", 0.3f, 400.0));

            // pull switches + speaker selection move the output
            expect (response ("jk_bright", 1.0f, 4000.0) != response ("jk_bright", 0.0f, 4000.0));
            expect (response ("jk_midboost", 1.0f, 1000.0) != response ("jk_midboost", 0.0f, 1000.0));
            expect (response ("jk_speaker", 0.0f, 400.0) != response ("jk_speaker", 2.0f, 400.0));
        }

        {
            beginTest ("reverb knob raises the wet level");
            auto level = [&] (float rv)
            {
                JimKelleyStyleAmplifierProcessor amp;
                setParam (amp, "jk_reverb", rv);
                amp.prepare (sr, 128, 2);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                juce::AudioBuffer<float> buf (2, 128);
                double sumSq = 0.0;
                long long n = 0;
                for (int i = 0; i < (int) (0.8 * sr / 128); ++i)
                {
                    for (int s = 0; s < 128; ++s)
                        buf.setSample (0, s, (float) (0.05 * std::sin (twoPi * 440.0 * (i * 128 + s) / sr)));
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf);
                    if (i > (int) (0.4 * sr / 128))
                        for (int s = 0; s < 128; ++s)
                        {
                            const double v = buf.getSample (0, s);
                            sumSq += v * v;
                            ++n;
                        }
                }
                return std::sqrt (sumSq / (double) n);
            };
            expect (level (0.8f) > level (0.0f));
        }

        {
            beginTest ("power select 30W lowers the output ceiling");
            auto level = [&] (float sel)
            {
                JimKelleyStyleAmplifierProcessor amp;
                setParam (amp, "jk_power_sel", sel);
                setParam (amp, "jk_gain", 0.9f);
                amp.prepare (sr, 128, 2);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                juce::AudioBuffer<float> buf (2, 128);
                double sumSq = 0.0;
                long long n = 0;
                for (int i = 0; i < (int) (0.8 * sr / 128); ++i)
                {
                    for (int s = 0; s < 128; ++s)
                        buf.setSample (0, s, (float) (0.1 * std::sin (twoPi * 220.0 * (i * 128 + s) / sr)));
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf);
                    if (i > (int) (0.4 * sr / 128))
                        for (int s = 0; s < 128; ++s)
                        {
                            const double v = buf.getSample (0, s);
                            sumSq += v * v;
                            ++n;
                        }
                }
                return std::sqrt (sumSq / (double) n);
            };
            expect (level (1.0f) < level (0.0f));
        }

        {
            beginTest ("bias knob moves the fixed-bias idle point");
            auto idleCurrent = [&] (float b)
            {
                JimKelleyStyleAmplifierProcessor amp;
                setParam (amp, "jk_bias", b);
                amp.prepare (sr, 128, 2);
                return amp.plateCurrentA();
            };
            for (float b : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                logMessage ("bias=" + juce::String (b, 2) + " ip=" + juce::String (idleCurrent (b) * 1000.0, 2) + " mA");
            // colder bias (more negative grid) -> less idle current
            expect (idleCurrent (0.9f) < idleCurrent (0.1f));
        }

        {
            beginTest ("reducedOrder mode: bounded output, zero failures");
            JimKelleyStyleAmplifierProcessor::reducedOrder = true;
            JimKelleyStyleAmplifierProcessor amp;
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
            JimKelleyStyleAmplifierProcessor::reducedOrder = false;
        }

        {
            // User report 2026-10-31: crackles at full Gain. The two series 1N914s on V1b's grid had a floating node
            // between them; Newton failed there on hot plucks and the channel recovered ~8 times a second.
            beginTest ("Gain at max, hard plucks and chords: zero solver failures and recoveries (both paths)");
            for (bool reduced : { true, false })
            {
                JimKelleyStyleAmplifierProcessor::reducedOrder = reduced;
                JimKelleyStyleAmplifierProcessor amp;
                for (auto* p : amp.getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == "jk_gain")
                            *f = 1.0f;
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                const double twoPi = 2.0 * juce::MathConstants<double>::pi;
                long long n = 0;
                for (int b = 0; b < (int) (6.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        const double t = (double) n / sr, lt = std::fmod (t, 0.25);
                        double x = 0.0;
                        for (int k = 1; k <= 8; ++k)
                            x += std::sin (twoPi * 98.0 * k * lt) / k;
                        buf.setSample (0, i, (float) (0.6 * std::exp (-lt / 0.12) * x));
                    }
                    buf.copyFrom (1, 0, buf, 0, 0, 128);
                    amp.process (buf);
                }
                logMessage (juce::String (reduced ? "reduced" : "full") + ": failures " + juce::String (amp.getSolveFailureRate(), 6)
                            + ", recoveries " + juce::String (amp.debugRecoveries()));
                expect (amp.getSolveFailureRate() == 0.0);
                expectEquals (amp.debugRecoveries(), 0);
            }
            JimKelleyStyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("JK_POWERCAL", {}).isNotEmpty())
        {
            beginTest ("power-stage calibration sweep (dev only)");
            JimKelleyStyleAmplifierProcessor amp;
            setParam (amp, "jk_volume", 0.8f);
            setParam (amp, "jk_power", 1.0f);
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

static JimKelleyStyleAmplifierProcessorTests jimKelleyTests;

} // namespace openguitarmultifx
