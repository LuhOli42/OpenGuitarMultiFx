#include "PhaserProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    constexpr double pi = juce::MathConstants<double>::pi;
}

PhaserProcessor::PhaserProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "phaser_rate", "Rate", juce::NormalisableRange<float> (0.02f, 2.0f, 0.0f, 0.5f), 0.3f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "phaser_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f);
    auto fb = std::make_unique<juce::AudioParameterFloat> (
        "phaser_feedback", "Feedback", juce::NormalisableRange<float> (0.0f, 0.9f), 0.3f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "phaser_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    rateHz = rate.get();
    depth = depthParam.get();
    feedback = fb.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "phaser", "Phaser", "|",
        std::move (rate), std::move (depthParam), std::move (fb), std::move (mixParam));
}

void PhaserProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedFeedback.reset (sampleRate, paramRampSeconds);
    smoothedFeedback.setCurrentAndTargetValue (feedback->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void PhaserProcessor::reset()
{
    for (auto& cascade : stages)
        for (auto& stage : cascade)
            stage.clear();
    lastCascadeOutput.fill (0.0f);
    lfoPhase = 0.0;
}

void PhaserProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedFeedback.setTargetValue (feedback->get());
    smoothedMix.setTargetValue (mix->get());
    const double phaseIncrement = twoPi * (double) smoothedRateHz.skip (numSamples) / currentSampleRate;

    constexpr float centreFreq = (minFreqHz + maxFreqHz) * 0.5f;
    constexpr float halfRange = (maxFreqHz - minFreqHz) * 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float depthAmount = smoothedDepth.getNextValue();
        const float fb = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float freqHz = centreFreq + halfRange * depthAmount * fast::sinRadians (lfoPhase);
        const float tanTerm = (float) fast::tanAllpass (pi * (double) freqHz / currentSampleRate);
        const float coeff = (tanTerm - 1.0f) / (tanTerm + 1.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& cascade = stages[(size_t) ch];
            auto& lastOut = lastCascadeOutput[(size_t) ch];

            const float input = data[i];
            float v = input + fb * lastOut;
            for (auto& stage : cascade)
                v = stage.process (v, coeff);
            lastOut = v;

            data[i] = input * (1.0f - wet) + v * wet;
        }

        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void PhaserProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/phaser.svg -- a
    // solid wave and its dotted, vertically-inverted mirror (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::phaser_svg, IconData::phaser_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
