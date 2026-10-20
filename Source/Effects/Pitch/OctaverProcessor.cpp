#include "OctaverProcessor.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr float octaveDownRatio = 0.5f;
    // Headroom so dry + a full-level sub-octave layer can't clip even
    // when both happen to peak together.
    constexpr float outputScale = 0.7f;
}

OctaverProcessor::OctaverProcessor()
{
    auto levelParam = std::make_unique<juce::AudioParameterFloat> (
        "octaver_level", "Level", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f);

    level = levelParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "octaver", "Octaver", "|", std::move (levelParam));
}

void OctaverProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    for (auto& shifter : shifters)
        shifter.prepare (sampleRate);

    smoothedLevel.reset (sampleRate, paramRampSeconds);
    smoothedLevel.setCurrentAndTargetValue (level->get());

    reset();
}

void OctaverProcessor::reset()
{
    for (auto& shifter : shifters)
        shifter.clear();
}

void OctaverProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    // Sample-major so smoothedLevel.getNextValue() advances once per
    // sample (not once per channel-sample) and both channels share it.
    smoothedLevel.setTargetValue (level->get());

    for (int i = 0; i < numSamples; ++i)
    {
        const float levelAmount = smoothedLevel.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto& shifter = shifters[(size_t) ch];

            const float input = data[i];
            const float subOctave = shifter.process (input, octaveDownRatio);
            data[i] = (input + subOctave * levelAmount) * outputScale;
        }
    }
}

void OctaverProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/octaver.svg -- the
    // same wave twice, the dotted one at double wavelength (Filtro/FX category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::octaver_svg, IconData::octaver_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
