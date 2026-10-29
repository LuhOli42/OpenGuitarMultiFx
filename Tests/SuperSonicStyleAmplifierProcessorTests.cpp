#include "Effects/SuperSonicStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class SuperSonicStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    SuperSonicStyleAmplifierProcessorTests() : juce::UnitTest ("SuperSonicStyleAmplifier", "Effects") {}

    using Amp = SuperSonicStyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;

    static void setParam (Amp& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    /** RMS of the second half of `seconds` of a sine at `level` (both channels fed). */
    static double sineRms (Amp& amp, double level, double hz, double seconds)
    {
        juce::AudioBuffer<float> buf (2, 128);
        double sum = 0.0;
        long long n = 0, counted = 0;
        const int blocks = (int) (seconds * sr / 128);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i, ++n)
            {
                const float s = (float) (level * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) n / sr));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            amp.process (buf);
            if (b >= blocks / 2)
                for (int i = 0; i < 128; ++i, ++counted)
                    sum += juce::square ((double) buf.getSample (0, i));
        }
        return std::sqrt (sum / (double) juce::jmax (1LL, counted));
    }

    void runTest() override
    {
        Amp::reducedOrder = false;

        beginTest ("DC operating point against the drawing's printed voltages");
        {
            Amp amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("B+ " + juce::String (amp.railPlates(), 1) + ", Z " + juce::String (amp.railScreens(), 1) + ", Y "
                        + juce::String (amp.railPhaseInverter(), 1) + ", X " + juce::String (amp.railV2(), 1) + ", W "
                        + juce::String (amp.railV1(), 1) + "; V1A " + juce::String (amp.debugVoltage (P::v1aPlate), 1) + ", V1B "
                        + juce::String (amp.debugVoltage (P::v1bPlate), 1) + ", V2A " + juce::String (amp.debugVoltage (P::v2aPlate), 1)
                        + ", V2B " + juce::String (amp.debugVoltage (P::v2bPlate), 1) + ", V4B "
                        + juce::String (amp.debugVoltage (P::v4bPlate), 1) + ", PI tail " + juce::String (amp.debugVoltage (P::phaseInverterTail), 1)
                        + ", 6V6 plates " + juce::String (amp.debugVoltage (P::powerPlateA), 1) + ", plate current "
                        + juce::String (1000.0 * amp.plateCurrentTotal(), 1) + " mA");
            // printed: +420 / +418 / +376 / +317 / +280, plates 415, PI cathodes 70.3 V, 24 mA per 6V6 (24 mV on 1 ohm)
            expectWithinAbsoluteError (amp.railPlates(), 420.0, 15.0);
            expectWithinAbsoluteError (amp.railPhaseInverter(), 376.0, 20.0);
            expectWithinAbsoluteError (amp.railV1(), 280.0, 25.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::phaseInverterTail), 68.8, 3.0); // the cathodes sit ~1.5 V above T
            expectWithinAbsoluteError (1000.0 * amp.plateCurrentTotal(), 48.0, 20.0);
        }

        beginTest ("silence stays silent and the solver converges, on every channel / Fat setting");
        for (int burn = 0; burn < 2; ++burn)
            for (int fat = 0; fat < 2; ++fat)
            {
                Amp amp;
                setParam (amp, "ssc_channel", (float) burn);
                setParam (amp, "ssc_fat", (float) fat);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double peak = 0.0;
                for (int b = 0; b < (int) (2.0 * sr / 128); ++b)
                {
                    buf.clear();
                    amp.process (buf);
                    if (b > 40)
                        for (int i = 0; i < 128; ++i)
                            peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                }
                logMessage ("burn " + juce::String (burn) + " fat " + juce::String (fat) + ": silence peak " + juce::String (peak, 6)
                            + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
                expectLessThan (peak, 0.01);
                expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
            }

        beginTest ("a hot pluck on each setting: finite, bounded, no recoveries");
        for (int burn = 0; burn < 2; ++burn)
            for (int fat = 0; fat < 2; ++fat)
            {
                Amp amp;
                setParam (amp, "ssc_channel", (float) burn);
                setParam (amp, "ssc_fat", (float) fat);
                setParam (amp, "ssc_gain1", 1.0f);
                setParam (amp, "ssc_gain2", 1.0f);
                setParam (amp, "ssc_reverb", 0.7f);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double peak = 0.0, phase = 0.0, age = 0.0;
                bool finite = true;
                for (int b = 0; b < (int) (3.0 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        const float s = (float) (0.6 * std::exp (-age / 0.8) * std::sin (phase));
                        phase += 2.0 * juce::MathConstants<double>::pi * 98.0 / sr;
                        age += 1.0 / sr;
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    amp.process (buf);
                    for (int i = 0; i < 128; ++i)
                    {
                        finite = finite && std::isfinite (buf.getSample (0, i));
                        peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
                    }
                }
                logMessage ("burn " + juce::String (burn) + " fat " + juce::String (fat) + ": pluck peak " + juce::String (peak, 3)
                            + ", recoveries " + juce::String (amp.debugRecoveries()));
                expect (finite);
                expectLessThan (peak, 3.0);
                expectGreaterThan (peak, 0.01);
                expectEquals (amp.debugRecoveries(), 0);
            }

        beginTest ("the global feedback is negative: closing it lowers the gain");
        {
            const auto rmsAt = [&] (double ohms)
            {
                Amp amp;
                setParam (amp, "ssc_v_volume", 0.2f);
                setParam (amp, "ssc_reverb", 0.0f);
                amp.prepare (sr, 128, 2);
                amp.debugSetFeedbackResistance (ohms);
                return sineRms (amp, 0.005, 440.0, 1.0);
            };
            const double closed = rmsAt (820.0), open = rmsAt (1.0e12);
            logMessage ("NFB closed vs open: " + juce::String (20.0 * std::log10 (closed / juce::jmax (1.0e-12, open)), 2) + " dB");
            expectLessThan (closed, open * 0.9);
        }

        beginTest ("Burn saturates far harder than Vintage; Fat lifts Vintage's gain");
        {
            const auto rms = [&] (int burn, int fat, double level)
            {
                Amp amp;
                setParam (amp, "ssc_channel", (float) burn);
                setParam (amp, "ssc_fat", (float) fat);
                setParam (amp, "ssc_reverb", 0.0f);
                amp.prepare (sr, 128, 2);
                return sineRms (amp, level, 196.0, 0.6);
            };
            const double vN = rms (0, 0, 0.02), vF = rms (0, 1, 0.02), bLo = rms (1, 0, 0.02), bHi = rms (1, 0, 0.2);
            logMessage ("0.02 V in -> Vintage " + juce::String (vN, 4) + ", Vintage Fat " + juce::String (vF, 4) + ", Burn "
                        + juce::String (bLo, 4) + "; Burn at 0.2 V " + juce::String (bHi, 4));
            expectGreaterThan (vF, vN * 1.5);
            // Burn is compressed: 10x more input gives far less than 10x more output
            expectLessThan (bHi / bLo, 3.0);
        }

        beginTest ("Reverb adds a tail after the note stops");
        {
            const auto tail = [&] (float reverb)
            {
                Amp amp;
                setParam (amp, "ssc_reverb", reverb);
                setParam (amp, "ssc_v_volume", 0.4f);
                amp.prepare (sr, 128, 2);
                juce::AudioBuffer<float> buf (2, 128);
                double e = 0.0;
                long long n = 0;
                for (int b = 0; b < (int) (1.5 * sr / 128); ++b)
                {
                    for (int i = 0; i < 128; ++i, ++n)
                    {
                        const float s = n < (long long) (0.3 * sr) ? (float) (0.1 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) n / sr)) : 0.0f;
                        buf.setSample (0, i, s);
                        buf.setSample (1, i, s);
                    }
                    amp.process (buf);
                    if (n > (long long) (0.45 * sr))
                        for (int i = 0; i < 128; ++i)
                            e += juce::square ((double) buf.getSample (0, i));
                }
                return e;
            };
            const double dry = tail (0.0f), wet = tail (0.8f);
            logMessage ("tail energy: reverb 0 -> " + juce::String (dry, 8) + ", reverb 0.8 -> " + juce::String (wet, 8));
            expectGreaterThan (wet, dry * 10.0 + 1.0e-9);
        }

        Amp::reducedOrder = true;
        beginTest ("reducedOrder: a pluck is finite and audible; CPU is bounded");
        {
            Amp amp;
            setParam (amp, "ssc_channel", 1.0f);
            amp.prepare (sr, 128, 2);
            juce::AudioBuffer<float> buf (2, 128);
            double peak = 0.0, phase = 0.0;
            const auto start = juce::Time::getHighResolutionTicks();
            const int blocks = (int) (4.0 * sr / 128);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float s = (float) (0.3 * std::sin (phase));
                    phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, 0.9f * s); // not dual-mono: both channels solved
                }
                amp.process (buf);
                for (int i = 0; i < 128; ++i)
                    peak = juce::jmax (peak, (double) std::abs (buf.getSample (0, i)));
            }
            const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
            logMessage ("reducedOrder peak " + juce::String (peak, 3) + ", CPU " + juce::String (100.0 * seconds / 4.0, 1) + "% (stereo)");
            expect (std::isfinite (peak));
            expectGreaterThan (peak, 0.01);
            expectLessThan (peak, 3.0);
        }
        Amp::reducedOrder = false;
    }
};

static SuperSonicStyleAmplifierProcessorTests superSonicStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
