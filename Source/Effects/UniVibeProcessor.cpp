#include "UniVibeProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr double pi = juce::MathConstants<double>::pi;

    /**
        Asymmetric shaping: a plain sine plus a smaller second harmonic,
        renormalised back to [-1, 1]. Rises through zero faster than it
        falls -- the lopsided response of a real photocell/lamp circuit,
        versus PhaserProcessor's clean symmetric sine.
    */
    float asymmetricLfo (double phase) noexcept
    {
        const float raw = (float) (std::sin (phase) + 0.35 * std::sin (2.0 * phase));
        constexpr float normalise = 1.0f / 1.35f;
        return raw * normalise;
    }
}

UniVibeProcessor::UniVibeProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "univibe_rate", "Rate", juce::NormalisableRange<float> (0.1f, 6.0f, 0.0f, 0.5f), 0.6f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "univibe_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "univibe_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "univibe", "Uni-Vibe", "|",
        std::move (rate), std::move (depthParam), std::move (mixParam));
}

void UniVibeProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void UniVibeProcessor::reset()
{
    for (auto& cascade : stages)
        for (auto& stage : cascade)
            stage.clear();
    lfoPhase = 0.0;
}

void UniVibeProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedMix.setTargetValue (mix->get());
    const double phaseIncrement = twoPi * (double) smoothedRateHz.skip (numSamples) / currentSampleRate;

    constexpr float centreFreq = (minFreqHz + maxFreqHz) * 0.5f;
    constexpr float halfRange = (maxFreqHz - minFreqHz) * 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float depthAmount = smoothedDepth.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float lfoValue = asymmetricLfo (lfoPhase);
        const float freqHz = centreFreq + halfRange * depthAmount * lfoValue;
        const float tanTerm = (float) std::tan (pi * (double) freqHz / currentSampleRate);
        const float coeff = (tanTerm - 1.0f) / (tanTerm + 1.0f);

        // The lamp's brightness throbs the signal's level too, not just
        // its phase -- a subtle, shallow throb (max ~15% at full depth),
        // synced to the same asymmetric LFO.
        const float throbGain = 1.0f - depthAmount * 0.15f * (0.5f - 0.5f * lfoValue);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& cascade = stages[(size_t) ch];

            const float input = data[i];
            float v = input;
            for (auto& stage : cascade)
                v = stage.process (v, coeff);

            data[i] = (input * (1.0f - wet) + v * wet) * throbGain;
        }

        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void UniVibeProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/uni_vibe.svg -- a
    // plain circle with a glowing centre (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::uni_vibe_svg, IconData::uni_vibe_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
