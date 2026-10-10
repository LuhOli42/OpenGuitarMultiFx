#include "Effects/BossOC2StyleOctaverProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class BossOC2StyleOctaverProcessorTests : public juce::UnitTest
{
public:
    BossOC2StyleOctaverProcessorTests() : juce::UnitTest ("BossOC2StyleOctaverProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    static void setKnob (BossOC2StyleOctaverProcessor& p, const char* id, float value)
    {
        for (auto* par : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (par); f != nullptr && f->paramID == id)
                *f = value;
    }

    /** Zero-crossing frequency estimate over the tail of a long render. The output is passed through a
        one-pole LP at `measureHz` first, so the chopper's upper products (3f/2 etc.) don't confound the
        count -- this is what a sub-octave sounds like, not what the raw harmonic mix looks like. */
    static double renderedFundamental (BossOC2StyleOctaverProcessor& p, double inFreq, double amplitude,
                                       double& outPeak, double measureHz)
    {
        juce::AudioBuffer<float> buf (1, 256);
        const double lp = 1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * measureHz / sr);
        double lpZ = 0.0, phase = 0.0, prev = 0.0, pk = 0.0;
        int crossings = 0;
        constexpr int blocks = 400, measureBlocks = 60;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                phase += 2.0 * juce::MathConstants<double>::pi * inFreq / sr;
                buf.setSample (0, i, (float) (amplitude * std::sin (phase)));
            }
            p.process (buf);
            const bool measuring = b >= blocks - measureBlocks;
            for (int i = 0; i < 256; ++i)
            {
                const double v = buf.getSample (0, i);
                lpZ += lp * (v - lpZ);
                if (measuring)
                {
                    pk = juce::jmax (pk, std::abs (v));
                    if (lpZ * prev < 0.0)
                        ++crossings;
                    prev = lpZ;
                }
            }
        }
        outPeak = pk;
        const double secs = measureBlocks * 256.0 / sr;
        return (crossings * 0.5) / secs;
    }

    void runTest() override
    {
        beginTest ("Direct only: the input fundamental passes through");
        {
            BossOC2StyleOctaverProcessor p;
            p.prepare (sr, 256, 1);
            setKnob (p, "oc2_direct", 1.0f);
            setKnob (p, "oc2_oct1", 0.0f);
            setKnob (p, "oc2_oct2", 0.0f);
            double pk = 0.0;
            const double f = renderedFundamental (p, 220.0, 0.1, pk, 400.0);
            logMessage ("direct: f0 estimate " + juce::String (f, 1) + " Hz, peak " + juce::String (pk, 3));
            expect (pk > 0.02, "audible output");
            expectWithinAbsoluteError (f, 220.0, 30.0);
        }

        beginTest ("Octave 1 only: the output sits an octave below the input");
        {
            BossOC2StyleOctaverProcessor p;
            p.prepare (sr, 256, 1);
            setKnob (p, "oc2_direct", 0.0f);
            setKnob (p, "oc2_oct1", 1.0f);
            setKnob (p, "oc2_oct2", 0.0f);
            double pk = 0.0;
            const double f = renderedFundamental (p, 220.0, 0.1, pk, 165.0);
            logMessage ("oct1: f0 estimate " + juce::String (f, 1) + " Hz, peak " + juce::String (pk, 3));
            expect (pk > 0.01, "the sub-octave is audible");
            expectWithinAbsoluteError (f, 110.0, 25.0);
        }

        beginTest ("Octave 2 only: two octaves below");
        {
            BossOC2StyleOctaverProcessor p;
            p.prepare (sr, 256, 1);
            setKnob (p, "oc2_direct", 0.0f);
            setKnob (p, "oc2_oct1", 0.0f);
            setKnob (p, "oc2_oct2", 1.0f);
            double pk = 0.0;
            const double f = renderedFundamental (p, 220.0, 0.1, pk, 85.0);
            logMessage ("oct2: f0 estimate " + juce::String (f, 1) + " Hz, peak " + juce::String (pk, 3));
            expect (pk > 0.005, "the -2 octave path is audible");
            expectWithinAbsoluteError (f, 55.0, 20.0);
        }

        beginTest ("bass register: a 100 Hz note produces a 50 Hz sub-octave");
        {
            BossOC2StyleOctaverProcessor p;
            p.prepare (sr, 256, 1);
            setKnob (p, "oc2_direct", 0.0f);
            setKnob (p, "oc2_oct1", 1.0f);
            setKnob (p, "oc2_oct2", 0.0f);
            double pk = 0.0;
            const double f = renderedFundamental (p, 100.0, 0.1, pk, 80.0);
            logMessage ("100 Hz in: f0 estimate " + juce::String (f, 1) + " Hz, peak " + juce::String (pk, 3));
            expectWithinAbsoluteError (f, 50.0, 20.0);
        }
    }
};

static BossOC2StyleOctaverProcessorTests bossOC2StyleOctaverProcessorTests;

} // namespace openguitarmultifx
