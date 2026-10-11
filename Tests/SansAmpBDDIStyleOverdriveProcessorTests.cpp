#include "Effects/SansAmpBDDIStyleOverdriveProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class SansAmpBDDIStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    SansAmpBDDIStyleOverdriveProcessorTests() : juce::UnitTest ("SansAmpBDDIStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnob (SansAmpBDDIStyleOverdriveProcessor& p, const char* id, float value)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && f->paramID == id)
                *f = value;
    }

    static double steadyStatePeakToPeak (SansAmpBDDIStyleOverdriveProcessor& p, double freq, double amplitude, int blocks = 400)
    {
        juce::AudioBuffer<float> buf (1, 128);
        double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 128; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                buf.setSample (0, i, (float) (amplitude * std::sin (phase)));
            }
            p.process (buf);
            if (b == blocks - 1)
                for (int i = 0; i < 128; ++i)
                {
                    lo = juce::jmin (lo, (double) buf.getSample (0, i));
                    hi = juce::jmax (hi, (double) buf.getSample (0, i));
                }
        }
        return hi - lo;
    }

    // THD of the steady-state output: fundamental amplitude by correlation over a whole-cycle window,
    // harmonics as the leftover RMS -- same definition the audit's sine probe reports.
    static double steadyStateThd (SansAmpBDDIStyleOverdriveProcessor& p, double freq, double amplitude)
    {
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        const long long total = (long long) (0.6 * sr);
        const double cycles = std::floor (0.2 * freq);
        const long long len = (long long) std::llround (cycles * sr / freq);
        const long long start = total - len;
        std::vector<float> y;
        y.reserve ((size_t) len);
        juce::AudioBuffer<float> buf (1, 1);
        double phase = 0.0;
        for (long long n = 0; n < total; ++n)
        {
            phase += twoPi * freq / sr;
            buf.setSample (0, 0, (float) (amplitude * std::sin (phase)));
            p.process (buf);
            if (n >= start)
                y.push_back (buf.getSample (0, 0));
        }
        double s = 0.0, c = 0.0, rms = 0.0;
        for (long long i = 0; i < len; ++i)
        {
            const double ph = twoPi * freq * i / sr;
            s += y[i] * std::sin (ph);
            c += y[i] * std::cos (ph);
            rms += y[i] * y[i];
        }
        const double fundRms = std::sqrt (2.0 * (s * s + c * c)) / len;
        rms = std::sqrt (rms / len);
        return std::sqrt (juce::jmax (0.0, rms * rms - fundRms * fundRms)) / juce::jmax (1.0e-9, fundRms);
    }

    void runTest() override
    {
        beginTest ("DC operating point -- every block converges");
        {
            SansAmpBDDIStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            expect (p.getSolveFailureRate() < 0.01);
        }

        beginTest ("the 72 Hz HPF doesn't load the notch's low band");
        {
            // The twin-T's output impedance is ~10k, so the HPF after it must sit at a much higher impedance:
            // a kOhm-scale load pulls the whole low band down with it (a ~30 dB loss at 110 Hz once did).
            using Pr = SansAmpBDDIStyleOverdriveProcessor::Probe;
            auto stagePp = [&] (Pr probe)
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                double phase = 0.0, lo = 1.0e9, hi = -1.0e9;
                juce::AudioBuffer<float> warm (1, 128);
                for (int b = 0; b < 300; ++b)
                {
                    for (int i = 0; i < 128; ++i)
                    {
                        phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                        warm.setSample (0, i, (float) (0.1 * std::sin (phase)));
                    }
                    p.process (warm);
                }
                juce::AudioBuffer<float> buf (1, 1);
                for (int i = 0; i < 4400; ++i) // ~10 cycles of 110 Hz
                {
                    phase += 2.0 * juce::MathConstants<double>::pi * 110.0 / sr;
                    buf.setSample (0, 0, (float) (0.1 * std::sin (phase)));
                    p.process (buf);
                    const double v = p.debugVoltage (probe);
                    lo = juce::jmin (lo, v);
                    hi = juce::jmax (hi, v);
                }
                return hi - lo;
            };
            const double buf = stagePp (Pr::buf), notch = stagePp (Pr::notch);
            logMessage ("110 Hz p-p at the buffer " + juce::String (buf, 3) + " V, after notch+HPF " + juce::String (notch, 3) + " V");
            expectGreaterThan (notch, buf * 0.5);
        }

        beginTest ("audible output");
        {
            SansAmpBDDIStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            const double pp = steadyStatePeakToPeak (p, 110.0, 0.1);
            logMessage ("110 Hz p-p: " + juce::String (pp, 3) + " V");
            expect (pp > 0.02 && std::isfinite (pp), "output is audible and finite");
        }

        beginTest ("Blend 0 keeps only Bass/Treble (dry is flat-ish), Blend 1 adds the emulation");
        {
            const double dry = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 0.0f);
                setKnob (p, "bddi_level", 0.8f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            const double wet = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 1.0f);
                setKnob (p, "bddi_drive", 0.8f);
                setKnob (p, "bddi_level", 0.8f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            logMessage ("blend 0 p-p " + juce::String (dry, 3) + " V, blend 1 " + juce::String (wet, 3) + " V");
            expect (dry > 0.05 && wet > 0.05, "both legs carry signal");
        }

        beginTest ("the 750 Hz notch scoops mids on the wet path");
        {
            const double mid = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 1.0f);
                setKnob (p, "bddi_drive", 0.0f);
                setKnob (p, "bddi_level", 1.0f);
                return steadyStatePeakToPeak (p, 750.0, 0.05);
            }();
            const double low = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 1.0f);
                setKnob (p, "bddi_drive", 0.0f);
                setKnob (p, "bddi_level", 1.0f);
                return steadyStatePeakToPeak (p, 100.0, 0.05);
            }();
            logMessage ("wet path p-p: 750 Hz " + juce::String (mid, 3) + " V vs 100 Hz " + juce::String (low, 3) + " V");
            expect (mid < low * 0.8, "the notch dips the mids relative to the bass: " + juce::String (mid, 3)
                                     + " vs " + juce::String (low, 3));
        }

        beginTest ("Drive sweeps clean to growl -- THD rises with the knob (the audit's dead-Drive regression)");
        {
            const auto thdAt = [&] (float drive)
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 1.0f);
                setKnob (p, "bddi_drive", drive);
                return steadyStateThd (p, 220.0, 0.2);
            };
            const double clean = thdAt (0.1f), growl = thdAt (0.9f);
            logMessage ("wet-path THD at 220 Hz: drive 0.1 = " + juce::String (100.0 * clean, 2)
                        + "%, drive 0.9 = " + juce::String (100.0 * growl, 2) + "%");
            expect (growl > clean * 3.0 && growl > 0.03,
                    "Drive must add harmonic content, not sit transparent");
        }

        beginTest ("Bass band shelves the low end");
        {
            const double up = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 0.0f);
                setKnob (p, "bddi_bass", 1.0f);
                setKnob (p, "bddi_level", 0.8f);
                return steadyStatePeakToPeak (p, 60.0, 0.05);
            }();
            const double down = [&]
            {
                SansAmpBDDIStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bddi_blend", 0.0f);
                setKnob (p, "bddi_bass", 0.0f);
                setKnob (p, "bddi_level", 0.8f);
                return steadyStatePeakToPeak (p, 60.0, 0.05);
            }();
            expect (up > down * 1.2, "Bass band shelves: " + juce::String (up, 3) + " vs " + juce::String (down, 3));
        }
    }
};

static SansAmpBDDIStyleOverdriveProcessorTests sansAmpBDDIStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
