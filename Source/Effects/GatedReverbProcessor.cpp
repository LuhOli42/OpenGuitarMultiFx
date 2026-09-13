#include "GatedReverbProcessor.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr float stereoSpreadMs = 19.0f / 44.1f;
    constexpr float thresholdDb = -35.0f;
}

GatedReverbProcessor::GatedReverbProcessor()
{
    auto decayParam = std::make_unique<juce::AudioParameterFloat> (
        "gatedreverb_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);
    auto holdParam = std::make_unique<juce::AudioParameterFloat> (
        "gatedreverb_hold", "Hold",
        juce::NormalisableRange<float> (10.0f, 500.0f, 0.0f, 0.5f), 150.0f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "gatedreverb_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    decay = decayParam.get();
    holdMs = holdParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "gatedreverb", "Gated", "|",
        std::move (decayParam), std::move (holdParam), std::move (mixParam));
}

void GatedReverbProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;

    // Short, Room-scale tunings, not Hall's ~25-35ms set -- a real gated
    // reverb needs to build up fast (its whole character depends on being
    // audibly full within the first several ms, since Hold is typically
    // short too) rather than slowly like a hall. High feedback (see
    // process()) still gives it a lush, dense wash despite the short taps.
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

    // Fast ballistic tracking (matching GateProcessor's own detector
    // convention) -- a slow release here would keep re-arming Hold for as
    // long as the detector's own envelope takes to decay past threshold,
    // on top of Hold itself, making the gate close far later than Hold
    // alone would suggest.
    inputDetector.prepare (sampleRate);
    inputDetector.setAttackTime (1.0f);
    inputDetector.setReleaseTime (2.0f);

    gateSmoother.prepare (sampleRate);
    gateSmoother.setAttackTime (1.0f);
    gateSmoother.setReleaseTime (12.0f);

    reset();
}

void GatedReverbProcessor::reset()
{
    for (auto& bank : combs)
        for (auto& c : bank)
            c.clear();
    for (auto& bank : allpass)
        for (auto& a : bank)
            a.clear();

    inputDetector.reset();
    gateSmoother.reset();
    holdRemainingSamples = 0.0;
    gateOpen = false;
}

void GatedReverbProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    const float feedback = 0.75f + decay->get() * 0.2f;
    const float wet = mix->get();
    const float thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb);
    const double holdSamples = (double) holdMs->get() * 0.001 * currentSampleRate;

    for (auto& bank : combs)
        for (auto& c : bank)
            c.feedback = feedback;

    std::array<const float*, 2> channelReadData {};
    for (int ch = 0; ch < numChannels; ++ch)
        channelReadData[(size_t) ch] = buffer.getReadPointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        // Trigger off the loudest channel's dry input -- the same
        // peak-across-channels convention GateProcessor uses, so a mono
        // pluck on one channel still opens the gate.
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, std::abs (channelReadData[(size_t) ch][i]));

        const float detected = inputDetector.processSample (peak);
        if (detected >= thresholdLinear)
        {
            gateOpen = true;
            holdRemainingSamples = holdSamples;
        }
        else if (holdRemainingSamples > 0.0)
        {
            holdRemainingSamples -= 1.0;
        }
        else
        {
            gateOpen = false;
        }

        const float gateGain = gateSmoother.processSample (gateOpen ? 1.0f : 0.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& combBank = combs[(size_t) ch];
            auto& allpassBank = allpass[(size_t) ch];

            const float input = data[i];

            float combSum = 0.0f;
            for (auto& c : combBank)
                combSum += c.process (input);
            combSum /= (float) numCombs;

            float diffused = combSum;
            for (auto& a : allpassBank)
                diffused = a.process (diffused);

            data[i] = input * (1.0f - wet) + (diffused * gateGain) * wet;
        }
    }
}

void GatedReverbProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/gated.svg -- a
    // tail cut off mid-decay by a hard bar (Reverb category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::gated_svg, IconData::gated_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
