#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    A dense-diffusion-into-a-cross-coupled-tank design, structurally
    different from every other reverb here (Hall/Room/Gated/Shimmer are
    per-channel independent parallel comb banks; Spring is a single
    per-channel comb): the input runs through 4 short series allpass
    stages per channel (denser, shorter diffusion than Hall's -- a plate's
    diffusion is fast and metallic, not spacious), then into ONE damped
    delay line per channel where each channel's line also reads a decayed
    copy of the OTHER channel's line. That left/right cross-feed is what
    gives a real plate its bright, tightly-coupled stereo ring, instead of
    two reverbs running independently side by side.
*/
class PlateReverbProcessor : public EffectProcessor
{
public:
    PlateReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Plate"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct AllpassFilter
    {
        std::vector<float> buffer;
        int pos = 0;
        float feedback = 0.7f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float process (float input) noexcept
        {
            const float bufOut = buffer[(size_t) pos];
            const float output = -input + bufOut;
            buffer[(size_t) pos] = input + bufOut * feedback;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return output;
        }
    };

    struct TankLine
    {
        std::vector<float> buffer;
        int pos = 0;
        float filterStore = 0.0f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
            filterStore = 0.0f;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; filterStore = 0.0f; }

        /** Reads the current output without advancing -- callers on both
            channels need to read each other's output before either writes. */
        float readOutput() const noexcept { return buffer[(size_t) pos]; }

        void writeAndAdvance (float valueToStore) noexcept
        {
            buffer[(size_t) pos] = valueToStore;
            if (++pos >= (int) buffer.size())
                pos = 0;
        }
    };

    static constexpr int numAllpassStages = 4;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::array<std::array<AllpassFilter, numAllpassStages>, 2> diffuser;
    std::array<TankLine, 2> tank;

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
