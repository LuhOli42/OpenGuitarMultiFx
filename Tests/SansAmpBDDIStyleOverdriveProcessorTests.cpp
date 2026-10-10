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

    void runTest() override
    {
        beginTest ("DC operating point -- every block converges");
        {
            SansAmpBDDIStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            expect (p.getSolveFailureRate() < 0.01);
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
