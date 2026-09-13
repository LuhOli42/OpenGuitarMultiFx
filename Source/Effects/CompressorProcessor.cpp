#include "CompressorProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <array>

namespace openguitarmultifx
{

CompressorProcessor::CompressorProcessor()
{
    auto threshold = std::make_unique<juce::AudioParameterFloat> (
        "comp_threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f), -18.0f);
    auto ratioParam = std::make_unique<juce::AudioParameterFloat> (
        "comp_ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.0f, 0.5f), 4.0f);
    auto attack = std::make_unique<juce::AudioParameterFloat> (
        "comp_attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.0f, 0.4f), 5.0f);
    auto release = std::make_unique<juce::AudioParameterFloat> (
        "comp_release", "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.0f, 0.4f), 80.0f);
    auto makeup = std::make_unique<juce::AudioParameterFloat> (
        "comp_makeup", "Makeup",
        juce::NormalisableRange<float> (-12.0f, 24.0f), 0.0f);

    thresholdDb = threshold.get();
    ratio = ratioParam.get();
    attackMs = attack.get();
    releaseMs = release.get();
    makeupDb = makeup.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "compressor", "Compressor", "|",
        std::move (threshold), std::move (ratioParam), std::move (attack),
        std::move (release), std::move (makeup));
}

void CompressorProcessor::prepare (double sampleRate, int, int)
{
    detector.prepare (sampleRate);
    reset();
}

void CompressorProcessor::reset()
{
    detector.reset();
}

void CompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    detector.setAttackTime (attackMs->get());
    detector.setReleaseTime (releaseMs->get());

    const float threshold = thresholdDb->get();
    const float r = juce::jmax (1.0f, ratio->get());
    const float makeup = makeupDb->get();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Caching the write pointer once per channel avoids the bounds-checked
    // indirection getSample()/setSample() would otherwise do on every one of
    // these hot per-sample accesses. Capped at 2 (mono/stereo) -- same
    // per-channel-state convention AnalogDelayProcessor's filterState uses.
    std::array<float*, 2> channelData {};
    const int cachedChannels = juce::jmin (numChannels, 2);
    for (int ch = 0; ch < cachedChannels; ++ch)
        channelData[(size_t) ch] = buffer.getWritePointer (ch);

    for (int i = 0; i < numSamples; ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < cachedChannels; ++ch)
            peak = juce::jmax (peak, std::abs (channelData[(size_t) ch][i]));

        const float envelope = detector.processSample (peak);
        const float envelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);

        float gainDb = makeup;
        const float overDb = envelopeDb - threshold;
        if (overDb > 0.0f)
            gainDb += -overDb * (1.0f - 1.0f / r);

        const float gainLinear = juce::Decibels::decibelsToGain (gainDb);

        for (int ch = 0; ch < cachedChannels; ++ch)
            channelData[(size_t) ch][i] *= gainLinear;
    }
}

void CompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/pulse.svg -- the
    // unified icon set's "Compressor" glyph (Dinamica category). The sheet
    // reuses this same pulse trace for Expander and IR Loader too, but
    // neither of those exists as a distinct processor/role yet.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
