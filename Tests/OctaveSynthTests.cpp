#include "EffectRegistry.h"
#include "Effects/Pitch/AlgorithmicPitchProcessor.h"
#include "Effects/Pitch/OctaveFuzzProcessor.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <cmath>

namespace openguitarmultifx
{

/**
    Octave and synth effects (Source/Effects/Pitch): each one makes the octave it claims -- a 220 Hz sine in, the
    energy at 110 / 220 / 440 / 55 Hz measured (Goertzel) -- plus the Mono Synth's tracking, and the common contract:
    finite, bounded, inside the CPU budget.
*/
class OctaveSynthTests : public juce::UnitTest
{
public:
    OctaveSynthTests() : juce::UnitTest ("OctaveSynth", "Effects") {}

    static constexpr double sr = 48000.0;

    /** Energy around `hz`: Goertzel on Hann-windowed 2048-sample frames (main lobe +-47 Hz), magnitudes averaged. A
        granular shifter's output sits in sidebands spaced by its grain rate, which one long exact bin would miss. */
    static double goertzel (const std::vector<float>& x, double hz)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr, k = 2.0 * std::cos (w);
        constexpr size_t frame = 2048;
        double total = 0.0;
        int frames = 0;
        for (size_t start = 0; start + frame <= x.size(); start += frame, ++frames)
        {
            double s1 = 0.0, s2 = 0.0;
            for (size_t n = 0; n < frame; ++n)
            {
                const double hann = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) n / (double) frame);
                const double s0 = hann * x[start + n] + k * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            total += std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) / (0.5 * frame);
        }
        return frames > 0 ? total / frames : 0.0;
    }

    static void set (EffectProcessor& p, const juce::String& suffix, float v)
    {
        for (auto* prm : p.getParameters()->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                if (f->paramID.endsWith (suffix))
                    *f = v;
    }

    /** Runs 2 s of a 220 Hz sine (amplitude `amp`) into both channels, as the engine feeds a mono guitar; returns the
        left channel's last second. */
    static std::vector<float> run (EffectProcessor& p, double amp, double& cpuPercent)
    {
        p.prepare (sr, 512, 2);
        juce::AudioBuffer<float> b (2, 256);
        std::vector<float> out;
        const auto t0 = std::chrono::steady_clock::now();
        const int blocks = (int) (2.0 * sr / 256);
        for (int blk = 0; blk < blocks; ++blk)
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (blk * 256 + i) / sr));
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            p.process (b);
            if (blk >= blocks / 2)
                out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + 256);
        }
        cpuPercent = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 2.0;
        return out;
    }

    static std::vector<float> runToneMachine (double amp, bool octaveOn)
    {
        OctaveFuzzProcessor p (OctaveFuzzModel::toneMachine);
        set (p, "_octave", octaveOn ? 1.0f : 0.0f);
        p.prepare (sr, 256, 1);
        juce::AudioBuffer<float> b (1, 256);
        std::vector<float> out;
        constexpr int total = 2 * (int) sr, skip = (int) (0.5 * sr);
        for (int base = 0; base < total; base += 256)
        {
            for (int i = 0; i < 256; ++i)
                b.setSample (0, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (base + i) / sr)));
            p.process (b);
            if (base + 256 > skip)
            {
                const int first = juce::jmax (0, skip - base);
                out.insert (out.end(), b.getReadPointer (0) + first, b.getReadPointer (0) + 256);
            }
        }
        return out;
    }

    static double runSilentToggle (bool fromOn, bool toOn)
    {
        constexpr int block = 128, second = (int) sr;
        OctaveFuzzProcessor p (OctaveFuzzModel::toneMachine);
        set (p, "_octave", fromOn ? 1.0f : 0.0f);
        p.prepare (sr, block, 1);
        juce::AudioBuffer<float> b (1, block);
        for (int n = 0; n < second; n += block)
        {
            b.clear();
            p.process (b);
        }
        set (p, "_octave", toOn ? 1.0f : 0.0f);
        double peak = 0.0;
        for (int n = 0; n < second; n += block)
        {
            b.clear();
            p.process (b);
            for (int i = 0; i < block; ++i)
                peak = std::max (peak, (double) std::abs (b.getSample (0, i)));
        }
        return peak;
    }

    static double freshToneMachinePeak (bool octaveOn)
    {
        constexpr int block = 128, second = (int) sr;
        OctaveFuzzProcessor p (OctaveFuzzModel::toneMachine);
        set (p, "_octave", octaveOn ? 1.0f : 0.0f);
        p.prepare (sr, block, 1);
        juce::AudioBuffer<float> b (1, block);
        double peak = 0.0;
        for (int n = 0; n < second; n += block)
        {
            for (int i = 0; i < block; ++i)
                b.setSample (0, i, (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (n + i) / sr)));
            p.process (b);
            for (int i = 0; i < block; ++i)
                peak = std::max (peak, (double) std::abs (b.getSample (0, i)));
        }
        return peak;
    }

    static double runSineToggle (bool fromOn, bool toOn)
    {
        constexpr int block = 128, second = (int) sr;
        OctaveFuzzProcessor p (OctaveFuzzModel::toneMachine);
        set (p, "_octave", fromOn ? 1.0f : 0.0f);
        p.prepare (sr, block, 1);
        juce::AudioBuffer<float> b (1, block);
        for (int n = 0; n < second; n += block)
        {
            for (int i = 0; i < block; ++i)
                b.setSample (0, i, (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (n + i) / sr)));
            p.process (b);
        }
        set (p, "_octave", toOn ? 1.0f : 0.0f);
        double peak = 0.0;
        for (int n = 0; n < second; n += block)
        {
            for (int i = 0; i < block; ++i)
                b.setSample (0, i, (float) (0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (second + n + i) / sr)));
            p.process (b);
            for (int i = 0; i < block; ++i)
                peak = std::max (peak, (double) std::abs (b.getSample (0, i)));
        }
        return peak;
    }

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        beginTest ("Tone Machine schematic operating point");
        {
            OctaveFuzzProcessor p (OctaveFuzzModel::toneMachine);
            p.prepare (sr, 256, 1);
            logMessage ("Q1 base " + juce::String (p.debugNodeVoltage (3), 3) + " V, Q1 collector/splitter base "
                        + juce::String (p.debugNodeVoltage (4), 3) + " V, splitter emitter " + juce::String (p.debugNodeVoltage (7), 3)
                        + " V, splitter collector " + juce::String (p.debugNodeVoltage (6), 3) + " V, rectifier sum "
                        + juce::String (p.debugNodeVoltage (11), 3) + " V");
            expectWithinAbsoluteError (p.debugNodeVoltage (3), 0.748, 0.05);
            expectWithinAbsoluteError (p.debugNodeVoltage (4), 2.216, 0.05);
            expectWithinAbsoluteError (p.debugNodeVoltage (7), 1.589, 0.05);
            expectWithinAbsoluteError (p.debugNodeVoltage (6), 7.378, 0.05);
            expectWithinAbsoluteError (p.debugNodeVoltage (11), 3.112, 0.05);
        }

        beginTest ("Tone Machine Octave switch selects octave or fundamental at 0.03 and 0.08 input");
        for (const double amp : { 0.03, 0.08 })
        {
            const auto on = runToneMachine (amp, true);
            const auto off = runToneMachine (amp, false);
            const double onFundamental = goertzel (on, 220.0), onOctave = goertzel (on, 440.0);
            const double offFundamental = goertzel (off, 220.0), offOctave = goertzel (off, 440.0);
            logMessage ("input " + juce::String (amp, 2) + ": Octave ON 440/220 = "
                        + juce::String (onOctave / juce::jmax (onFundamental, 1.0e-12), 3)
                        + " (" + juce::String (onOctave, 5) + " / " + juce::String (onFundamental, 5)
                        + "), OFF 440/220 = " + juce::String (offOctave / juce::jmax (offFundamental, 1.0e-12), 3)
                        + " (" + juce::String (offOctave, 5) + " / " + juce::String (offFundamental, 5) + ")");
            expectGreaterThan (onOctave, onFundamental, "Octave ON emphasizes 440 Hz at input " + juce::String (amp, 2));
            expectLessThan (offOctave, offFundamental, "Octave OFF emphasizes 220 Hz at input " + juce::String (amp, 2));
        }

        beginTest ("Tone Machine Octave toggle during playback doesn't thump");
        for (const auto& [fromOn, toOn] : { std::pair { true, false }, std::pair { false, true } })
        {
            const double silentPeak = runSilentToggle (fromOn, toOn);
            logMessage ("silence " + juce::String (fromOn ? "ON" : "OFF") + " -> " + (toOn ? "ON" : "OFF")
                        + ": peak " + juce::String (silentPeak, 6));
            expectLessThan (silentPeak, 1.0e-3);
        }
        const double steadyOff = freshToneMachinePeak (false), steadyOn = freshToneMachinePeak (true);
        for (const auto& [fromOn, toOn] : { std::pair { true, false }, std::pair { false, true } })
        {
            const double transitionPeak = runSineToggle (fromOn, toOn);
            const double steadyPeak = toOn ? steadyOn : steadyOff;
            logMessage ("sine " + juce::String (fromOn ? "ON" : "OFF") + " -> " + (toOn ? "ON" : "OFF")
                        + ": peak " + juce::String (transitionPeak, 6) + ", fresh-position peak "
                        + juce::String (steadyPeak, 6) + ", limit " + juce::String (1.5 * steadyPeak, 6));
            expectLessThan (transitionPeak, 1.5 * steadyPeak);
        }

        // amp: the test level (0.3 = a hot pickup). cpuLimit: 5% of a core, except the octave fuzzes: full netlists that the
        // alias table (docs/circuits/OctaveFuzzes.md) keeps at 2x even in Eco, like the Fuzz Face / Tone Bender / Big Muff.
        struct Case { const char* key; double expectHz; std::vector<std::pair<const char*, float>> settings; double amp = 0.3; double cpuLimit = 5.0; };
        const std::vector<Case> cases {
            { "OctaviaStyleFuzz", 440.0, {}, 0.3, 12.0 },
            { "GreenRingerStyleFuzz", 440.0, {}, 0.3, 12.0 },
            { "SuperFuzzStyleFuzz", 440.0, {}, 0.3, 12.0 },
            { "ToneMachineStyleFuzz", 440.0, { { "_octave", 1.0f } }, 0.03, 12.0 },
            { "OctaveMultiplexerStyleOctaver", 110.0, { { "_blend", 1.0f } } },
            { "FX35StyleOctaver", 110.0, { { "_direct", 0.0f } } },
            { "OCT1StyleOctaver", 110.0, { { "_direct", 0.0f } } },
            { "MicroSynthStyleSynth", 110.0, { { "_guitar", 0.0f }, { "_square", 0.0f }, { "_start", 1.0f }, { "_stop", 1.0f } } },
            { "BassMicroSynthStyleSynth", 110.0, { { "_guitar", 0.0f }, { "_square", 0.0f }, { "_start", 1.0f }, { "_stop", 1.0f } } },
            { "GuitarSynthStyleSynth", 110.0, { { "_guitar", 0.0f }, { "_square", 0.0f }, { "_start", 1.0f }, { "_stop", 1.0f } } },
            { "BrassmasterStyleSynth", 440.0, { { "_bass", 0.0f } } },
            { "PolyOctave", 110.0, { { "_dry", 0.0f }, { "_up1", 0.0f } } },
            { "Whammy", 440.0, {} },
            { "OrganMachine", 220.0, {} },
            { "MonoSynth", 220.0, { { "_mix", 1.0f } } } };

        for (const auto& cs : cases)
        {
            beginTest (juce::String (cs.key) + ": the expected octave dominates; bounded; inside the CPU budget");
            auto fx = registry.create (cs.key);
            for (const auto& [suffix, v] : cs.settings)
                set (*fx, suffix, v);
            double cpu = 0.0;
            const auto y = run (*fx, cs.amp, cpu);
            double peak = 0.0;
            bool finite = true;
            for (float v : y) { finite = finite && std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
            const double e55 = goertzel (y, 55.0), e110 = goertzel (y, 110.0), e220 = goertzel (y, 220.0), e440 = goertzel (y, 440.0);
            logMessage (juce::String (cs.key) + ": 55 Hz " + juce::String (e55, 4) + "  110 Hz " + juce::String (e110, 4) + "  220 Hz "
                        + juce::String (e220, 4) + "  440 Hz " + juce::String (e440, 4) + "  peak " + juce::String (peak, 2)
                        + "  CPU (stereo in) " + juce::String (cpu, 2) + "%");
            expect (finite);
            expect (peak < 10.0);
            expectLessThan (cpu, cs.cpuLimit);
            const double target = cs.expectHz == 440.0 ? e440 : (cs.expectHz == 110.0 ? e110 : e220);
            for (double other : { e110, e220, e440 })
                if (other != target)
                    expect (target > other, juce::String (cs.expectHz) + " Hz should dominate");
        }

        beginTest ("Silence in, silence out: no start-up step from an operating point");
        for (const auto& cs : cases)
        {
            auto fx = registry.create (cs.key);
            fx->prepare (sr, 512, 2);
            juce::AudioBuffer<float> b (2, 256);
            double peak = 0.0;
            for (int blk = 0; blk < 40; ++blk)
            {
                b.clear();
                fx->process (b);
                peak = std::max ({ peak, (double) b.getMagnitude (0, 0, 256), (double) b.getMagnitude (1, 0, 256) });
            }
            logMessage (juce::String (cs.key) + ": start-up peak " + juce::String (peak, 5));
            expectLessThan (peak, 0.001);
        }

        beginTest ("Octave fuzz netlists converge cheaply");
        for (auto m : { OctaveFuzzModel::octavia, OctaveFuzzModel::greenRinger, OctaveFuzzModel::superFuzz, OctaveFuzzModel::toneMachine })
        {
            OctaveFuzzProcessor p (m);
            double cpu = 0.0;
            run (p, 0.3, cpu);
            logMessage (juce::String (p.getName()) + ": " + juce::String (p.debugAverageIterations(), 2) + " Newton iterations / sample, failures "
                        + juce::String (p.getSolveFailureRate(), 4) + ", CPU (1x, stereo in) " + juce::String (cpu, 2) + "%");
            expectLessThan (p.getSolveFailureRate(), 0.001);
        }

        beginTest ("Mono Synth tracks a 220 Hz note");
        {
            AlgorithmicPitchProcessor p (AlgorithmicPitch::monoSynth);
            double cpu = 0.0;
            run (p, 0.3, cpu);
            logMessage ("tracked " + juce::String (p.debugTrackedHz(), 2) + " Hz");
            expectWithinAbsoluteError (p.debugTrackedHz(), 220.0, 5.0);
        }

        beginTest ("OCT-1 in -2: 55 Hz");
        {
            auto fx = registry.create ("OCT1StyleOctaver");
            set (*fx, "_direct", 0.0f);
            set (*fx, "_octave", 1.0f);
            double cpu = 0.0;
            const auto y = run (*fx, 0.3, cpu);
            logMessage ("55 Hz " + juce::String (goertzel (y, 55.0), 4) + " vs 110 Hz " + juce::String (goertzel (y, 110.0), 4));
            expect (goertzel (y, 55.0) > goertzel (y, 110.0));
        }
    }
};

static OctaveSynthTests octaveSynthTests;

} // namespace openguitarmultifx
