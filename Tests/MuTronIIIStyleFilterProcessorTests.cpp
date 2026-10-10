#include "Effects/MuTronIIIStyleFilterProcessor.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class MuTronIIIStyleFilterProcessorTests : public juce::UnitTest
{
public:
    MuTronIIIStyleFilterProcessorTests() : juce::UnitTest ("MuTronIIIStyleFilterProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using P = MuTronIIIStyleFilterProcessor;

    /** Params: Gain, Peak, Mode (0 LP/1 BP/2 HP), Range (0 Lo/1 Hi), Drive (0 Up/1 Down). */
    static double outAmp (std::initializer_list<float> params, double freq, double amp = 0.1)
    {
        P p;
        p.prepare (sr, 512, 1);
        setParams (p, params);
        return probeSine (p, {}, freq, amp, sr, 0.6, 0.2).out;
    }

    void runTest() override
    {
        beginTest ("silence stays silent and audible output at noon");
        {
            P p;
            p.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 10; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
                expect (std::abs (buf.getSample (0, i)) < 0.001f);

            const double out = outAmp ({ 0.5, 0.5, 1.0, 1.0, 0.0 }, 220.0);
            expectGreaterThan (out, 0.005);
        }

        beginTest ("the envelope sweeps the filter up in Drive-Up mode");
        {
            // With the envelope lit the BP peak sits high; measure the sweep resistance directly.
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.8f, 0.3f, 1.0f, 1.0f, 0.0f });
            probeSine (p, {}, 200.0, 0.15, sr, 0.8, 0.1); // sustain a loud low note
            const double lit = p.debugSweepOhms();
            P q;
            q.prepare (sr, 512, 1);
            setParams (q, { 0.8f, 0.3f, 1.0f, 1.0f, 0.0f });
            const double dark = q.debugSweepOhms();
            logMessage ("dark Reff " + juce::String (dark, 0) + " ohm, lit " + juce::String (lit, 0) + " ohm");
            expectGreaterThan (lit, 0.0);
            expectLessThan (lit, dark); // light -> lower R -> higher fc (up sweep)
        }

        beginTest ("Down mode mirrors the sweep");
        {
            auto idleReff = [] (float driveKnob)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.8f, 0.3f, 1.0f, 1.0f, driveKnob });
                juce::AudioBuffer<float> buf (1, 512); // silence: the sweep readout only updates in process()
                buf.clear();
                p.process (buf);
                return p.debugSweepOhms();
            };
            const double darkDown = idleReff (1.0f); // Drive = Down
            const double darkUp = idleReff (0.0f);   // Drive = Up
            logMessage ("down-mode idle Reff " + juce::String (darkDown, 0) + ", up-mode idle " + juce::String (darkUp, 0));
            // parked: down mode idles at the top of the sweep (lit), up mode at the bottom (dark)
            expectLessThan (darkDown, darkUp);
        }

        beginTest ("Peak raises resonance: BP gain at the parked centre frequency");
        {
            // A quiet probe keeps the envelope below the detector threshold, so the filter stays
            // parked near its dark fc (~200 Hz in Hi): the BP resonance 1/d is directly measurable.
            auto resonance = [] (float peakKnob)
            {
                double best = 0.0;
                for (double f : { 150.0, 180.0, 200.0, 220.0, 250.0 })
                    best = juce::jmax (best, outAmp ({ 0.8f, peakKnob, 1.0f, 1.0f, 0.0f }, f, 0.003));
                return best;
            };
            const double lowPeak = resonance (0.0f);
            const double highPeak = resonance (1.0f);
            logMessage ("peak 0 resonance " + juce::String (lowPeak, 3) + ", peak 1 " + juce::String (highPeak, 3));
            expectGreaterThan (highPeak, lowPeak * 3.0);
        }

        beginTest ("Mode selects the right output: HP kills the lows, LP kills the highs");
        {
            const double lpLow = outAmp ({ 0.6f, 0.2f, 0.0f, 1.0f, 0.0f }, 100.0, 0.15);
            const double hpLow = outAmp ({ 0.6f, 0.2f, 2.0f, 1.0f, 0.0f }, 100.0, 0.15);
            logMessage ("lp @100Hz " + juce::String (lpLow, 3) + ", hp @100Hz " + juce::String (hpLow, 3));
            expectGreaterThan (lpLow, hpLow);
        }

        beginTest ("random knob moves, plucked notes and hot bursts stay finite");
        for (int seed = 89; seed < 91; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 10.0, seed), 0);
        }
    }
};

static MuTronIIIStyleFilterProcessorTests muTronIIIStyleFilterProcessorTests;

} // namespace openguitarmultifx
