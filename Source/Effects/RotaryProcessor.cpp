#include "RotaryProcessor.h"
#include "FastMath.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

RotaryProcessor::RotaryProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "rotary_rate", "Rate", juce::NormalisableRange<float> (0.3f, 8.0f, 0.0f, 0.5f), 1.0f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "rotary_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "rotary_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.7f);

    rateHz = rate.get();
    depth = depthParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "rotary", "Rotary", "|",
        std::move (rate), std::move (depthParam), std::move (mixParam));
}

void RotaryProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;

    // One-pole lowpass coefficient for the crossover split -- the
    // highpass band is simply (input - lowpassed).
    crossoverCoeff = (float) (1.0 / (1.0 + (double) sampleRate / (twoPi * (double) crossoverHz)));

    smoothedRateHz.reset (sampleRate, paramRampSeconds);
    smoothedRateHz.setCurrentAndTargetValue (rateHz->get());
    smoothedDepth.reset (sampleRate, paramRampSeconds);
    smoothedDepth.setCurrentAndTargetValue (depth->get());
    smoothedMix.reset (sampleRate, paramRampSeconds);
    smoothedMix.setCurrentAndTargetValue (mix->get());

    reset();
}

void RotaryProcessor::reset()
{
    hornPhase = 0.0;
    woofPhase = 0.0;
    lowState.fill (0.0f);
}

void RotaryProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    smoothedRateHz.setTargetValue (rateHz->get());
    smoothedDepth.setTargetValue (depth->get());
    smoothedMix.setTargetValue (mix->get());
    const float rateNow = smoothedRateHz.skip (numSamples);
    const double hornPhaseInc = twoPi * (double) (rateNow * hornRateMultiplier) / currentSampleRate;
    const double woofPhaseInc = twoPi * (double) (rateNow * woofRateMultiplier) / currentSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // Same "oscillate between (1-depth) and 1.0" AM shape as
        // TremoloProcessor, just computed twice at two different rates.
        const float depthAmount = smoothedDepth.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float hornGain = 1.0f - depthAmount * 0.5f * (1.0f - fast::cosRadians (hornPhase));
        const float woofGain = 1.0f - depthAmount * 0.5f * (1.0f - fast::cosRadians (woofPhase));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float input = data[i];

            auto& lp = lowState[(size_t) ch];
            lp += crossoverCoeff * (input - lp);
            const float lowBand = lp;
            const float highBand = input - lp;

            const float rotated = lowBand * woofGain + highBand * hornGain;
            data[i] = input * (1.0f - wet) + rotated * wet;
        }

        hornPhase += hornPhaseInc;
        if (hornPhase >= twoPi)
            hornPhase -= twoPi;
        woofPhase += woofPhaseInc;
        if (woofPhase >= twoPi)
            woofPhase -= twoPi;
    }
}

void RotaryProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/rotary.svg -- a
    // single spin arrow around a centre point (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::rotary_svg, IconData::rotary_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
