#include "Effects/TubeScreamerStyleOverdriveProcessor.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace openguitarmultifx
{

class TubeScreamerStyleOverdriveProcessorTests : public juce::UnitTest
{
public:
    TubeScreamerStyleOverdriveProcessorTests() : juce::UnitTest ("TubeScreamerStyleOverdriveProcessor", "Effects") {}

    using Model = TubeScreamerStyleOverdriveProcessor::Model;

    static juce::String prefixFor (Model m)
    {
        return m == Model::ts808 ? "ts808" : (m == Model::ts9 ? "ts9" : "ts10");
    }

    static void setParam (TubeScreamerStyleOverdriveProcessor& proc, const juce::String& paramId, float value)
    {
        if (auto* group = proc.getParameters())
            for (auto* param : group->getParameters (true))
                if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                    if (floatParam->paramID == paramId)
                        *floatParam = value;
    }

    static void setKnobs (TubeScreamerStyleOverdriveProcessor& proc, Model m, float drive, float tone, float level)
    {
        setParam (proc, prefixFor (m) + "_drive", drive);
        setParam (proc, prefixFor (m) + "_tone", tone);
        setParam (proc, prefixFor (m) + "_level", level);
    }

    struct Result { double fundamental; float posPeak, negPeak; };

    /** Runs a sine through the processor and reports the steady-state
        fundamental amplitude (correlation over the last whole cycles) plus
        the waveform's positive/negative peaks. */
    static Result runSine (TubeScreamerStyleOverdriveProcessor& proc, double freq, double amplitude,
                            double warmupSeconds = 0.4, double measureSeconds = 0.2)
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;

        juce::AudioBuffer<float> buffer (1, block);
        const long long warmup = (long long) (warmupSeconds * sr);
        const long long measure = (long long) (measureSeconds * sr);
        const long long total = warmup + measure;

        // Correlate over a whole number of cycles so the fundamental is exact.
        const double cycles = std::floor (measureSeconds * freq);
        const long long measureLen = (long long) std::llround (cycles * sr / freq);
        const long long measureStart = total - measureLen;

        double sumSin = 0.0, sumCos = 0.0;
        float posPeak = 0.0f, negPeak = 0.0f;

        for (long long n0 = 0; n0 < total; n0 += block)
        {
            for (int i = 0; i < block; ++i)
                buffer.setSample (0, i, (float) (amplitude * std::sin (twoPi * freq * (double) (n0 + i) / sr)));

            proc.process (buffer);

            for (int i = 0; i < block; ++i)
            {
                const long long n = n0 + i;
                if (n < measureStart || n >= measureStart + measureLen)
                    continue;

                const double y = (double) buffer.getSample (0, i);
                const double ph = twoPi * freq * (double) n / sr;
                sumSin += y * std::sin (ph);
                sumCos += y * std::cos (ph);
                posPeak = juce::jmax (posPeak, (float) y);
                negPeak = juce::jmin (negPeak, (float) y);
            }
        }

        const double amp = 2.0 * std::sqrt (sumSin * sumSin + sumCos * sumCos) / (double) measureLen;
        return { amp, posPeak, negPeak };
    }

    void runTest() override
    {
        beginTest ("silence in stays finite and settles near silence, all three models");
        for (auto model : { Model::ts808, Model::ts9, Model::ts10 })
        {
            TubeScreamerStyleOverdriveProcessor ts (model);
            ts.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 20; ++block)
            {
                buffer.clear();
                ts.process (buffer);
            }

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float s = buffer.getSample (0, i);
                expect (std::isfinite (s));
                expect (std::abs (s) < 0.05f);
            }
        }

        beginTest ("display names carry the -Style suffix, never a bare trademark");
        {
            expectEquals (juce::String (TubeScreamerStyleOverdriveProcessor (Model::ts808).getName()), juce::String ("TS808-Style Overdrive"));
            expectEquals (juce::String (TubeScreamerStyleOverdriveProcessor (Model::ts9).getName()), juce::String ("TS9-Style Overdrive"));
            expectEquals (juce::String (TubeScreamerStyleOverdriveProcessor (Model::ts10).getName()), juce::String ("TS10-Style Overdrive"));
        }

        beginTest ("stays finite/bounded and the clipper converges under a loud sine, every Drive setting");
        for (auto model : { Model::ts808, Model::ts9, Model::ts10 })
        {
            for (float driveSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                TubeScreamerStyleOverdriveProcessor ts (model);
                ts.prepare (48000.0, 512, 1);
                setKnobs (ts, model, driveSetting, 0.5f, 0.5f);

                const auto r = runSine (ts, 220.0, 0.8, 0.3, 0.1);
                expect (std::isfinite (r.fundamental));
                expect (std::abs (r.posPeak) < 20.0f && std::abs (r.negPeak) < 20.0f);
                expectLessThan (ts.getClipperFailureRate(), 0.001);
            }
        }

        beginTest ("clipper convergence at the worst case: max Drive, hot input, high frequency");
        {
            TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
            ts.prepare (48000.0, 512, 1);
            setKnobs (ts, Model::ts808, 1.0f, 1.0f, 1.0f);
            runSine (ts, 3000.0, 1.0, 0.3, 0.1);
            logMessage ("clipper failure rate: " + juce::String (ts.getClipperFailureRate(), 6));
            expectLessThan (ts.getClipperFailureRate(), 0.001);
        }

        beginTest ("Q1's DC operating point is forward-active, not cutoff");
        {
            TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
            ts.prepare (48000.0, 512, 1);

            juce::AudioBuffer<float> buffer (1, 512);
            for (int block = 0; block < 40; ++block)
            {
                buffer.clear();
                ts.process (buffer);
            }

            const auto bias = ts.getDebugBiasPoint();
            expect (std::isfinite (bias.vBase) && std::isfinite (bias.vEmitter) && std::isfinite (bias.vCollector));

            const float vbe = bias.vBase - bias.vEmitter;
            expectGreaterOrEqual (vbe, 0.2f);
            expectLessOrEqual (vbe, 0.8f);
        }

        beginTest ("clipping is symmetric (two diodes, one each way)");
        {
            // The opposite of the OD-1's asymmetric test: equal diode
            // counts both ways means the positive and negative peaks of a
            // hard-clipped sine must match closely.
            TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
            ts.prepare (48000.0, 512, 1);
            setKnobs (ts, Model::ts808, 1.0f, 0.5f, 0.5f);

            const auto r = runSine (ts, 110.0, 0.5, 0.5, 0.2);
            logMessage ("posPeak=" + juce::String (r.posPeak, 4) + " negPeak=" + juce::String (r.negPeak, 4));
            expectGreaterThan (r.posPeak, 0.05f); // it actually produced output
            expectLessThan (std::abs (r.posPeak - std::abs (r.negPeak)) / r.posPeak, 0.05f);
        }

        beginTest ("small-signal frequency response has the mid hump: bass gets far less gain than 1 kHz");
        {
            // 5 mV keeps the diodes off, so this is the linear gain shape:
            // R4+C3 leave bass at low gain (720 Hz corner), per Geofex.
            TubeScreamerStyleOverdriveProcessor lowFreq (Model::ts9), midFreq (Model::ts9);
            lowFreq.prepare (48000.0, 512, 1);
            midFreq.prepare (48000.0, 512, 1);
            setKnobs (lowFreq, Model::ts9, 0.5f, 0.5f, 0.5f);
            setKnobs (midFreq, Model::ts9, 0.5f, 0.5f, 0.5f);

            const auto low = runSine (lowFreq, 100.0, 0.005);
            const auto mid = runSine (midFreq, 1000.0, 0.005);
            logMessage ("gain@100Hz=" + juce::String (low.fundamental / 0.005, 2)
                        + " gain@1kHz=" + juce::String (mid.fundamental / 0.005, 2));
            expectGreaterThan (mid.fundamental / low.fundamental, 2.5);
        }

        beginTest ("Drive is a real gain control: more Drive, more output, for a guitar-level signal");
        {
            double previous = -1.0;
            for (float driveSetting : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
                ts.prepare (48000.0, 512, 1);
                setKnobs (ts, Model::ts808, driveSetting, 0.5f, 0.5f);

                const auto r = runSine (ts, 1000.0, 0.05);
                logMessage ("drive " + juce::String (driveSetting, 2) + " -> fundamental " + juce::String (r.fundamental, 4));
                expectGreaterThan (r.fundamental, previous);
                previous = r.fundamental;
            }
        }

        beginTest ("Tone works: treble content rises with Tone, bass barely moves");
        {
            TubeScreamerStyleOverdriveProcessor dark (Model::ts808), bright (Model::ts808);
            dark.prepare (48000.0, 512, 1);
            bright.prepare (48000.0, 512, 1);
            setKnobs (dark, Model::ts808, 0.3f, 0.0f, 0.5f);
            setKnobs (bright, Model::ts808, 0.3f, 1.0f, 0.5f);

            const auto darkTreble = runSine (dark, 6000.0, 0.005);
            const auto brightTreble = runSine (bright, 6000.0, 0.005);
            logMessage ("6kHz: tone0=" + juce::String (darkTreble.fundamental, 5)
                        + " tone1=" + juce::String (brightTreble.fundamental, 5));
            expectGreaterThan (brightTreble.fundamental / darkTreble.fundamental, 3.0);

            TubeScreamerStyleOverdriveProcessor dark2 (Model::ts808), bright2 (Model::ts808);
            dark2.prepare (48000.0, 512, 1);
            bright2.prepare (48000.0, 512, 1);
            setKnobs (dark2, Model::ts808, 0.3f, 0.0f, 0.5f);
            setKnobs (bright2, Model::ts808, 0.3f, 1.0f, 0.5f);

            const auto darkBass = runSine (dark2, 200.0, 0.005);
            const auto brightBass = runSine (bright2, 200.0, 0.005);
            const double bassRatio = brightBass.fundamental / darkBass.fundamental;
            logMessage ("200Hz ratio tone1/tone0 = " + juce::String (bassRatio, 3));
            expectGreaterThan (bassRatio, 0.7);
            expectLessThan (bassRatio, 1.4);
        }

        beginTest ("Level scales the output");
        {
            double previous = -1.0;
            for (float levelSetting : { 0.1f, 0.4f, 0.7f, 1.0f })
            {
                TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
                ts.prepare (48000.0, 512, 1);
                setKnobs (ts, Model::ts808, 0.5f, 0.5f, levelSetting);

                const auto r = runSine (ts, 1000.0, 0.05);
                expectGreaterThan (r.fundamental, previous);
                previous = r.fundamental;
            }
        }

        beginTest ("TS808 and TS9 sound the same to within the output-stage difference (Geofex: only R14/R15 differ)");
        {
            TubeScreamerStyleOverdriveProcessor ts808 (Model::ts808), ts9 (Model::ts9);
            ts808.prepare (48000.0, 512, 1);
            ts9.prepare (48000.0, 512, 1);
            setKnobs (ts808, Model::ts808, 0.6f, 0.5f, 0.6f);
            setKnobs (ts9, Model::ts9, 0.6f, 0.5f, 0.6f);

            const auto a = runSine (ts808, 440.0, 0.1);
            const auto b = runSine (ts9, 440.0, 0.1);
            const double relDiff = std::abs (a.fundamental - b.fundamental) / juce::jmax (a.fundamental, b.fundamental);
            logMessage ("808 fundamental=" + juce::String (a.fundamental, 5) + " TS9=" + juce::String (b.fundamental, 5)
                        + " relDiff=" + juce::String (relDiff, 4));
            expectLessThan (relDiff, 0.03);
        }

        beginTest ("TS10's documented differences: higher Q1 bias, and a small-signal level trim from the 220ohm divider + JFET-bias loading");
        {
            // Q1 bias: 9.2K/22K off +9V = 6.35V through 510K, vs the 4.5V rail.
            TubeScreamerStyleOverdriveProcessor ts9 (Model::ts9), ts10 (Model::ts10);
            ts9.prepare (48000.0, 512, 1);
            ts10.prepare (48000.0, 512, 1);
            const auto b9 = ts9.getDebugBiasPoint();
            const auto b10 = ts10.getDebugBiasPoint();
            logMessage ("Q1 base: TS9=" + juce::String (b9.vBase, 3) + " TS10=" + juce::String (b10.vBase, 3));
            expectGreaterThan (b10.vBase - b9.vBase, 1.0f);
            expectGreaterOrEqual (b10.vBase - b10.vEmitter, 0.2f);
            expectLessOrEqual (b10.vBase - b10.vEmitter, 0.8f);

            // Linear regime (Drive 0, 2 mV in, so the diodes stay off). Two
            // independent, hand-derived TS10 effects multiply:
            //  - the 220ohm ahead of the (+) pin's 10K bias node:
            //    10K/(10K+220) = 0.978
            //  - the two 510K JFET-bias resistors loading the Level wiper
            //    (~19K source impedance at Level 0.5): the wiper now drives
            //    ~160K instead of the ~430K the TS9's Q2 base network
            //    presents, so 160/(160+19) / (430/(430+19)) = ~0.934
            // (plus ~+0.5% from Q1's higher bias lowering its emitter
            // resistance). Product ~0.918 -- the measured value.
            setKnobs (ts9, Model::ts9, 0.0f, 0.5f, 0.5f);
            setKnobs (ts10, Model::ts10, 0.0f, 0.5f, 0.5f);
            const auto a = runSine (ts9, 1000.0, 0.002);
            const auto b = runSine (ts10, 1000.0, 0.002);
            const double ratio = b.fundamental / a.fundamental;
            logMessage ("TS10/TS9 small-signal ratio = " + juce::String (ratio, 4) + " (hand-derived ~0.918)");
            expectGreaterThan (ratio, 0.90);
            expectLessThan (ratio, 0.94);
        }

        beginTest ("stereo input runs identical circuits per channel, no cross-talk");
        {
            TubeScreamerStyleOverdriveProcessor ts (Model::ts808);
            ts.prepare (48000.0, 512, 2);
            setKnobs (ts, Model::ts808, 0.7f, 0.5f, 0.5f);

            juce::AudioBuffer<float> buffer (2, 512);
            float maxAsymmetry = 0.0f;

            for (int block = 0; block < 40; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (double) (block * 512 + i) / 48000.0;
                    const float x = (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t));
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }
                ts.process (buffer);

                if (block >= 30)
                    for (int i = 0; i < 512; ++i)
                        maxAsymmetry = juce::jmax (maxAsymmetry, std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
            }

            expectLessThan (maxAsymmetry, 1.0e-4f);
        }
    }
};

static TubeScreamerStyleOverdriveProcessorTests tubeScreamerStyleOverdriveProcessorTests;

} // namespace openguitarmultifx
