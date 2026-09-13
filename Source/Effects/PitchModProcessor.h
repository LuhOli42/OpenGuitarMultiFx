#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    True pitch-bending, not a modulated delay line. The two-grain
    time-domain shifter from ShimmerReverbProcessor::OctaveUpShifter,
    generalised to a continuously-variable ratio instead of a fixed
    octave: an LFO sweeps the playback ratio up and down (musically, in
    semitones) each sample, so what comes out is genuinely re-pitched --
    Chorus/Vibrato/Flanger only ever LOOK like pitch modulation as a side
    effect of a moving delay time, which is audibly gentler and can't
    cover a wide musical interval the way real pitch-shifting can.
*/
class PitchModProcessor : public EffectProcessor
{
public:
    PitchModProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Pitch Mod"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7a5cc9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    // Two-grain doubler generalised to a continuously variable ratio --
    // see the class doc comment. `delayA -= (ratio - 1)` per sample: at
    // ratio=1 the delay never moves (no shift); ratio>1 decreases delay
    // (reading forward through history faster = pitch up); ratio<1
    // increases it (pitch down). Wrapping in BOTH directions is needed
    // here, unlike the fixed-ratio-always->1 case in ShimmerReverbProcessor.
    struct VariableRateShifter
    {
        std::vector<float> buffer;
        int bufferSize = 0;
        int writePos = 0;
        float grainSamples = 1.0f;
        float delayA = 0.0f;
        float delayB = 0.0f;

        void prepare (double sampleRate)
        {
            grainSamples = (float) (0.05 * sampleRate); // 50ms grain
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

    static constexpr float maxSemitones = 3.0f;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateHz = nullptr;
    juce::AudioParameterFloat* depth = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<VariableRateShifter, 2> shifters;

    double currentSampleRate = 0.0;
    double lfoPhase = 0.0;
};

} // namespace openguitarmultifx
