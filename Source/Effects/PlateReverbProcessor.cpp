#include "PlateReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

PlateReverbProcessor::PlateReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "plate_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.55f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "plate_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "plate_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);

    decay = decayParam.get();
    tone = toneParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "plate", "Plate", "|",
        std::move (decayParam), std::move (toneParam), std::move (mixParam));
}

void PlateReverbProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;

    // Short, Dattorro-style input diffusion -- fast and dense, the
    // metallic character, versus Hall's longer Freeverb-scale delays.
    static constexpr float allpassTuningsMs[numAllpassStages] = { 4.7f, 3.6f, 12.7f, 9.3f };

    for (auto& channelStages : diffuser)
        for (int i = 0; i < numAllpassStages; ++i)
        {
            channelStages[(size_t) i].prepare (sampleRate, allpassTuningsMs[i]);
            channelStages[(size_t) i].feedback = 0.5f;
        }

    // Slightly different lengths per channel so the cross-fed ring doesn't
    // beat at an exact period -- part of what reads as "wide" rather than
    // a mono ring panned centre.
    tank[0].prepare (sampleRate, 29.7f);
    tank[1].prepare (sampleRate, 33.1f);

    reset();
}

void PlateReverbProcessor::reset()
{
    for (auto& channelStages : diffuser)
        for (auto& stage : channelStages)
            stage.clear();
    for (auto& line : tank)
        line.clear();
}

void PlateReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Deliberately lower than Hall's ceiling: the cross-feed already
    // circulates each channel's energy through BOTH tank lines every
    // cycle (see process() below), so the same nominal feedback value
    // rings noticeably longer here than in a single self-only comb --
    // this range was tuned down empirically to keep worst-case (loud,
    // fully decorrelated stereo noise) headroom comparable to the other
    // reverbs here rather than mirroring Hall's constants blindly.
    const float feedback = 0.4f + decay->get() * 0.15f;
    const float damp1 = 1.0f - tone->get();
    const float damp2 = tone->get();
    const float wet = mix->get();

    std::array<float*, 2> channelData {};
    for (int ch = 0; ch < numChannels; ++ch)
        channelData[(size_t) ch] = buffer.getWritePointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        std::array<float, 2> diffused { 0.0f, 0.0f };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float value = channelData[(size_t) ch][i];
            for (auto& stage : diffuser[(size_t) ch])
                value = stage.process (value);
            diffused[(size_t) ch] = value;
        }

        // Read both lines' current output before either writes -- that
        // simultaneity is what makes this a cross-feed instead of one
        // channel always hearing a one-sample-stale version of itself.
        const float outA = tank[0].readOutput();
        const float outB = numChannels > 1 ? tank[1].readOutput() : outA;

        auto& storeA = tank[0].filterStore;
        storeA = outA * damp2 + storeA * damp1;
        const float otherFeedA = numChannels > 1 ? outB : outA;
        tank[0].writeAndAdvance (diffused[0] + storeA * feedback * 0.5f + otherFeedA * feedback * 0.5f);

        if (numChannels > 1)
        {
            auto& storeB = tank[1].filterStore;
            storeB = outB * damp2 + storeB * damp1;
            tank[1].writeAndAdvance (diffused[1] + storeB * feedback * 0.5f + outA * feedback * 0.5f);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float wetSample = ch == 0 ? outA : outB;
            const float input = channelData[(size_t) ch][i];
            channelData[(size_t) ch][i] = input * (1.0f - wet) + wetSample * wet;
        }
    }
}

void PlateReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/plate.svg -- a
    // flat plate with sound arcing off its surface (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::plate_svg, IconData::plate_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
