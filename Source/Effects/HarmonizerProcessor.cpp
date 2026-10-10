#include "HarmonizerProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    // Headroom so dry + a full-level harmony layer can't clip even when
    // both happen to peak together.
    constexpr float outputScale = 0.7f;
}

HarmonizerProcessor::HarmonizerProcessor()
{
    auto semitonesParam = std::make_unique<juce::AudioParameterFloat> (
        "harmonizer_semitones", "Semitones", juce::NormalisableRange<float> (-24.0f, 24.0f), 4.0f);
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "harmonizer_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);

    semitones = semitonesParam.get();
    level = levelParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "harmonizer", "Harmonizer", "|",
        std::move (semitonesParam), std::move (levelParam));
}

void HarmonizerProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    for (auto& shifter : shifters)
        shifter.prepare (sampleRate);

    smoothedSemitones.reset (sampleRate, paramRampSeconds);
    smoothedSemitones.setCurrentAndTargetValue (semitones->get());
    smoothedLevel.reset (sampleRate, paramRampSeconds);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    reset();
}

void HarmonizerProcessor::reset()
{
    for (auto& shifter : shifters)
        shifter.clear();
}

void HarmonizerProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    // Semitones retunes the grain slews -> per-block stepped ramp; level
    // scales the harmony into the dry signal -> per-sample ramp (sample-
    // major so getNextValue() advances once per sample for both channels).
    smoothedSemitones.setTargetValue (semitones->get());
    const float ratio = std::pow (2.0f, smoothedSemitones.skip (numSamples) / 12.0f);
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float levelAmount = smoothedLevel.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& shifter = shifters[(size_t) ch];

            const float input = data[i];
            const float harmony = shifter.process (input, ratio);
            data[i] = (input + harmony * levelAmount) * outputScale;
        }
    }
}

void HarmonizerProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/harmonizer.svg --
    // two notes on one stem, sounding together (Filtro/FX category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::harmonizer_svg, IconData::harmonizer_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
