#include "ReverseDelayProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

ReverseDelayProcessor::ReverseDelayProcessor()
{
    auto time = std::make_unique<juce::AudioParameterFloat> (
        "reversedelay_time", "Time",
        juce::NormalisableRange<float> (minChunkMs, maxChunkMs, 0.0f, 0.5f), 500.0f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "reversedelay_feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.9f), 0.3f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "reversedelay_mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    timeMs = time.get();
    feedback = fb.get();
    mix = mixParam.get();
    registerTempoSyncParam (timeMs);

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "reversedelay", "Reverse Delay", "|",
        std::move (time), std::move (fb), std::move (mixParam));
}

void ReverseDelayProcessor::prepare (double sampleRate, int, int numChannels)
{
    currentSampleRate = sampleRate;

    const int maxChunkSamples = (int) std::ceil (maxChunkMs * 0.001 * sampleRate) + 4;
    const int initialChunkSamples = juce::jlimit (1, maxChunkSamples,
        (int) (juce::jlimit (minChunkMs, maxChunkMs, timeMs->get()) * 0.001f * (float) sampleRate));

    channels.assign ((size_t) juce::jmax (1, numChannels), ChannelState {});
    for (auto& c : channels)
    {
        c.chunkBuffers[0].assign ((size_t) maxChunkSamples, 0.0f);
        c.chunkBuffers[1].assign ((size_t) maxChunkSamples, 0.0f);
        c.activeRecordBuffer = 0;
        c.recordPos = 0;
        c.chunkLengthSamples = initialChunkSamples;
        c.playbackPos = c.chunkLengthSamples - 1;
    }
}

void ReverseDelayProcessor::reset()
{
    for (auto& c : channels)
    {
        std::fill (c.chunkBuffers[0].begin(), c.chunkBuffers[0].end(), 0.0f);
        std::fill (c.chunkBuffers[1].begin(), c.chunkBuffers[1].end(), 0.0f);
        c.activeRecordBuffer = 0;
        c.recordPos = 0;
        c.playbackPos = c.chunkLengthSamples - 1;
    }
}

void ReverseDelayProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0 || channels.empty())
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
    const int numSamples = buffer.getNumSamples();
    const float fb = feedback->get();
    const float wetAmount = mix->get();
    const int maxChunkSamples = (int) channels[0].chunkBuffers[0].size();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& c = channels[(size_t) ch];

        for (int i = 0; i < numSamples; ++i)
        {
            auto& playbackBuffer = c.chunkBuffers[(size_t) (1 - c.activeRecordBuffer)];
            const float wet = (c.playbackPos >= 0 && c.playbackPos < (int) playbackBuffer.size())
                                   ? playbackBuffer[(size_t) c.playbackPos]
                                   : 0.0f;

            const float input = data[i];

            auto& recordBuffer = c.chunkBuffers[(size_t) c.activeRecordBuffer];
            if (c.recordPos < (int) recordBuffer.size())
                recordBuffer[(size_t) c.recordPos] = input + fb * wet;

            data[i] = input * (1.0f - wetAmount) + wet * wetAmount;

            ++c.recordPos;
            --c.playbackPos;

            if (c.recordPos >= c.chunkLengthSamples)
            {
                // Chunk boundary: what we just finished recording becomes
                // the next chunk to play back in reverse; re-sample Time
                // now (not mid-chunk) so a knob twist can't corrupt an
                // in-progress chunk.
                c.activeRecordBuffer = 1 - c.activeRecordBuffer;
                c.recordPos = 0;
                c.chunkLengthSamples = juce::jlimit (1, maxChunkSamples,
                    (int) (juce::jlimit (minChunkMs, maxChunkMs, timeMs->get()) * 0.001f * (float) currentSampleRate));
                c.playbackPos = c.chunkLengthSamples - 1;
            }
        }
    }
}

void ReverseDelayProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/reverse_delay.svg
    // -- the unified icon set's "Reverse Delay" glyph (Delay category).
    static const std::unique_ptr<juce::Drawable> svg =
        icon::loadSvg (IconData::reverse_delay_svg, IconData::reverse_delay_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
