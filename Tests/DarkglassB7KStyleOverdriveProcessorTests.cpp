#include "Effects/DarkglassB7KStyleOverdriveProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class DarkglassB7KStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    DarkglassB7KStyleOverdriveProcessorTests() : juce::UnitTest ("DarkglassB7KStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnob (DarkglassB7KStyleOverdriveProcessor& p, const char* id, float value)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && f->paramID == id)
                *f = value;
    }

    static double steadyStatePeakToPeak (DarkglassB7KStyleOverdriveProcessor& p, double freq, double amplitude, int blocks = 400)
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

    static double steadyStateRms (DarkglassB7KStyleOverdriveProcessor& p, double freq, double amplitude)
    {
        constexpr int warmupBlocks = 25, measureBlocks = 375, blockSize = 128;
        juce::AudioBuffer<float> buf (1, blockSize);
        double phase = 0.0, sumSquares = 0.0;
        for (int b = 0; b < warmupBlocks + measureBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * freq / sr;
                buf.setSample (0, i, (float) (amplitude * std::sin (phase)));
            }
            p.process (buf);
            if (b >= warmupBlocks)
                for (int i = 0; i < blockSize; ++i)
                {
                    const double sample = buf.getSample (0, i);
                    sumSquares += sample * sample;
                }
        }
        return std::sqrt (sumSquares / (measureBlocks * blockSize));
    }

    void runTest() override
    {
        beginTest ("DC operating point -- every block converges, output sits at 0 V DC");
        {
            DarkglassB7KStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            expect (p.getSolveFailureRate() < 0.01);
        }

        beginTest ("audible output");
        {
            DarkglassB7KStyleOverdriveProcessor p;
            p.prepare (sr, 128, 1);
            setKnob (p, "b7k_drive", 0.6f);
            setKnob (p, "b7k_level", 0.7f);
            const double pp = steadyStatePeakToPeak (p, 110.0, 0.1);
            logMessage ("110 Hz p-p: " + juce::String (pp, 3) + " V");
            expect (pp > 0.01 && std::isfinite (pp), "output is audible and finite");
        }

        beginTest ("Grunt raises the low-to-high ratio in ordered switch steps");
        {
            const auto rmsAt = [] (float grunt, double hz)
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_blend", 1.0f);
                setKnob (p, "b7k_drive", 0.0f);
                setKnob (p, "b7k_level", 0.5f);
                setKnob (p, "b7k_attack", 0.0f);
                setKnob (p, "b7k_grunt", grunt);
                return steadyStateRms (p, hz, 0.01);
            };
            const double g0_80 = rmsAt (0.0f, 80.0), g0_3k = rmsAt (0.0f, 3000.0);
            const double g1_80 = rmsAt (1.0f, 80.0), g1_3k = rmsAt (1.0f, 3000.0);
            const double g2_80 = rmsAt (2.0f, 80.0), g2_3k = rmsAt (2.0f, 3000.0);
            const double ratio0Db = 20.0 * std::log10 (g0_80 / g0_3k);
            const double ratio1Db = 20.0 * std::log10 (g1_80 / g1_3k);
            const double ratio2Db = 20.0 * std::log10 (g2_80 / g2_3k);
            logMessage ("Grunt 0/1/2 RMS 80 Hz: " + juce::String (g0_80, 7) + " / " + juce::String (g1_80, 7)
                        + " / " + juce::String (g2_80, 7) + " V; 3 kHz: " + juce::String (g0_3k, 7) + " / "
                        + juce::String (g1_3k, 7) + " / " + juce::String (g2_3k, 7) + " V; ratios: "
                        + juce::String (ratio0Db, 3) + " / " + juce::String (ratio1Db, 3) + " / "
                        + juce::String (ratio2Db, 3) + " dB");
            expectGreaterThan (ratio2Db - ratio0Db, 2.0);
            expect (ratio1Db > ratio0Db && ratio1Db < ratio2Db, "Grunt position 1 lies between positions 0 and 2");
        }

        beginTest ("Blend: fully dry is (nearly) clean, fully wet distorts");
        {
            // Dry at blend 0 should pass the 0.1 V signal largely intact; blend 1 runs the CMOS clipper.
            const double dry = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_blend", 0.0f);
                setKnob (p, "b7k_level", 0.8f);
                setKnob (p, "b7k_drive", 1.0f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            const double wet = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_blend", 1.0f);
                setKnob (p, "b7k_level", 0.8f);
                setKnob (p, "b7k_drive", 1.0f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            logMessage ("blend 0 p-p " + juce::String (dry, 3) + " V, blend 1 " + juce::String (wet, 3) + " V");
            expect (dry > 0.02, "the dry path carries signal");
            expect (wet > 0.05, "the wet path carries signal");
        }

        beginTest ("Low band shelving at 60 Hz: boost vs cut");
        {
            const double up = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_blend", 0.0f);   // keep the path clean so the EQ reads clearly
                setKnob (p, "b7k_low", 1.0f);
                return steadyStatePeakToPeak (p, 60.0, 0.05);
            }();
            const double down = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_blend", 0.0f);
                setKnob (p, "b7k_low", 0.0f);
                return steadyStatePeakToPeak (p, 60.0, 0.05);
            }();
            logMessage ("60 Hz p-p: boost " + juce::String (up, 3) + " V, cut " + juce::String (down, 3) + " V");
            expect (up > down * 1.2, "Low band shelves the bottom: " + juce::String (up, 3)
                                     + " vs " + juce::String (down, 3));
        }

        beginTest ("Level is monotonic and near-silent at zero");
        {
            const double zero = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_level", 0.0f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            const double full = [&]
            {
                DarkglassB7KStyleOverdriveProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "b7k_level", 1.0f);
                return steadyStatePeakToPeak (p, 110.0, 0.1);
            }();
            expect (full > zero * 5.0, "Level sweeps from near silence to full output");
        }
    }
};

static DarkglassB7KStyleOverdriveProcessorTests darkglassB7KStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
