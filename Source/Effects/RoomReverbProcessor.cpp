#include "RoomReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr float stereoSpreadMs = 11.0f / 44.1f;
}

RoomReverbProcessor::RoomReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "room_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "room_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "room_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f);

    decay = decayParam.get();
    tone = toneParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "room", "Room", "|",
        std::move (decayParam), std::move (toneParam), std::move (mixParam));
}

void RoomReverbProcessor::prepare (double sampleRate, int, int)
{
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 0.85f);
    currentSampleRate = sampleRate;

    // Short early-reflection-scale tunings -- a small room's own dimensions,
    // not Hall's cathedral-scale delays.
    static constexpr float combTuningsMs[numCombs] = { 7.1f, 9.8f, 13.3f, 16.6f };
    static constexpr float allpassTuningsMs[numAllpassStages] = { 3.1f, 1.7f };

    for (int ch = 0; ch < 2; ++ch)
    {
        const float spread = ch == 0 ? 0.0f : stereoSpreadMs;

        for (int i = 0; i < numCombs; ++i)
            combs[(size_t) ch][(size_t) i].prepare (sampleRate, combTuningsMs[i] + spread);

        for (int i = 0; i < numAllpassStages; ++i)
        {
            allpass[(size_t) ch][(size_t) i].prepare (sampleRate, allpassTuningsMs[i] + spread);
            allpass[(size_t) ch][(size_t) i].feedback = 0.5f;
        }
    }

    reset();
}

void RoomReverbProcessor::reset()
{
    for (auto& m : wetMatch)
        m.reset();
    for (auto& bank : combs)
        for (auto& c : bank)
            c.clear();
    for (auto& bank : allpass)
        for (auto& a : bank)
            a.clear();
}

void RoomReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Capped well below Hall's 0.98 -- a real small room's reflections die
    // out in a few hundred ms, not seconds.
    const float feedback = 0.5f + decay->get() * 0.4f;
    const float damp1 = tone->get();
    const float damp2 = 1.0f - damp1;
    const float wet = mix->get();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& combBank = combs[(size_t) ch];
        auto& allpassBank = allpass[(size_t) ch];

        for (auto& c : combBank)
        {
            c.feedback = feedback;
            c.damp1 = damp1;
            c.damp2 = damp2;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];

            float combSum = 0.0f;
            for (auto& c : combBank)
                combSum += c.process (input);
            combSum /= (float) numCombs;

            float diffused = combSum;
            for (auto& a : allpassBank)
                diffused = a.process (diffused);

            wetMatch[(size_t) ch].accumulate (input, diffused);
            data[i] = input * (1.0f - wet) + diffused * wet * wetMatch[(size_t) ch].gain();
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
        wetMatch[(size_t) ch].endBlock (numSamples, 1);
}

void RoomReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/room.svg -- nested
    // boxes tightening toward the centre (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::room_svg, IconData::room_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
