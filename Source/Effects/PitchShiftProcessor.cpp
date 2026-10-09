#include "PitchShiftProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

PitchShiftProcessor::PitchShiftProcessor()
{
    auto semitonesParam = std::make_unique<juce::AudioParameterFloat> (
        "pitchshift_semitones", "Semitones", juce::NormalisableRange<float> (-24.0f, 24.0f), 12.0f);
    auto mixParam = std::make_unique<juce::AudioParameterFloat> (
        "pitchshift_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f);

    semitones = semitonesParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> (
        "pitchshift", "Pitch Shift", "|",
        std::move (semitonesParam), std::move (mixParam));
}

void PitchShiftProcessor::prepare (double sampleRate, int, int)
{
    currentSampleRate = sampleRate;
    for (auto& shifter : shifters)
        shifter.prepare (sampleRate);
    reset();
}

void PitchShiftProcessor::reset()
{
    for (auto& shifter : shifters)
        shifter.clear();
    tracker.prepare (currentSampleRate);
}

void PitchShiftProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (currentSampleRate <= 0.0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    const float ratio = std::pow (2.0f, semitones->get() / 12.0f);
    const float wet = mix->get();

    float* channels[2] = { buffer.getWritePointer (0),
                          numChannels > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        if (wet > 0.0f)
        {
            tracker.push (channels[0][i]);
            for (auto& shifter : shifters)
                shifter.slewGrain (tracker.grainTarget);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& shifter = shifters[(size_t) ch];

            const float input = channels[ch][i];
            const float shifted = shifter.process (input, ratio);
            channels[ch][i] = input * (1.0f - wet) + shifted * wet;
        }
    }
}

void PitchShiftProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // See docs/icons/AGENT-icon-notes.md / Assets/Icons/pitch_shift.svg --
    // one note moving to a new fixed position via a single arrow (Filtro/FX category).
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pitch_shift_svg, IconData::pitch_shift_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
