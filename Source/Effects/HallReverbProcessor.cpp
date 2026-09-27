#include "HallReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr float stereoSpreadMs = 23.0f / 44.1f;
}

HallReverbProcessor::HallReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "hall_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);
    auto toneParam = std::make_unique<juce::AudioParameterFloat> (
        "hall_tone", "Tone", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "hall_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.35f);

    decay = decayParam.get();
    tone = toneParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "hall", "Hall", "|",
        std::move (decayParam), std::move (toneParam), std::move (mixParam));
}

void HallReverbProcessor::prepare (double sampleRate, int, int)
{
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 0.31f);
    currentSampleRate = sampleRate;

    // Classic Freeverb comb/allpass tunings (ms at 44.1kHz, scaled to the
    // actual sample rate below). The right channel gets a small offset on
    // each so left/right don't ring in exact lockstep -- that stereo
    // decorrelation is most of what makes a hall sound wide instead of a
    // mono echo panned centre.
    static constexpr float combTuningsMs[numCombs] =
    {
        1116.0f / 44.1f, 1188.0f / 44.1f, 1277.0f / 44.1f, 1356.0f / 44.1f,
        1422.0f / 44.1f, 1491.0f / 44.1f, 1557.0f / 44.1f, 1617.0f / 44.1f
    };
    static constexpr float allpassTuningsMs[numAllpassStages] =
    {
        556.0f / 44.1f, 441.0f / 44.1f, 341.0f / 44.1f, 225.0f / 44.1f
    };

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

void HallReverbProcessor::reset()
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

void HallReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Same decay/tone -> feedback/damping mapping style as SpringReverbProcessor,
    // just applied across 8 combs instead of 1: long, bright-capable tails
    // without needing separate scale/offset constants per parameter.
    const float feedback = 0.7f + decay->get() * 0.28f;
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

void HallReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/hall.svg -- the
    // unified icon set's "Hall" glyph (Reverb category), added to the
    // sheet but left unused until this processor existed.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::hall_svg, IconData::hall_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
