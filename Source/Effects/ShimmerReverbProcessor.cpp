#include "ShimmerReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr float stereoSpreadMs = 17.0f / 44.1f;
}

ShimmerReverbProcessor::ShimmerReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "shimmer_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.55f);
    auto shimmerParam = std::make_unique<juce::AudioParameterFloat> (
        "shimmer_amount", "Shimmer", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "shimmer_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.4f);

    decay = decayParam.get();
    shimmerAmount = shimmerParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "shimmer", "Shimmer", "|",
        std::move (decayParam), std::move (shimmerParam), std::move (mixParam));
}

void ShimmerReverbProcessor::prepare (double sampleRate, int, int)
{
    for (auto& m : wetMatch)
        m.prepare (sampleRate, 0.8f);
    currentSampleRate = sampleRate;

    static constexpr float combTuningsMs[numCombs] = { 1116.0f / 44.1f, 1277.0f / 44.1f, 1422.0f / 44.1f, 1557.0f / 44.1f };
    static constexpr float allpassTuningsMs[numAllpassStages] = { 441.0f / 44.1f, 225.0f / 44.1f };

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

        shifters[(size_t) ch].prepare (sampleRate);
    }

    reset();
}

void ShimmerReverbProcessor::reset()
{
    for (auto& m : wetMatch)
        m.reset();
    for (auto& bank : combs)
        for (auto& c : bank)
            c.clear();
    for (auto& bank : allpass)
        for (auto& a : bank)
            a.clear();
    for (auto& s : shifters)
        s.clear();
    lastCombSum.fill (0.0f);
}

void ShimmerReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Both ceilings deliberately leave headroom for each other -- see
    // CombFilter::process's doc comment -- so their worst-case sum
    // (0.85 + 0.1 = 0.95) stays a safe margin under 1 regardless of how
    // Decay and Shimmer are set together.
    const float feedback = 0.6f + decay->get() * 0.25f;
    const float shimmerGain = shimmerAmount->get() * 0.1f;
    const float wet = mix->get();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& combBank = combs[(size_t) ch];
        auto& allpassBank = allpass[(size_t) ch];
        auto& shifter = shifters[(size_t) ch];
        auto& lastSum = lastCombSum[(size_t) ch];

        for (auto& c : combBank)
            c.feedback = feedback;

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];

            // Fed from the PREVIOUS sample's comb sum (a one-sample-old
            // feedback tap), not this sample's -- the alternative would
            // need every comb's output before any comb can be written,
            // which isn't possible in a single pass. One sample of extra
            // latency in the shimmer path is inaudible.
            const float shifted = shifter.process (lastSum);

            float combSum = 0.0f;
            for (auto& c : combBank)
                combSum += c.process (input, shifted, shimmerGain);
            combSum /= (float) numCombs;
            lastSum = combSum;

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

void ShimmerReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/shimmer.svg -- a
    // wash with a sparkle above it (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::shimmer_svg, IconData::shimmer_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
