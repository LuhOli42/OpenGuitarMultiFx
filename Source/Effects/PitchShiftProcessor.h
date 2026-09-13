#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A fixed, user-set pitch shift (+/-24 semitones) via the same two-grain
    time-domain shifter PitchModProcessor generalises from
    ShimmerReverbProcessor -- but with a STATIC ratio (no LFO) and the
    standard dry/wet crossfade Mix, so at full wet the output is simply
    the input transposed to a new fixed pitch. That crossfade-to-replace
    behaviour is what distinguishes this from OctaverProcessor and
    HarmonizerProcessor, which both always keep the dry signal fully
    present and only ADD a shifted layer underneath -- a pitch shifter is
    meant to be able to fully replace the original note, an octaver/
    harmonizer never should.
*/
class PitchShiftProcessor : public EffectProcessor
{
public:
    PitchShiftProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Pitch Shift"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a9e5c); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct GrainShifter
    {
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float grainSamples = 1.0f;
        float delayA = 0.0f;
        float delayB = 0.0f;

        void prepare (double sampleRate)
        {
            grainSamples = (float) (0.05 * sampleRate);
            bufferSize = (int) grainSamples * 2 + 8;
            buffer.assign ((size_t) bufferSize, 0.0f);
            clear();
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
            delayA = grainSamples * 0.5f;
            delayB = 0.0f;
        }

        static float windowGain (float delay, float grain) noexcept
        {
            const float t = delay / grain;
            return 1.0f - std::abs (2.0f * t - 1.0f);
        }

        float readAt (float delaySamples) const noexcept
        {
            float readPos = (float) writePos - delaySamples;
            while (readPos < 0.0f)
                readPos += (float) bufferSize;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1 >= bufferSize) ? 0 : i0 + 1;
            const float frac = readPos - (float) i0;
            return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
        }

        float process (float input, float ratio) noexcept
        {
            buffer[(size_t) writePos] = input;

            const float outA = readAt (delayA) * windowGain (delayA, grainSamples);
            const float outB = readAt (delayB) * windowGain (delayB, grainSamples);

            const float step = ratio - 1.0f;
            delayA -= step;
            if (delayA <= 0.0f)
                delayA += grainSamples;
            else if (delayA >= grainSamples)
                delayA -= grainSamples;

            delayB -= step;
            if (delayB <= 0.0f)
                delayB += grainSamples;
            else if (delayB >= grainSamples)
                delayB -= grainSamples;

            if (++writePos >= bufferSize)
                writePos = 0;
            return outA + outB;
        }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* semitones = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<GrainShifter, 2> shifters;
    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
