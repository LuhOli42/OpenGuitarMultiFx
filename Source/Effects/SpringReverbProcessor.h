#pragma once

#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A physically-inspired spring-tank emulation, not a re-tuned
    juce::dsp::Reverb (that's ReverbProcessor/"Ambient" -- a smooth
    algorithmic wash, the wrong character for a spring). Signal chain:

      input -> 3 short Schroeder allpass stages (metallic dispersion/
      smearing, the "boing" character's timbre) -> a single feedback comb
      loop with a one-pole lowpass in the loop (the ring/decay, darkening
      as it dies out the way a real spring does) -> wet/dry mix.

    Decay controls the comb loop's feedback amount (how long it rings);
    Tone controls the in-loop lowpass cutoff (how bright the ring starts
    out). Fixed integer sample delays for the allpass/comb stages -- no
    fractional interpolation needed since nothing modulates their length
    (unlike DelayProcessor/TapeDelayProcessor's user-adjustable Time).
*/
class SpringReverbProcessor : public EffectProcessor
{
public:
    SpringReverbProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Spring"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2f9aa6); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    struct AllpassStage
    {
        std::vector<float> buffer;
        int pos = 0;
        float feedback = 0.6f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float process (float x) noexcept
        {
            const float delayed = buffer[(size_t) pos];
            const float y = -feedback * x + delayed;
            buffer[(size_t) pos] = x + feedback * y;
            if (++pos >= (int) buffer.size())
                pos = 0;
            return y;
        }
    };

    struct CombChannel
    {
        std::vector<float> buffer;
        int pos = 0;
        float lowpassState = 0.0f;

        void prepare (double sampleRate, float delayMs)
        {
            buffer.assign ((size_t) juce::jmax (1, (int) (delayMs * 0.001f * (float) sampleRate)), 0.0f);
            pos = 0;
            lowpassState = 0.0f;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); pos = 0; lowpassState = 0.0f; }
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* decay = nullptr;
    juce::AudioParameterFloat* tone = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    static constexpr int numAllpassStages = 3;
    // One allpass chain + comb per channel -- shared state across channels
    // would leak left/right content into each other.
    std::array<std::array<AllpassStage, numAllpassStages>, 2> allpass;
    std::array<CombChannel, 2> comb;

    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
