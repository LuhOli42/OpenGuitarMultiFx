#include "Effects/BassBigMuffStyleFuzzProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class BassBigMuffStyleFuzzProcessorTests : public juce::UnitTest
{
public:
    BassBigMuffStyleFuzzProcessorTests() : juce::UnitTest ("BassBigMuffStyleFuzzProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnob (BassBigMuffStyleFuzzProcessor& p, const char* id, float value)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && f->paramID == id)
                *f = value;
    }

    static double steadyStatePeakToPeak (BassBigMuffStyleFuzzProcessor& p, double freq, double amplitude, int blocks = 400)
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
        beginTest ("DC operating point -- all four stages forward-active, none pinned at a rail");
        {
            BassBigMuffStyleFuzzProcessor p;
            p.prepare (sr, 128, 1);
            expect (p.dcConverged(), "DC solve converged");
            for (int stage = 0; stage < 4; ++stage)
            {
                const double vc = p.debugCollector (stage);
                logMessage ("stage " + juce::String (stage) + " collector " + juce::String (vc, 3) + " V");
                expect (vc > 0.5 && vc < 8.5, "stage " + juce::String (stage) + " not pinned at a rail: "
                                              + juce::String (vc, 3) + " V");
            }
            expectWithinAbsoluteError (p.debugCollector (1), p.debugCollector (2), 0.25);
        }

        beginTest ("audible output through the full clip path");
        {
            BassBigMuffStyleFuzzProcessor p;
            p.prepare (sr, 128, 1);
            setKnob (p, "bbmp_sustain", 0.7f);
            setKnob (p, "bbmp_volume", 0.8f);
            const double pp = steadyStatePeakToPeak (p, 110.0, 0.1);
            logMessage ("110 Hz p-p at sustain .7: " + juce::String (pp, 3) + " V");
            expect (pp > 0.02 && std::isfinite (pp), "fuzz output is audible and finite");
            expect (p.getSolveFailureRate() < 0.01, "solver converges on a steady tone");
        }

        beginTest ("Sustain compresses like every Big Muff");
        {
            const auto dynamicRange = [&] (float sustain)
            {
                BassBigMuffStyleFuzzProcessor soft, loud;
                soft.prepare (sr, 128, 1);
                loud.prepare (sr, 128, 1);
                setKnob (soft, "bbmp_sustain", sustain);
                setKnob (loud, "bbmp_sustain", sustain);
                setKnob (soft, "bbmp_volume", 1.0f);
                setKnob (loud, "bbmp_volume", 1.0f);
                return steadyStatePeakToPeak (loud, 110.0, 0.20) / juce::jmax (1.0e-9, steadyStatePeakToPeak (soft, 110.0, 0.05));
            };
            const double clean = dynamicRange (0.0f), squashed = dynamicRange (1.0f);
            logMessage ("4x input change: " + juce::String (clean, 2) + "x at Sustain 0, "
                        + juce::String (squashed, 2) + "x at Sustain 1");
            expect (squashed < clean, "more Sustain compresses harder");
        }

        beginTest ("Bass Boost switch fattens the low end");
        {
            auto lowAt = [&] (float boost)
            {
                BassBigMuffStyleFuzzProcessor p;
                p.prepare (sr, 128, 1);
                setKnob (p, "bbmp_sustain", 0.4f);
                setKnob (p, "bbmp_volume", 0.8f);
                setKnob (p, "bbmp_bass_boost", boost);
                return steadyStatePeakToPeak (p, 55.0, 0.05);
            };
            const double flat = lowAt (0.0f), boosted = lowAt (1.0f);
            logMessage ("55 Hz p-p: flat " + juce::String (flat, 3) + " V, boosted " + juce::String (boosted, 3) + " V");
            expect (boosted > flat * 1.1, "bass boost raises the 55 Hz level: " + juce::String (flat, 3)
                                          + " -> " + juce::String (boosted, 3));
        }

        beginTest ("DRY switch: dry signal passes at constant level, Volume adds distortion");
        {
            BassBigMuffStyleFuzzProcessor p;
            p.prepare (sr, 128, 1);
            setKnob (p, "bbmp_dry", 1.0f);
            setKnob (p, "bbmp_volume", 0.0f);   // knob down: dry only, still audible
            const double dryOnly = steadyStatePeakToPeak (p, 110.0, 0.1);
            logMessage ("dry-only p-p at Volume 0: " + juce::String (dryOnly, 3) + " V");
            expect (dryOnly > 0.05, "the dry path is audible with Volume all the way down");

            BassBigMuffStyleFuzzProcessor off;
            off.prepare (sr, 128, 1);
            setKnob (off, "bbmp_dry", 0.0f);
            setKnob (off, "bbmp_volume", 0.0f);
            const double noDry = steadyStatePeakToPeak (off, 110.0, 0.1);
            expect (noDry < dryOnly * 0.5, "with DRY off and Volume 0 the output is much quieter");
        }
    }
};

static BassBigMuffStyleFuzzProcessorTests bassBigMuffStyleFuzzProcessorTests;

} // namespace openguitarmultifx
