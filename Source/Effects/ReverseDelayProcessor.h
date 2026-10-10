#pragma once

#include "EffectProcessor.h"

#include <array>
#include <vector>

namespace openguitarmultifx
{

/**
    Records into one chunk-length buffer while playing the PREVIOUS chunk
    back in reverse from a second buffer, swapping the two at each chunk
    boundary -- genuinely different from every other Delay processor here
    (all of which read forward from a circular line): this one only ever
    reads a completed chunk backwards, so a chunk's very first sample only
    plays once the chunk is fully recorded.

    Chunk length (Time) is only re-sampled at a chunk boundary, not mid-
    chunk -- changing it while a chunk is mid-record/playback would need to
    resize a buffer that's actively being written into, which risks either
    a click or (worse) an out-of-bounds read on the audio thread. Feedback
    feeds the reversed output back into what's currently being recorded,
    so successive chunks can build up an evolving, decaying reverse trail
    instead of the same fixed audio looping in reverse forever.
*/
class ReverseDelayProcessor : public EffectProcessor
{
public:
    ReverseDelayProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Reverse Delay"; }
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3868b0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

private:
    static constexpr float maxChunkMs = 2000.0f;
    static constexpr float minChunkMs = 100.0f;

    struct ChannelState
    {
        std::array<std::vector<float>, 2> chunkBuffers;
        int activeRecordBuffer = 0; // index into chunkBuffers currently being written
        int recordPos = 0;
        int playbackPos = 0;
        int chunkLengthSamples = 0;
    };

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* timeMs = nullptr;
    juce::AudioParameterFloat* feedback = nullptr;
    juce::AudioParameterFloat* mix = nullptr;

    std::vector<ChannelState> channels;
    double currentSampleRate = 0.0;
};

} // namespace openguitarmultifx
