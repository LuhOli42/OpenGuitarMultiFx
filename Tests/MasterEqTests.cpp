#include "Engine/MasterEq.h"
#include "Engine/MasterBusSettings.h"

#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class MasterEqTests : public juce::UnitTest
{
public:
    MasterEqTests() : juce::UnitTest ("MasterEq", "Engine") {}

    static constexpr double sr = 48000.0;

    static MasterEqBand band (MasterEqBand::Type type, float freq, float gain = 0.0f, float q = 1.0f,
                              int slope = 12, bool enabled = true)
    {
        MasterEqBand b;
        b.type = type; b.freqHz = freq; b.gainDb = gain; b.q = q; b.slopeDbOct = slope; b.enabled = enabled;
        return b;
    }

    /** Steady-state amplitude response in dB at `freq` with `bands` live. */
    static double measured (const std::vector<MasterEqBand>& bands, double freq)
    {
        MasterEq eq;
        eq.prepare (sr, 1);
        eq.setBands (bands);
        const auto m = probeSine (eq, {}, freq, 0.1, sr, 0.5, 0.25);
        return 20.0 * std::log10 (juce::jmax (1.0e-6, m.out / 0.1));
    }

    void runTest() override
    {
        beginTest ("no bands (and all-disabled) is unity across the spectrum");
        for (double f : { 30.0, 100.0, 440.0, 1000.0, 5000.0, 15000.0 })
            expectWithinAbsoluteError (measured ({}, f), 0.0, 0.05);
        for (double f : { 100.0, 1000.0, 8000.0 })
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::peak, 800, 12, 1, 12, false) }, f), 0.0, 0.05);

        beginTest ("a peak band has its gain at its own frequency and decays away from it");
        {
            for (float gain : { -12.0f, 6.0f, 15.0f })
                expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::peak, 1000, gain, 1.0) }, 1000.0),
                                           (double) gain, 0.15);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::peak, 1000, 12, 2.0) }, 60.0), 0.0, 0.4);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::peak, 1000, 12, 2.0) }, 18000.0), 0.0, 0.4);
        }

        beginTest ("shelves hold their gain past the corner");
        {
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::lowShelf, 200, 12) }, 40.0), 12.0, 0.7);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::lowShelf, 200, 12) }, 200.0), 6.0, 0.7);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::highShelf, 4000, -10) }, 16000.0), -10.0, 0.8);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::highShelf, 4000, -10) }, 4000.0), -5.0, 0.8);
        }

        beginTest ("cuts: -3 dB at the corner, the stated slope away from it");
        {
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::lowCut, 200, 0, 1, 12) }, 200.0), -3.0, 0.4);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::highCut, 4000, 0, 1, 12) }, 4000.0), -3.0, 0.4);
            // One octave past the corner: ~12 dB for the 12 dB/oct band, ~24 dB for the 24 dB/oct one.
            const double cut12 = measured ({ band (MasterEqBand::Type::lowCut, 400, 0, 1, 12) }, 200.0);
            const double cut24 = measured ({ band (MasterEqBand::Type::lowCut, 400, 0, 1, 24) }, 200.0);
            logMessage ("low-cut one octave below corner: 12 dB/oct -> " + juce::String (cut12, 1)
                        + " dB, 24 dB/oct -> " + juce::String (cut24, 1) + " dB");
            expectWithinAbsoluteError (cut12, -12.0, 1.5);
            expectWithinAbsoluteError (cut24, -24.0, 2.0);
            // Well inside the passband: untouched.
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::lowCut, 200, 0, 1, 48) }, 8000.0), 0.0, 0.1);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::highCut, 8000, 0, 1, 48) }, 100.0), 0.0, 0.1);
        }

        beginTest ("a notch removes its own frequency and nothing else");
        {
            const double at = measured ({ band (MasterEqBand::Type::notch, 1000, 0, 4.0) }, 1000.0);
            logMessage ("notch at its frequency: " + juce::String (at, 1) + " dB");
            expectLessThan (at, -30.0);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::notch, 1000, 0, 4.0) }, 200.0), 0.0, 0.6);
            expectWithinAbsoluteError (measured ({ band (MasterEqBand::Type::notch, 1000, 0, 4.0) }, 10000.0), 0.0, 0.6);
        }

        beginTest ("bands stack: a low cut plus a mid cut plus a high boost");
        {
            const std::vector<MasterEqBand> bands {
                band (MasterEqBand::Type::lowCut, 120, 0, 1, 24),
                band (MasterEqBand::Type::peak, 900, -8, 2.0),
                band (MasterEqBand::Type::highShelf, 8000, 6) };
            expectLessThan (measured (bands, 40.0), -18.0);
            expectWithinAbsoluteError (measured (bands, 900.0), -8.0, 0.5);
            expectWithinAbsoluteError (measured (bands, 16000.0), 6.0, 1.0);
        }

        beginTest ("analytic magnitudeAt() agrees with what the filters actually do");
        for (double f : { 50.0, 200.0, 1000.0, 5000.0 })
        {
            const std::vector<MasterEqBand> bands { band (MasterEqBand::Type::lowShelf, 250, 9),
                                                    band (MasterEqBand::Type::peak, 2000, -6, 2.0) };
            const double analytic = 20.0 * std::log10 (juce::jmax (1.0e-6, MasterEq::magnitudeAt (bands, f, sr)));
            expectWithinAbsoluteError (measured (bands, f), analytic, 0.4);
        }

        beginTest ("extreme settings stay stable and finite (Q 18, +24 dB, 48 dB/oct cuts near Nyquist)");
        {
            MasterEq eq;
            eq.prepare (sr, 2);
            eq.setBands ({ band (MasterEqBand::Type::peak, 21000, 24, 18.0f),
                           band (MasterEqBand::Type::notch, 60, 0, 18.0f),
                           band (MasterEqBand::Type::lowCut, 1000, 0, 4.0f, 48),
                           band (MasterEqBand::Type::lowShelf, 100, 24, 2.0f),
                           band (MasterEqBand::Type::highShelf, 18000, -24, 2.0f) });
            juce::AudioBuffer<float> buf (2, 512);
            juce::Random rng (7);
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.1f * (rng.nextFloat() * 2.0f - 1.0f);
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                eq.process (buf);
                for (int i = 0; i < 512; ++i)
                    expect (std::isfinite (buf.getSample (0, i)) && std::abs (buf.getSample (0, i)) < 1.0e5f);
            }
        }

        beginTest ("the maxBands cap holds");
        {
            std::vector<MasterEqBand> tooMany;
            for (int i = 0; i < MasterEq::maxBands + 6; ++i)
                tooMany.push_back (band (MasterEqBand::Type::peak, 100.0f + 200.0f * i, 12));
            MasterEq eq;
            eq.prepare (sr, 1);
            eq.setBands (tooMany);
            expectEquals ((int) eq.getBands().size(), MasterEq::maxBands);
        }

        beginTest ("settings round-trip: save/load preserves every band field");
        {
            masterbus::State state;
            state.gainDb = -3.5f;
            state.bands = { band (MasterEqBand::Type::lowCut, 90, 0, 1, 24),
                            band (MasterEqBand::Type::peak, 750, 5.5f, 2.5f),
                            band (MasterEqBand::Type::highShelf, 9000, -4, 1.5f, 12, false) };
            masterbus::saveState (state);
            const auto loaded = masterbus::loadState();
            expectWithinAbsoluteError ((double) loaded.gainDb, -3.5, 0.01);
            expectEquals ((int) loaded.bands.size(), 3);
            if (loaded.bands.size() == 3)
            {
                expect (loaded.bands[0].type == MasterEqBand::Type::lowCut && loaded.bands[0].slopeDbOct == 24);
                expectWithinAbsoluteError ((double) loaded.bands[1].freqHz, 750.0, 0.1);
                expectWithinAbsoluteError ((double) loaded.bands[1].gainDb, 5.5, 0.01);
                expectWithinAbsoluteError ((double) loaded.bands[1].q, 2.5, 0.01);
                expect (! loaded.bands[2].enabled);
            }
            // Restore the pre-test state -- this file is the app's real settings on this box.
            masterbus::saveState ({});
        }
    }
};

static MasterEqTests masterEqTests;

} // namespace openguitarmultifx
