#include "Effects/TraceElliotGP12StyleAmplifierProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class TraceElliotGP12StyleAmplifierProcessorTests : public juce::UnitTest
{
public:
    TraceElliotGP12StyleAmplifierProcessorTests() : juce::UnitTest ("TraceElliotGP12StyleAmplifierProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = TraceElliotGP12StyleAmplifierProcessor;

    /** Sets the page-1 params + the 12 band sliders (page 2 is the subgroup, reached via getParameters(true)). */
    static double gainDb (float shape, float loComp, float bal, float hiComp, float gLevelDb,
                          std::initializer_list<float> sliders, double freq, double amp = 0.01)
    {
        P p;
        p.prepare (sr, 512, 1);
        setParams (p, { 0.5f, shape, loComp, bal, hiComp, 1.0f, gLevelDb, 0.5f });
        // the 12 sliders follow the page-1 params in getParameters(true) order
        auto params = p.getParameters()->getParameters (true);
        int i = 8;
        for (float v : sliders)
            *dynamic_cast<juce::AudioParameterFloat*> (params[i++]) = v;
        const auto m = probeSine (p, {}, freq, amp, sr, 0.8, 0.25);
        return 20.0 * std::log10 (m.out / amp);
    }

    void runTest() override
    {
        beginTest ("DC converges and silence stays silent");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            expectWithinAbsoluteError (p.debugEq2Out(), 4.5, 0.02);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 20; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
                expect (std::abs (buf.getSample (0, i)) < 0.002f);
        }

        beginTest ("flat settings: roughly unity across the spectrum (Graphic in, sliders centred)");
        {
            double worst = 0.0;
            for (double f : { 40.0, 80.0, 150.0, 300.0, 600.0, 1200.0, 2500.0, 5000.0, 10000.0 })
            {
                const double g = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f,
                                         { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, f);
                logMessage (juce::String (f, 0).paddedLeft (' ', 6) + " Hz: " + juce::String (g, 2) + " dB");
                worst = juce::jmax (worst, std::abs (g));
            }
            expectLessThan (worst, 2.0);
        }

        beginTest ("Graphic = out bypasses the EQ (boosted bands stop mattering)");
        {
            std::initializer_list<float> hot { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15 };
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f });
            auto params = p.getParameters()->getParameters (true);
            int i = 8;
            for (float v : hot)
                *dynamic_cast<juce::AudioParameterFloat*> (params[i++]) = v;
            const auto m = probeSine (p, {}, 1000.0, 0.05, sr, 0.8, 0.2);
            const double g = 20.0 * std::log10 (m.out / 0.05);
            logMessage ("graphic out, all bands +15: " + juce::String (g, 2) + " dB");
            expectLessThan (std::abs (g), 2.5);
        }

        beginTest ("each end band moves its own region");
        {
            const double flatLo = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 30.0);
            const double hotLo = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 15,0,0,0,0,0,0,0,0,0,0,0 }, 30.0);
            const double flatHi = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 12000.0);
            const double hotHi = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,15 }, 12000.0);
            logMessage ("30 Hz band +15: " + juce::String (hotLo - flatLo, 1) + " dB; 15k band +15 @12k: " + juce::String (hotHi - flatHi, 1) + " dB");
            expectGreaterThan (hotLo - flatLo, 8.0);
            expectGreaterThan (hotHi - flatHi, 5.0);
        }

        beginTest ("High Compression squashes the high band: a bright note loses level");
        {
            const double none = gainDb (0.0f, 0.0f, 1.0f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 5000.0, 0.3);
            const double comp = gainDb (0.0f, 0.0f, 1.0f, 1.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 5000.0, 0.3);
            logMessage ("5 kHz, hi-comp 0 vs 1: " + juce::String (none, 1) + " vs " + juce::String (comp, 1) + " dB");
            expectLessThan (comp, none);
        }

        beginTest ("Pre Shape 1 scoops the mids relative to flat");
        {
            const double flatMid = gainDb (0.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 400.0);
            const double shapedMid = gainDb (1.0f, 0.0f, 0.5f, 0.0f, 0.0f, { 0,0,0,0,0,0,0,0,0,0,0,0 }, 400.0);
            logMessage ("400 Hz, flat vs shape1: " + juce::String (flatMid, 1) + " vs " + juce::String (shapedMid, 1) + " dB");
            expectLessThan (shapedMid - flatMid, -3.0);
        }

        beginTest ("random slider moves, plucked notes and hot bursts: the solver stays finite and converges");
        for (int seed = 93; seed < 95; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }
    }
};

static TraceElliotGP12StyleAmplifierProcessorTests traceElliotGP12StyleAmplifierProcessorTests;

} // namespace openguitarmultifx
