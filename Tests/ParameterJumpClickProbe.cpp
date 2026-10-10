#include "EffectRegistry.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace openguitarmultifx
{

/** Dev-only probe (CLICK_PROBE=all, or one registry key): which knobs click when their value JUMPS, the way a snapshot
    switch moves them (docs/PresetsAndSnapshots.md, stage 2). For every float parameter: play a 220 Hz sine, jump the
    knob to the far end of its range and back, and compare the peak second difference (a click detector -- a step or
    a kink shows up there, a smooth change does not) right after each jump with the same measure on the settled sound
    on both sides. Prints one line per knob, worst first; ratio > clickRatioThreshold = audible click. */
class ParameterJumpClickProbe : public juce::UnitTest
{
public:
    ParameterJumpClickProbe() : juce::UnitTest ("ParameterJumpClickProbe", "Probe") {}

    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 128;
    static constexpr float clickRatioThreshold = 3.0f;

    void runTest() override
    {
        const char* which = std::getenv ("CLICK_PROBE");
        if (which == nullptr)
            return;

        beginTest (juce::String ("parameter jump clicks: ") + which);
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        struct Row { juce::String key, param; float ratio; bool selector; };
        std::vector<Row> rows;

        for (auto& key : registry.getRegisteredNames())
        {
            if (juce::String (which) != "all" && key != which)
                continue;

            auto probe = registry.create (key);
            if (probe == nullptr || probe->getParameters() == nullptr)
                continue;

            const auto params = probe->getParameters()->getParameters (true);
            for (int index = 0; index < params.size(); ++index)
            {
                auto* shape = dynamic_cast<juce::AudioParameterFloat*> (params[index]);
                if (shape == nullptr)
                    continue;

                // A fresh processor per knob: one knob's jump must not colour the next one's baseline.
                auto fx = registry.create (key);
                auto* param = dynamic_cast<juce::AudioParameterFloat*> (fx->getParameters()->getParameters (true)[index]);
                const float ratio = measure (*fx, *param);
                const auto& range = param->getNormalisableRange();
                const bool selector = range.interval >= 1.0f && (range.end - range.start) / range.interval <= 8.0f;
                rows.push_back ({ key, param->paramID, ratio, selector });
            }
        }

        std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.ratio > b.ratio; });

        int clicking = 0;
        for (auto& r : rows)
        {
            const bool clicks = r.ratio > clickRatioThreshold;
            clicking += clicks ? 1 : 0;
            logMessage (juce::String (clicks ? "CLICK " : "ok    ") + juce::String (r.ratio, 1).paddedLeft (' ', 7) + "  "
                        + r.key + " / " + r.param + (r.selector ? "  (selector)" : ""));
        }
        logMessage (juce::String (clicking) + " of " + juce::String ((int) rows.size()) + " knobs click on a jump");
    }

private:
    /** Worst ratio of the two jumps (out to the far end, then back). */
    static float measure (EffectProcessor& fx, juce::AudioParameterFloat& param)
    {
        fx.prepare (sampleRate, blockSize, 2);
        const float home = param.get();
        const auto& range = param.getNormalisableRange();
        const float far = range.convertTo0to1 (home) < 0.5f ? range.end : range.start;

        double phase = 0.0;
        auto runSeconds = [&] (double seconds, std::vector<float>& out)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float s = 0.25f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * 220.0 / sampleRate;
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }
                fx.process (buffer);
                for (int i = 0; i < blockSize; ++i)
                    out.push_back (buffer.getSample (0, i));
            }
        };
        auto roughness = [] (const std::vector<float>& x)
        {
            float worst = 0.0f;
            for (size_t i = 2; i < x.size(); ++i)
                if (std::isfinite (x[i]))
                    worst = std::max (worst, std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]));
            return worst;
        };

        std::vector<float> settleA, before, jumpOut, settleB, middle, jumpBack, settleC, after;
        runSeconds (0.5, settleA);
        runSeconds (0.25, before);
        param = far;
        runSeconds (0.04, jumpOut);
        runSeconds (0.5, settleB);
        runSeconds (0.25, middle);
        param = home;
        runSeconds (0.04, jumpBack);
        runSeconds (0.5, settleC);
        runSeconds (0.25, after);

        // Prepend the last two settled samples so the jump's very first sample is compared against what preceded it.
        auto withLead = [] (const std::vector<float>& lead, std::vector<float> x)
        {
            x.insert (x.begin(), lead.end() - 2, lead.end());
            return x;
        };

        // CLICK_PROBE_DUMP=<paramID>: write the samples around both jumps to a CSV next to the binary, to look at.
        if (const char* dump = std::getenv ("CLICK_PROBE_DUMP"); dump != nullptr && param.paramID == dump)
        {
            juce::File csv = juce::File::getCurrentWorkingDirectory().getChildFile (param.paramID + ".csv");
            juce::String text;
            auto add = [&text] (const char* tag, const std::vector<float>& x, size_t from, size_t to)
            {
                for (size_t i = from; i < std::min (to, x.size()); ++i)
                    text << tag << "," << (int) i << "," << x[i] << "\n";
            };
            add ("before", before, before.size() - 256, before.size());
            add ("jumpOut", jumpOut, 0, jumpOut.size());
            add ("middle", middle, middle.size() - 256, middle.size());
            add ("jumpBack", jumpBack, 0, jumpBack.size());
            csv.replaceWithText (text);
        }

        const float floorOut = std::max ({ roughness (before), roughness (middle), 1.0e-4f });
        const float floorBack = std::max ({ roughness (middle), roughness (after), 1.0e-4f });
        return std::max (roughness (withLead (before, jumpOut)) / floorOut,
                         roughness (withLead (middle, jumpBack)) / floorBack);
    }
};

static ParameterJumpClickProbe parameterJumpClickProbe;

} // namespace openguitarmultifx
