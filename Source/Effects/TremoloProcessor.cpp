#include "TremoloProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <array>
#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

TremoloProcessor::TremoloProcessor()
{
    auto rate = std::make_unique<juce::AudioParameterFloat> (
        "tremolo_rate", "Rate", juce::NormalisableRange<float> (0.1f, 12.0f, 0.0f, 0.5f), 4.0f);
    auto depthParam = std::make_unique<juce::AudioParameterFloat> (
        "tremolo_depth", "Depth", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);

    rateHz = rate.get();
    depth = depthParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "tremolo", "Tremolo", "|",
        std::move (rate), std::move (depthParam));
}

void TremoloProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    reset();
}

void TremoloProcessor::reset()
{
    lfoPhase = 0.0;
}

void TremoloProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const double phaseIncrement = twoPi * (double) rateHz->get() / currentSampleRate;
    const float depthAmount = depth->get();

    std::array<float*, 2> channelData {};
    const int cachedChannels = juce::jmin (numChannels, 2);
    for (int ch = 0; ch < cachedChannels; ++ch)
        channelData[(size_t) ch] = buffer.getWritePointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        // Oscillates between (1-depth) and 1.0 -- full depth reaches
        // silence at the trough, zero depth leaves the signal untouched.
        const float gain = 1.0f - depthAmount * 0.5f * (1.0f - (float) std::cos (lfoPhase));

        for (int ch = 0; ch < cachedChannels; ++ch)
            channelData[(size_t) ch][i] *= gain;

        lfoPhase += phaseIncrement;
        if (lfoPhase >= twoPi)
            lfoPhase -= twoPi;
    }
}

void TremoloProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/tremolo.svg -- a
    // wave whose humps shrink/grow around a centre line (Modulacao category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::tremolo_svg, IconData::tremolo_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
