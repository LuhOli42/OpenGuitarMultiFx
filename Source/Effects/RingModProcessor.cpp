#include "RingModProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    constexpr double twoPi = 6.283185307179586;
    constexpr double carrierAmplitude = 1.0; // volts, against the diodes' 0.2 V bias and 0.4 V knee
}

RingModProcessor::RingModProcessor()
{
    auto freqParam = std::make_unique<juce::AudioParameterFloat> (
        "ringmod_frequency", "Frequency", juce::NormalisableRange<float> (0.5f, 5000.0f, 0.0f, 0.3f), 300.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 10.0f ? 1 : 0) + " Hz";
        }));
    auto driveParam = std::make_unique<juce::AudioParameterFloat> ("ringmod_drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);
    auto shapeParam = std::make_unique<juce::AudioParameterFloat> (
        "ringmod_shape", "Shape", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const char* names[] = { "Sine", "Triangle", "Square" };
            return juce::String (names[juce::jlimit (0, 2, juce::roundToInt (v))]);
        }));
    auto mixParam = std::make_unique<juce::AudioParameterFloat> ("ringmod_mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f);

    frequency = freqParam.get();
    drive = driveParam.get();
    shape = shapeParam.get();
    mix = mixParam.get();

    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ringmod", "Ring Mod", "|", std::move (freqParam), std::move (driveParam),
                                                                         std::move (shapeParam), std::move (mixParam));
}

double RingModProcessor::diode (double v, double vb, double vl, double h) noexcept
{
    v = std::abs (v);
    if (v <= vb)
        return 0.0;
    if (v <= vl)
        return h * (v - vb) * (v - vb) / (2.0 * vl - 2.0 * vb);
    return h * v - h * vl + h * (vl - vb) * (vl - vb) / (2.0 * vl - 2.0 * vb);
}

double RingModProcessor::carrier (double ph, int shapeIndex) const noexcept
{
    switch (shapeIndex)
    {
        case 1:  return 1.0 - 4.0 * std::abs (ph - 0.5); // triangle, -1 .. 1
        case 2:  return juce::jlimit (-1.0, 1.0, 4.0 * std::sin (twoPi * ph)); // a square with rounded edges (a hard one would alias)
        default: return std::sin (twoPi * ph);
    }
}

void RingModProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    smoothedFrequency.reset (newSampleRate, 0.03);
    smoothedFrequency.setCurrentAndTargetValue (frequency->get());
    smoothedDrive.reset (newSampleRate, 0.03);
    smoothedDrive.setCurrentAndTargetValue (drive->get());
    smoothedMix.reset (newSampleRate, 0.03);
    smoothedMix.setCurrentAndTargetValue (mix->get());
    reset();
}

void RingModProcessor::reset()
{
    phase = 0.0;
}

void RingModProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    smoothedFrequency.setTargetValue (frequency->get());
    smoothedDrive.setTargetValue (drive->get());
    smoothedMix.setTargetValue (mix->get());
    const int shapeIndex = juce::jlimit (0, 2, juce::roundToInt (shape->get()));

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        const double f = smoothedFrequency.getNextValue();
        const double gain = 0.5 * std::pow (16.0, (double) smoothedDrive.getNextValue()); // x0.5 .. x8
        const double m = smoothedMix.getNextValue();

        phase += f / sampleRate;
        if (phase >= 1.0)
            phase -= 1.0;
        const double c = carrierAmplitude * carrier (phase, shapeIndex);

        // One carrier for every channel: a stereo pair stays a pair. Unity for a full-scale carrier: D' = h = 1, so the product
        // is x * sign (c); dividing by the drive restores the guitar's level.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double x = (double) buffer.getSample (ch, i);
            const double xs = 0.5 * gain * x;
            const double wet = (diode (c + xs) - diode (c - xs)) / gain;
            buffer.setSample (ch, i, (float) (x * (1.0 - m) + wet * m));
        }
    }
}

void RingModProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    // No ring-mod glyph on the icon sheet yet: the tremolo wave (amplitude modulation, which is what a ring modulator is) stands in.
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::tremolo_svg, IconData::tremolo_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
