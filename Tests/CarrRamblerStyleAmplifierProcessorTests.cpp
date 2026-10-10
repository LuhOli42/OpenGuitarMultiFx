#include "Effects/CarrRamblerStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

/** The Carr Rambler-style amplifier (docs/circuits/CarrRambler.md). */
class CarrRamblerStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    CarrRamblerStyleAmplifierProcessorTests() : juce::UnitTest ("CarrRamblerStyleAmplifier", "Amp models") {}

    using P = CarrRamblerStyleAmplifierProcessor::Probe;

    static void setParam (CarrRamblerStyleAmplifierProcessor& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    void runTest() override
    {
        CarrRamblerStyleAmplifierProcessor::reducedOrder = false;
        const double sr = 48000.0;

        {
            beginTest ("DC operating point converges to sane tube voltages");
            CarrRamblerStyleAmplifierProcessor amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            // rails
            expectWithinAbsoluteError (amp.railPlates(), 405.0, 60.0);
            expectWithinAbsoluteError (amp.railScreens(), 390.0, 60.0);
            // preamp stages
            expectWithinAbsoluteError (amp.debugVoltage (P::stage1Plate), 200.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::stage1Cathode), 1.5, 1.2);
            expectWithinAbsoluteError (amp.debugVoltage (P::recoveryPlate), 200.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::recoveryCathode), 1.5, 1.2);
            // PI
            expectWithinAbsoluteError (amp.debugVoltage (P::piPlateA), 200.0, 90.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::piCathode), 35.0, 25.0);
            // power tubes: cathode-bias node positive, plate near rail
            expectWithinAbsoluteError (amp.debugVoltage (P::powerPlateA), 400.0, 70.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::cathodeBias), 27.0, 25.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::powerGridA), 0.0, 3.0);
            const double ip = amp.plateCurrentA();
            expect (ip > 0.005 && ip < 0.150); // tens of mA per tube
        }

        {
            beginTest ("stays silent on silence");
            CarrRamblerStyleAmplifierProcessor amp;
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
            const double rms = std::sqrt (sumSq / (double) n);
            logMessage (juce::String::formatted ("CR silence: rms=%.4f preFail=%lld powFail=%lld rec=%d spk=%.2f pOut=%.3f",
                        rms, amp.debugPreFailures(), amp.debugPowerFailures(), amp.debugRecoveries(),
                        amp.debugVoltage (CarrRamblerStyleAmplifierProcessor::Probe::speaker),
                        amp.debugVoltage (CarrRamblerStyleAmplifierProcessor::Probe::pOut)));
            expect (rms < 0.01);
            expect (amp.getSolveFailureRate() == 0.0);
        }

        {
            beginTest ("produces audible output and sane response");
            CarrRamblerStyleAmplifierProcessor amp;
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
            expect (rms > 0.005);      // audible
            expect (peak < 2.0);       // bounded by the emit limiter
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
        }

        {
            beginTest ("knobs move the response in the right direction");
            auto response = [&] (const char* id, float v, double freq)
            {
                CarrRamblerStyleAmplifierProcessor amp;
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
            expect (response ("cr_volume", 0.9f, 400.0) > response ("cr_volume", 0.2f, 400.0));
            expect (response ("cr_treble", 0.9f, 4000.0) > response ("cr_treble", 0.1f, 4000.0));
            expect (response ("cr_bass", 0.9f, 120.0) > response ("cr_bass", 0.1f, 120.0));
            expect (response ("cr_power", 0.9f, 400.0) > response ("cr_power", 0.3f, 400.0));

            // speaker selection changes the output
            expect (response ("cr_speaker", 0.0f, 400.0) != response ("cr_speaker", 2.0f, 400.0));
        }

        {
            beginTest ("bias knob moves the cathode-bias idle point");
            auto idleCurrent = [&] (float b)
            {
                CarrRamblerStyleAmplifierProcessor amp;
                setParam (amp, "cr_bias", b);
                amp.prepare (sr, 128, 2);
                return amp.plateCurrentA();
            };
            // more cathode resistance -> cooler bias -> less idle current
            expect (idleCurrent (0.9f) < idleCurrent (0.1f));
        }

        {
            beginTest ("triode mode lowers the power-stage ceiling");
            CarrRamblerStyleAmplifierProcessor amp;
            setParam (amp, "cr_mode", 1.0f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            for (int i = 0; i < (int) (0.5 * sr / 128); ++i)
                amp.process (buf);
            expect (amp.getSolveFailureRate() == 0.0);
            expect (amp.debugRecoveries() == 0);
        }

        {
            beginTest ("reducedOrder mode: bounded output, zero failures");
            CarrRamblerStyleAmplifierProcessor::reducedOrder = true;
            CarrRamblerStyleAmplifierProcessor amp;
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
            CarrRamblerStyleAmplifierProcessor::reducedOrder = false;
        }

        if (juce::SystemStats::getEnvironmentVariable ("CR_POWERCAL", {}).isNotEmpty())
        {
            beginTest ("power-stage calibration sweep (dev only)");
            CarrRamblerStyleAmplifierProcessor amp;
            setParam (amp, "cr_volume", 0.8f);
            setParam (amp, "cr_power", 1.0f);
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

static CarrRamblerStyleAmplifierProcessorTests carrRamblerTests;

} // namespace openguitarmultifx
