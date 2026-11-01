#include "Effects/DumbleODSStyleAmplifierProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class DumbleODSStyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    DumbleODSStyleAmplifierProcessorTests() : juce::UnitTest ("DumbleODSStyleAmplifier", "Effects") {}

    using Amp = DumbleODSStyleAmplifierProcessor;
    using P = Amp::Probe;
    static constexpr double sr = 48000.0;

    static void setParam (Amp& amp, const char* id, float v)
    {
        for (auto* p : amp.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                if (f->paramID == id)
                    *f = v;
    }

    /** RMS of the second half of `seconds` of a sine at `level`. */
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

    /** A decaying 98 Hz pluck: returns the peak, flags non-finite output. */
    static double pluckPeak (Amp& amp, double level, bool& finite)
    {
        juce::AudioBuffer<float> buf (2, 128);
        double peak = 0.0, phase = 0.0, age = 0.0;
        finite = true;
        for (int b = 0; b < (int) (2.5 * sr / 128); ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float s = (float) (level * std::exp (-age / 0.8) * std::sin (phase));
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
        return peak;
    }

    void runTest() override
    {
        Amp::reducedOrder = false;

        beginTest ("DC operating point against the 70s trace's printed rails");
        {
            Amp amp;
            amp.prepare (sr, 128, 2);
            expect (amp.dcConverged());
            logMessage ("A " + juce::String (amp.railPlates(), 1) + ", B " + juce::String (amp.railScreens(), 1) + ", C "
                        + juce::String (amp.railPi(), 1) + ", D " + juce::String (amp.railOverdrive(), 1) + ", E "
                        + juce::String (amp.railV1(), 1) + "; V1A " + juce::String (amp.debugVoltage (P::v1aPlate), 1)
                        + ", V1B " + juce::String (amp.debugVoltage (P::v1bPlate), 1) + ", V2A "
                        + juce::String (amp.debugVoltage (P::v2aPlate), 1) + ", V2B " + juce::String (amp.debugVoltage (P::v2bPlate), 1)
                        + "; PI plates " + juce::String (amp.debugVoltage (P::phaseInverterPlateA), 1) + " / "
                        + juce::String (amp.debugVoltage (P::phaseInverterPlateB), 1) + ", tail "
                        + juce::String (amp.debugVoltage (P::phaseInverterTail), 1) + ", grid A " + juce::String (amp.debugVoltage (P::phaseInverterGrid), 2) + ", N " + juce::String (amp.debugVoltage (P::feedbackNode), 2) + "; bias "
                        + juce::String (amp.debugVoltage (P::biasNode), 1) + " V, 6L6 plates "
                        + juce::String (amp.debugVoltage (P::powerPlateA), 1) + ", plate current "
                        + juce::String (1000.0 * amp.plateCurrentTotal(), 1) + " mA");
            // printed: A 460, B 454, C 436, D 306, E 303; PI plates 308 / 303; 457 V on the 6L6 plates and screens
            expectWithinAbsoluteError (amp.railPlates(), 460.0, 12.0);
            expectWithinAbsoluteError (amp.railScreens(), 454.0, 12.0);
            expectWithinAbsoluteError (amp.railPi(), 436.0, 15.0);
            // D and E sit ~40 V high: the printed 22K drop needs ~5.9 mA, and four 12AX7s on 100K / 1.5K draw ~4.5
            // (see the doc). The rails are kept as drawn rather than loaded to match.
            expectWithinAbsoluteError (amp.railOverdrive(), 306.0, 45.0);
            expectWithinAbsoluteError (amp.railV1(), 303.0, 45.0);
            expectWithinAbsoluteError (amp.debugVoltage (P::phaseInverterPlateA), 305.0, 25.0);
            expectWithinAbsoluteError (1000.0 * amp.plateCurrentTotal(), 70.0, 25.0);
        }

        beginTest ("silence stays silent on both channels");
        for (int od = 0; od < 2; ++od)
        {
            Amp amp;
            setParam (amp, "ods_channel", (float) od);
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
            logMessage ("channel " + juce::String (od) + ": silence peak " + juce::String (peak, 6) + ", failures "
                        + juce::String (amp.getSolveFailureRate(), 6));
            expectLessThan (peak, 0.01);
            expectLessThan (amp.getSolveFailureRate(), 1.0e-6);
        }

        beginTest ("a hot pluck on every channel / voicing: finite, bounded, no recoveries");
        for (int od = 0; od < 2; ++od)
            for (int voicing = 0; voicing < 3; ++voicing)
            {
                Amp amp;
                setParam (amp, "ods_channel", (float) od);
                setParam (amp, "ods_voicing", (float) voicing);
                setParam (amp, "ods_volume", 0.8f);
                setParam (amp, "ods_overdrive", 1.0f);
                setParam (amp, "ods_master", 0.8f);
                amp.prepare (sr, 128, 2);
                bool finite = true;
                const double peak = pluckPeak (amp, 0.6, finite);
                logMessage ("channel " + juce::String (od) + " voicing " + juce::String (voicing) + ": peak "
                            + juce::String (peak, 3) + ", recoveries " + juce::String (amp.debugRecoveries())
                            + ", failures " + juce::String (amp.getSolveFailureRate(), 6));
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
                setParam (amp, "ods_volume", 0.2f);
                amp.prepare (sr, 128, 2);
                amp.debugSetFeedbackResistance (ohms);
                return sineRms (amp, 0.005, 440.0, 1.0);
            };
            const double closed = rmsAt (3.9e3), open = rmsAt (1.0e12);
            logMessage ("NFB closed vs open: " + juce::String (20.0 * std::log10 (closed / juce::jmax (1.0e-12, open)), 2) + " dB");
            expectLessThan (closed, open * 0.9);
        }

        beginTest ("Overdrive adds gain and compression; the Overdrive pot sets how much");
        {
            const auto rms = [&] (int od, float drive, double level)
            {
                Amp amp;
                setParam (amp, "ods_channel", (float) od);
                setParam (amp, "ods_overdrive", drive);
                amp.prepare (sr, 128, 2);
                return sineRms (amp, level, 196.0, 0.6);
            };
            const double nLo = rms (0, 0.5f, 0.01), oLo = rms (1, 0.5f, 0.01), oHi = rms (1, 0.5f, 0.1);
            const double oLowDrive = rms (1, 0.1f, 0.01);
            logMessage ("0.01 V in -> Normal " + juce::String (nLo, 4) + ", Overdrive " + juce::String (oLo, 4)
                        + " (drive 0.1: " + juce::String (oLowDrive, 4) + "); Overdrive at 0.1 V " + juce::String (oHi, 4));
            expectGreaterThan (oLo, oLowDrive * 1.5);
            expectLessThan (oHi / oLo, 5.0); // 10x the input, far less than 10x the output
        }

        beginTest ("the switches: PAB is the loudest, Mid Boost lifts the mids, Jazz is quieter than Rock");
        {
            const auto rms = [&] (int voicing, bool midBoost, double hz)
            {
                Amp amp;
                setParam (amp, "ods_voicing", (float) voicing);
                setParam (amp, "ods_mid_boost", midBoost ? 1.0f : 0.0f);
                setParam (amp, "ods_volume", 0.3f);
                amp.prepare (sr, 128, 2);
                return sineRms (amp, 0.005, hz, 0.6);
            };
            const double rock = rms (2, false, 700.0), jazz = rms (0, false, 700.0), pab = rms (1, false, 700.0);
            const double boosted = rms (2, true, 700.0);
            logMessage ("700 Hz: Rock " + juce::String (rock, 5) + ", Jazz " + juce::String (jazz, 5) + ", PAB "
                        + juce::String (pab, 5) + ", Rock + Mid Boost " + juce::String (boosted, 5));
            expectGreaterThan (pab, rock * 1.5);
            expectLessThan (jazz, rock);
            expectGreaterThan (boosted, rock * 1.2);
        }

        beginTest ("at the defaults Overdrive sits a few dB over Normal, not tens");
        {
            const auto rms = [&] (int od)
            {
                Amp amp;
                setParam (amp, "ods_channel", (float) od);
                amp.prepare (sr, 128, 2);
                return sineRms (amp, 0.2, 196.0, 0.6);
            };
            const double normal = rms (0), overdrive = rms (1);
            const double db = 20.0 * std::log10 (overdrive / normal);
            logMessage ("0.2 V: Normal " + juce::String (normal, 4) + ", Overdrive " + juce::String (overdrive, 4) + " ("
                        + juce::String (db, 1) + " dB)");
            expectGreaterThan (db, 3.0);
            expectLessThan (db, 15.0);
        }

        Amp::reducedOrder = true;
        beginTest ("reducedOrder: a pluck on both channels is finite and audible");
        for (int od = 0; od < 2; ++od)
        {
            Amp amp;
            setParam (amp, "ods_channel", (float) od);
            amp.prepare (sr, 128, 2);
            bool finite = true;
            const double peak = pluckPeak (amp, 0.3, finite);
            logMessage ("reducedOrder channel " + juce::String (od) + ": peak " + juce::String (peak, 3));
            expect (finite);
            expectGreaterThan (peak, 0.01);
            expectLessThan (peak, 3.0);
            expectEquals (amp.debugRecoveries(), 0);
        }
        Amp::reducedOrder = false;
    }
};

static DumbleODSStyleAmplifierProcessorTests dumbleODSStyleAmplifierProcessorTests;

} // namespace openguitarmultifx
