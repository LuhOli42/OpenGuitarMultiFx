#include "Effects/CompressorProcessor.h"
#include "Effects/EffectProcessor.h"

namespace openguitarmultifx
{

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    const int rampSamples = (int) (EffectProcessor::paramRampSeconds * sampleRate);

    /** Looks up a float parameter on a processor's parameter group by ID. */
    juce::AudioParameterFloat* findParam (EffectProcessor& p, const char* id)
    {
        auto* group = p.getParameters();
        if (group == nullptr)
            return nullptr;
        for (auto* param : group->getParameters (true))
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                if (withId->paramID.equalsIgnoreCase (id))
                    return dynamic_cast<juce::AudioParameterFloat*> (param);
        return nullptr;
    }

    /** Pushes `numBlocks` blocks of constant `value` through `p`, appending output. */
    void run (EffectProcessor& p, int numBlocks, float value, std::vector<float>& out)
    {
        juce::AudioBuffer<float> buffer (1, blockSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, value);
            p.process (buffer);
            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }
    }

    float maxStep (const std::vector<float>& signal, size_t from, size_t to)
    {
        float worst = 0.0f;
        for (size_t i = juce::jmax ((size_t) 1, from); i < juce::jmin (to, signal.size()); ++i)
            worst = juce::jmax (worst, std::abs (signal[i] - signal[i - 1]));
        return worst;
    }
}

class ParamSmoothingTests : public juce::UnitTest
{
public:
    ParamSmoothingTests() : juce::UnitTest ("Parameter smoothing", "effects") {}

    void runTest() override
    {
        // CompressorProcessor is the cleanest probe: on a constant input below
        // threshold its output is input * makeupGain exactly, so a makeup jump
        // maps 1:1 onto a gain jump.
        constexpr float input = 0.25f;
        constexpr float makeupTargetDb = 24.0f;
        const float targetGain = juce::Decibels::decibelsToGain (makeupTargetDb);
        const float targetOut = input * targetGain;
        // What the output would jump by in ONE sample without the ramp.
        const float instantStep = targetOut - input;

        beginTest ("a knob jump ramps over the parameter ramp instead of stepping");
        {
            CompressorProcessor p;
            auto* makeup = findParam (p, "comp_makeup");
            auto* threshold = findParam (p, "comp_threshold");
            expect (makeup != nullptr && threshold != nullptr, "makeup/threshold params not found");
            *threshold = 0.0f;  // input stays below threshold -> no gain reduction
            p.prepareIfNeeded (sampleRate, blockSize, 1);

            std::vector<float> out;
            run (p, 4, input, out);  // settle at makeup = 0 dB

            *makeup = makeupTargetDb;
            const size_t jumpAt = out.size();
            run (p, 2, input, out);

            const float stepDuringRamp = maxStep (out, jumpAt, out.size());
            expect (stepDuringRamp < instantStep * 0.05f,
                    "per-sample step during ramp (" + juce::String (stepDuringRamp)
                        + ") should be far below the instant jump (" + juce::String (instantStep) + ")");
            // First block after the jump must still be far from the target --
            // an instant apply would already sit on it.
            expect (std::abs (out[jumpAt + blockSize - 1] - targetOut) > instantStep * 0.5f,
                    "first block after a knob jump applied the value instantly");
        }

        beginTest ("the ramp converges onto the target value");
        {
            CompressorProcessor p;
            auto* makeup = findParam (p, "comp_makeup");
            auto* threshold = findParam (p, "comp_threshold");
            *threshold = 0.0f;
            *makeup = makeupTargetDb;
            p.prepareIfNeeded (sampleRate, blockSize, 1);

            std::vector<float> out;
            run (p, 1 + rampSamples / blockSize + 4, input, out);  // ramp + margin

            float lateMean = 0.0f;
            for (size_t i = out.size() - (size_t) blockSize; i < out.size(); ++i)
                lateMean += out[i];
            lateMean /= (float) blockSize;
            expectWithinAbsoluteError (lateMean, targetOut, targetOut * 0.02f,
                                       "output did not converge to the smoothed makeup gain");

            // No double-smoothing: once converged the output is dead flat --
            // a ramp riding on top of another ramp would keep drifting.
            expect (maxStep (out, out.size() - (size_t) blockSize, out.size()) < 1.0e-5f,
                    "output kept moving after the ramp should have ended (double-smoothing?)");
        }

        beginTest ("state set before prepare applies instantly (preset load)");
        {
            CompressorProcessor p;
            auto* makeup = findParam (p, "comp_makeup");
            auto* threshold = findParam (p, "comp_threshold");
            *threshold = 0.0f;
            *makeup = makeupTargetDb;
            p.prepareIfNeeded (sampleRate, blockSize, 1);  // snap happens here

            std::vector<float> out;
            run (p, 1, input, out);

            // The very first block must already sit on the preset value --
            // ramping in from a default would make every preset load sweep.
            expectWithinAbsoluteError (out[(size_t) blockSize - 1], targetOut, targetOut * 0.05f,
                                       "preset-load value ramped in instead of applying instantly");
        }
    }
};

static ParamSmoothingTests paramSmoothingTests;

} // namespace openguitarmultifx
