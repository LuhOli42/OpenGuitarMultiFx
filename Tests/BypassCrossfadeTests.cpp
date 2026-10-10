#include "Effects/DelayProcessor.h"
#include "Effects/EffectProcessor.h"

namespace openguitarmultifx
{

namespace
{
    /** Flips polarity: the worst case for a hard bypass switch (the output jumps by twice the signal). */
    class PolarityProcessor : public EffectProcessor
    {
    public:
        void prepare (double, int, int) override {}
        void process (juce::AudioBuffer<float>& buffer) override { ++processCalls; buffer.applyGain (-1.0f); }
        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        const char* getName() const override { return "Polarity"; }

        int processCalls = 0;
    };

    /** Dry + one feedback echo, with trails -- a minimal delay pedal. */
    class EchoProcessor : public EffectProcessor
    {
    public:
        static constexpr int delaySamples = 2400;

        void prepare (double, int, int) override { line.assign (delaySamples, 0.0f); }
        void process (juce::AudioBuffer<float>& buffer) override
        {
            ++processCalls;
            auto* data = buffer.getWritePointer (0);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float echo = line[(size_t) pos];
                line[(size_t) pos] = data[i] + 0.5f * echo;
                data[i] += echo;
                pos = (pos + 1) % delaySamples;
            }
        }
        void reset() override {}
        juce::AudioProcessorParameterGroup* getParameters() override { return nullptr; }
        const char* getName() const override { return "Echo"; }
        bool hasTrails() const override { return true; }

        int processCalls = 0;

    private:
        std::vector<float> line;
        int pos = 0;
    };

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    const int rampSamples = (int) (EffectProcessor::bypassRampSeconds * sampleRate);

    /** Runs `numBlocks` blocks of constant `value` through `p`, appending the output to `out`. */
    void run (EffectProcessor& p, int numBlocks, float value, std::vector<float>& out)
    {
        juce::AudioBuffer<float> buffer (1, blockSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, value);
            p.processWithBypass (buffer);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
    }

    float maxStep (const std::vector<float>& signal, size_t from)
    {
        float worst = 0.0f;
        for (size_t i = juce::jmax ((size_t) 1, from); i < signal.size(); ++i)
            worst = juce::jmax (worst, std::abs (signal[i] - signal[i - 1]));
        return worst;
    }
}

class BypassCrossfadeTests : public juce::UnitTest
{
public:
    BypassCrossfadeTests() : juce::UnitTest ("BypassCrossfade", "Effects") {}

    void runTest() override
    {
        beginTest ("a processor first heard while bypassed passes the input straight through and never runs");
        {
            PolarityProcessor p;
            p.prepareIfNeeded (sampleRate, blockSize, 1);
            p.setBypassed (true);
            std::vector<float> out;
            run (p, 4, 1.0f, out);
            expectEquals (p.processCalls, 0);
            for (auto v : out)
                expectEquals (v, 1.0f);
        }

        beginTest ("bypassing crossfades over bypassRampSeconds with no jump, then stops calling process()");
        {
            PolarityProcessor p;
            p.prepareIfNeeded (sampleRate, blockSize, 1);
            std::vector<float> out;
            run (p, 2, 1.0f, out);
            expectEquals (out.back(), -1.0f);

            p.setBypassed (true);
            const auto switchedAt = out.size();
            run (p, 10, 1.0f, out);

            // A hard switch would step by 2.0 in one sample; the ramp spreads it over rampSamples.
            expectLessOrEqual (maxStep (out, switchedAt - 1), 2.0f / (float) rampSamples * 1.01f);
            expectEquals (out[switchedAt + (size_t) rampSamples], 1.0f);
            expectEquals (out.back(), 1.0f);

            const int callsAfterRamp = p.processCalls;
            run (p, 4, 1.0f, out);
            expectEquals (p.processCalls, callsAfterRamp);
        }

        beginTest ("turning a processor back on ramps in the same way");
        {
            PolarityProcessor p;
            p.prepareIfNeeded (sampleRate, blockSize, 1);
            p.setBypassed (true);
            std::vector<float> out;
            run (p, 2, 1.0f, out);

            p.setBypassed (false);
            const auto switchedAt = out.size();
            run (p, 10, 1.0f, out);
            expectLessOrEqual (maxStep (out, switchedAt - 1), 2.0f / (float) rampSamples * 1.01f);
            expectEquals (out.back(), -1.0f);
        }

        beginTest ("a processor with trails keeps its echoes after bypass, then stops running once they're silent");
        {
            EchoProcessor p;
            p.prepareIfNeeded (sampleRate, blockSize, 1);

            // One impulse, then bypass the very next block.
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (1, blockSize);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            p.processWithBypass (buffer);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));

            p.setBypassed (true);
            run (p, (int) (3.0 * sampleRate) / blockSize, 0.0f, out);

            expectWithinAbsoluteError (out[(size_t) EchoProcessor::delaySamples], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (out[(size_t) EchoProcessor::delaySamples * 2], 0.5f, 1.0e-6f);

            // ~17 echoes to fall under -100 dB (0.5^17), plus the silence hold: well inside 3 s.
            const int callsWhenSilent = p.processCalls;
            run (p, 8, 0.0f, out);
            expectEquals (p.processCalls, callsWhenSilent);
        }

        beginTest ("the real Digital Delay rings out after bypass and then goes idle");
        {
            DelayProcessor delay;
            delay.prepareIfNeeded (sampleRate, blockSize, 1);

            std::vector<float> out;
            juce::AudioBuffer<float> buffer (1, blockSize);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            delay.processWithBypass (buffer);
            delay.setBypassed (true);
            run (delay, (int) (0.5 * sampleRate) / blockSize, 0.0f, out); // longer than its default time

            float echoPeak = 0.0f;
            for (auto v : out)
                echoPeak = std::max (echoPeak, std::abs (v));
            expectGreaterThan (echoPeak, 0.05f, "the repeats should still sound with the pedal bypassed");

            std::vector<float> late;
            run (delay, (int) (30.0 * sampleRate) / blockSize, 0.0f, late);
            float latePeak = 0.0f;
            for (size_t i = late.size() - 4 * (size_t) blockSize; i < late.size(); ++i)
                latePeak = std::max (latePeak, std::abs (late[i]));
            expectEquals (latePeak, 0.0f, "once the tail is silent the bypassed delay outputs the dry input (silence)");
        }

        beginTest ("without trails the echo of a bypassed effect is cut");
        {
            struct NoTrailsEcho : EchoProcessor { bool hasTrails() const override { return false; } } p;
            p.prepareIfNeeded (sampleRate, blockSize, 1);

            std::vector<float> out;
            juce::AudioBuffer<float> buffer (1, blockSize);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            p.processWithBypass (buffer);
            p.setBypassed (true);
            run (p, 40, 0.0f, out);
            for (auto v : out)
                expectEquals (v, 0.0f);
        }
    }
};

static BypassCrossfadeTests bypassCrossfadeTests;

} // namespace openguitarmultifx
