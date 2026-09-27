#include "Effects/ZendriveStyleOverdriveProcessor.h"
#include "Effects/NodalCircuit.h"
#include "Effects/SeriesDiodes.h"

#include "PedalStress.h"
#include "SineProbe.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>
#include <complex>

namespace openguitarmultifx
{

class ZendriveStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    ZendriveStyleOverdriveProcessorTests() : juce::UnitTest ("ZendriveStyleOverdriveProcessor", "Effects") {}

    static constexpr double sr = 48000.0;
    using C = std::complex<double>;
    using P = ZendriveStyleOverdriveProcessor;

    static SineProbe run (P& p, double freq, double amp, double warmup = 0.4, double measure = 0.2)
    {
        return probeSine (p, { [&p] { return p.debugStage1Out(); }, [&p] { return p.debugStage1Out() - p.debugMinus(); }, [&p] { return p.debugToneNode(); } },
                          freq, amp, sr, warmup, measure);
    }

    /** Jack to the op-amp output: 470 nF into (+) (470K to the bias); AD712 (A0 2e5, 4 MHz); (-) leg 1K + Voice + 100 nF;
        feedback = (output-to-wiper) + ((wiper-to-(-)) || 1K), all || 100 pF. */
    static double stage1Gain (double f, double drive, double voiceKnob)
    {
        const C s (0.0, 2.0 * juce::MathConstants<double>::pi * f);
        const C vPlus = 470.0e3 / (470.0e3 + 1.0 / (s * 470.0e-9));
        const double rA = juce::jmax (1.0, 500.0e3 * drive), rB = juce::jmax (1.0, 500.0e3 - rA);
        const double rF = rA + rB * 1.0e3 / (rB + 1.0e3);
        const C zFb = 1.0 / (1.0 / rF + s * 100.0e-12);
        const C zLeg = 1.0e3 + juce::jmax (1.0, 10.0e3 * (1.0 - voiceKnob)) + 1.0 / (s * 100.0e-9);
        const C beta = zLeg / (zLeg + zFb);
        const C a = 2.0e5 / (1.0 + s / (2.0 * juce::MathConstants<double>::pi * (4.0e6 / 2.0e5)));
        return std::abs (vPlus * a / (1.0 + a * beta));
    }

    void runTest() override
    {
        beginTest ("DC operating point: the op-amp output, its (-) input and the tone node at the 4.5 V bias");
        {
            P p;
            p.prepare (sr, 512, 1);
            expect (p.dcConverged());
            logMessage ("out " + juce::String (p.debugStage1Out(), 4) + " V, (-) " + juce::String (p.debugMinus(), 4) + " V, tone node "
                        + juce::String (p.debugToneNode(), 4) + " V");
            expectWithinAbsoluteError (p.debugStage1Out(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugMinus(), 4.5, 0.02);
            expectWithinAbsoluteError (p.debugToneNode(), 4.5, 0.02);
        }

        beginTest ("silence in: finite, settled near silence, no solver failures");
        {
            P p;
            p.prepare (sr, 512, 1);
            juce::AudioBuffer<float> buf (1, 512);
            for (int b = 0; b < 20; ++b)
            {
                buf.clear();
                p.process (buf);
            }
            for (int i = 0; i < 512; ++i)
            {
                expect (std::isfinite (buf.getSample (0, i)));
                expect (std::abs (buf.getSample (0, i)) < 0.001f);
            }
            expectEquals (p.getSolveFailureRate(), 0.0);
        }

        beginTest ("gain stage (Drive, Voice, AD712 gain-bandwidth) matches the closed form below the clipper's knee");
        for (float drive : { 0.1f, 0.5f, 1.0f })
            for (float voice : { 0.0f, 0.5f, 1.0f })
                for (double f : { 150.0, 1000.0 })
                {
                    P p;
                    p.prepare (sr, 512, 1);
                    setParams (p, { drive, 0.5f, voice, 0.5f });
                    const double amp = drive < 0.5f ? 0.001 : 0.00005;
                    const double measured = run (p, f, amp).tap[0] / amp;
                    const double expected = stage1Gain (f, drive, voice);
                    const double errDb = 20.0 * std::log10 (measured / expected);
                    logMessage ("Drive " + juce::String (drive, 1) + " Voice " + juce::String (voice, 1) + " @ " + juce::String (f, 0) + " Hz: "
                                + juce::String (measured, 2) + "x vs " + juce::String (expected, 2) + "x (" + juce::String (errDb, 2) + " dB)");
                    expectLessThan (std::abs (errDb), 0.5);
                }

        beginTest ("the clipper across the feedback is asymmetric: ~+0.8 V (BAT41 + body diode) and ~-1.0 V (BAT41 + 1N34A + body diode) at these currents");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 1.0f, 0.5f, 1.0f, 0.5f });
            const auto m = run (p, 440.0, 0.1);
            logMessage ("output minus (-): " + juce::String (m.tapMin[1], 3) + " .. +" + juce::String (m.tapMax[1], 3) + " V");
            expectGreaterThan (m.tapMax[1], 0.6);
            expectLessThan (m.tapMax[1], 1.0);
            expectGreaterThan (-m.tapMin[1], 0.75);
            expectLessThan (-m.tapMin[1], 1.25);
            expectGreaterThan (-m.tapMin[1], m.tapMax[1] + 0.1);
        }

        beginTest ("Tone: the knob turns the treble down (5 kHz relative to 200 Hz), from ~4.8 kHz to ~800 Hz");
        {
            auto ratio = [] (float tone)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.2f, tone, 0.5f, 1.0f });
                const double hi = run (p, 5000.0, 0.0005).out;
                P q;
                q.prepare (sr, 512, 1);
                setParams (q, { 0.2f, tone, 0.5f, 1.0f });
                const double lo = run (q, 200.0, 0.0005).out;
                return hi / lo;
            };
            const double dark = ratio (0.0f), bright = ratio (1.0f);
            logMessage ("5k/200 Hz: Tone 0 -> " + juce::String (dark, 3) + ", Tone 1 -> " + juce::String (bright, 3));
            expectGreaterThan (bright, dark * 3.0);
        }

        beginTest ("Voice: more Voice = more gain and a higher bass corner (1 kHz level up, 100 Hz relative to 1 kHz down)");
        {
            auto levels = [] (float voice)
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.3f, 1.0f, voice, 1.0f });
                const double lo = run (p, 100.0, 0.0005).out;
                P q;
                q.prepare (sr, 512, 1);
                setParams (q, { 0.3f, 1.0f, voice, 1.0f });
                const double hi = run (q, 1000.0, 0.0005).out;
                return std::make_pair (lo, hi);
            };
            const auto thick = levels (0.0f), bright = levels (1.0f);
            logMessage ("Voice 0: 100 Hz " + juce::String (thick.first, 4) + ", 1 kHz " + juce::String (thick.second, 4) + "; Voice 1: 100 Hz "
                        + juce::String (bright.first, 4) + ", 1 kHz " + juce::String (bright.second, 4));
            expectGreaterThan (bright.second, thick.second * 3.0);
            expectLessThan (bright.first / bright.second, thick.first / thick.second * 0.5);
        }

        beginTest ("a chain of series diodes is one diode: BAT41 + 1N34A + body diode against the explicit netlist");
        {
            const DiodeModel schottky { 13.0e-9, 1.1 * 25.85e-3 }, ge { 200.0e-9, 1.3 * 25.85e-3 }, body { 2.0e-12, 1.4 * 25.85e-3 };
            const auto eq = seriesDiodes ({ schottky, ge, body });
            for (double volts : { 0.5, 1.5, 3.0, 8.0 })
            {
                auto solve = [&] (bool chain)
                {
                    NodalCircuit c;
                    const auto n1 = c.addNode(), n2 = c.addNode(), n3 = c.addNode(), n4 = c.addNode();
                    const int src = c.addSource (n1, volts);
                    c.addResistor (n1, n2, 100.0e3);
                    if (chain)
                    {
                        c.addDiode (n2, n3, schottky.Is, schottky.nVt);
                        c.addDiode (n3, n4, ge.Is, ge.nVt);
                        c.addDiode (n4, NodalCircuit::ground, body.Is, body.nVt);
                        c.setInitialGuess (n3, 0.3);
                        c.setInitialGuess (n4, 0.6);
                    }
                    else
                        c.addDiode (n2, NodalCircuit::ground, eq.Is, eq.nVt);
                    c.setInitialGuess (n2, 0.9);
                    c.prepare (48000.0);
                    c.setSource (src, volts);
                    for (int i = 0; i < 50; ++i)
                        c.solveSample();
                    return c.voltage (n2);
                };
                const double explicitV = solve (true), oneDiode = solve (false);
                logMessage ("source " + juce::String (volts, 1) + " V: chain " + juce::String (explicitV, 4) + " V, one diode " + juce::String (oneDiode, 4) + " V");
                expectWithinAbsoluteError (oneDiode, explicitV, volts < 1.0 ? 0.006 : 0.002); // below ~1 uA the dropped -1 of each junction shows
            }
        }

        beginTest ("Volume: monotonic and silent at 0");
        {
            double previous = -1.0;
            for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                P p;
                p.prepare (sr, 512, 1);
                setParams (p, { 0.5f, 0.5f, 0.5f, v });
                const double out = run (p, 440.0, 0.05).out;
                expectGreaterThan (out, previous);
                previous = out;
            }
        }

        beginTest ("stays finite/bounded and converges under a hot sine, every Drive setting");
        for (float d : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { d, 0.5f, 0.5f, 1.0f });
            const auto m = run (p, 220.0, 0.5, 0.3, 0.1);
            expect (std::isfinite (m.out));
            expect (std::abs (m.outMax) < 3.0f && std::abs (m.outMin) < 3.0f);
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("stereo: identical input gives identical channels; different input keeps them independent");
        {
            P p;
            p.prepare (sr, 512, 2);
            setParams (p, { 0.7f, 0.3f, 0.5f, 0.8f });
            juce::AudioBuffer<float> buf (2, 512);
            for (int b = 0; b < 4; ++b)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float x = 0.2f * std::sin (0.05f * (float) (b * 512 + i));
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                p.process (buf);
                for (int i = 0; i < 512; ++i)
                    expectEquals (buf.getSample (0, i), buf.getSample (1, i));
            }
            for (int i = 0; i < 512; ++i)
            {
                buf.setSample (0, i, 0.2f * std::sin (0.05f * (float) i));
                buf.setSample (1, i, 0.0f);
            }
            p.process (buf);
            expect (std::abs (buf.getSample (1, 511)) < 0.05f);
            expectGreaterThan (std::abs (buf.getSample (0, 256)), 0.01f);
        }

        beginTest ("random knob moves, plucked notes and hot bursts: the solver never fails to converge (no frozen circuit)");
        for (int seed = 1234; seed < 1238; ++seed)
        {
            P p;
            p.prepare (sr, 128, 2);
            expectEquals (runPedalStress (p, 12.0, seed), 0);
            expectLessThan (p.getSolveFailureRate(), 1.0e-5);
        }

        beginTest ("cost (informational)");
        {
            P p;
            p.prepare (sr, 512, 1);
            setParams (p, { 0.8f, 0.3f, 0.5f, 0.7f });
            juce::AudioBuffer<float> buf (1, 512);
            const int blocks = 400;
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, 0.1f * std::sin (0.06f * (float) (b * 512 + i)));
                p.process (buf);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro> (t1 - t0).count() / (double) (blocks * 512);
            logMessage ("Zendrive: " + juce::String (us, 2) + " us/sample = " + juce::String (100.0 * us * sr * 1.0e-6, 1) + "% of one core at 48 kHz (mono, no oversampling)");
        }
    }
};

static ZendriveStyleOverdriveProcessorTests zendriveStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
