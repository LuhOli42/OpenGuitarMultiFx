#include "Effects/ParametricEQProcessor.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class ParametricEQProcessorTests : public juce::UnitTest
{
public:
    ParametricEQProcessorTests() : juce::UnitTest ("ParametricEQProcessor", "Effects") {}

    static constexpr double sr = 48000.0;

    // order: Low gain, Low freq, LM gain, LM freq, LM Q, Level, HM gain, HM freq, HM Q, High gain, High freq
    static void set (ParametricEQProcessor& p, std::initializer_list<float> v) { setParams (p, v); }

    static double measured (std::initializer_list<float> knobs, double freq)
    {
        ParametricEQProcessor p;
        p.prepare (sr, 512, 1);
        set (p, knobs);
        const auto m = probeSine (p, {}, freq, 0.1, sr, 0.5, 0.25);
        return 20.0 * std::log10 (m.out / 0.1);
    }

    void runTest() override
    {
        beginTest ("all knobs flat: unity across the spectrum");
        for (double f : { 30.0, 100.0, 440.0, 1000.0, 4000.0, 12000.0 })
            expectWithinAbsoluteError (measured ({ 0, 100, 0, 400, 1, 0, 0, 2500, 1, 0, 6000 }, f), 0.0, 0.05);

        beginTest ("a peaking band has exactly its gain at its own frequency, at any Q");
        for (float gain : { -12.0f, 6.0f, 15.0f })
            for (float q : { 0.5f, 1.0f, 4.0f })
            {
                const double g = measured ({ 0, 100, gain, 800, q, 0, 0, 2500, 1, 0, 6000 }, 800.0);
                expectWithinAbsoluteError (g, (double) gain, 0.1);
            }

        beginTest ("the high-mid band, both shelves and Level do what they say");
        {
            expectWithinAbsoluteError (measured ({ 0, 100, 0, 400, 1, 0, 9, 3000, 2, 0, 6000 }, 3000.0), 9.0, 0.1);
            expectWithinAbsoluteError (measured ({ 12, 200, 0, 400, 1, 0, 0, 2500, 1, 0, 6000 }, 40.0), 12.0, 0.6);   // low shelf well below its corner
            expectWithinAbsoluteError (measured ({ 12, 200, 0, 400, 1, 0, 0, 2500, 1, 0, 6000 }, 200.0), 6.0, 0.6);   // half the gain at the corner
            expectWithinAbsoluteError (measured ({ 0, 100, 0, 400, 1, 0, 0, 2500, 1, -10, 4000 }, 16000.0), -10.0, 0.7);
            expectWithinAbsoluteError (measured ({ 0, 100, 0, 400, 1, 6, 0, 2500, 1, 0, 6000 }, 1000.0), 6.0, 0.05);
        }

        beginTest ("a narrower Q means a narrower bell: the gain one octave away is smaller");
        {
            const double wide = measured ({ 0, 100, 12, 1000, 0.5f, 0, 0, 2500, 1, 0, 6000 }, 2000.0);
            const double narrow = measured ({ 0, 100, 12, 1000, 4.0f, 0, 0, 2500, 1, 0, 6000 }, 2000.0);
            logMessage ("one octave above a +12 dB bell: Q 0.5 -> " + juce::String (wide, 2) + " dB, Q 4 -> " + juce::String (narrow, 2) + " dB");
            expectGreaterThan (wide, narrow + 3.0);
        }

        beginTest ("extreme settings stay stable and finite (frequency near Nyquist, Q 8, +15 dB everywhere)");
        {
            ParametricEQProcessor p;
            p.prepare (sr, 512, 2);
            set (p, { 15, 500, 15, 2500, 8, 15, 15, 9000, 8, 15, 14000 });
            juce::AudioBuffer<float> buf (2, 512);
            juce::Random rng (5);
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                for (int i = 0; i < 512; ++i)
                    expect (std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 100.0f);
            }
        }
    }
};

static ParametricEQProcessorTests parametricEQProcessorTests;

} // namespace openguitarmultifx
