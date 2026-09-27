#include "NS2StyleNoiseSuppressorProcessor.h"
#include "IconKit.h"

#include <IconData.h>

#include <cmath>

namespace openguitarmultifx
{

NS2StyleNoiseSuppressorProcessor::NS2StyleNoiseSuppressorProcessor()
{
    auto thr = std::make_unique<juce::AudioParameterFloat> ("ns2_threshold", "Threshold", juce::NormalisableRange<float> (-80.0f, -30.0f, 0.5f), -60.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
    auto dcy = std::make_unique<juce::AudioParameterFloat> ("ns2_decay", "Decay", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f);
    auto mut = std::make_unique<juce::AudioParameterFloat> ("ns2_mute", "Mute", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Reduction" : "Mute"); }));

    threshold = thr.get(); decay = dcy.get(); mute = mut.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ns2", "NS-2-Style Noise Suppressor", "|", std::move (thr), std::move (dcy), std::move (mut));
}

void NS2StyleNoiseSuppressorProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    reset();
}

void NS2StyleNoiseSuppressorProcessor::reset()
{
    envelope = 0.0;
    gainDb = 0.0;
    muteGain = 1.0;
}

void NS2StyleNoiseSuppressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const double thr = threshold->get();
    // Decay knob: 40 ms .. 2 s to travel the full reduction, exponentially spaced.
    const double closeSeconds = 0.04 * std::pow (50.0, (double) decay->get());
    const double closeStep = maxReductionDb / (closeSeconds * sampleRate);   // dB per sample
    const double openStep = maxReductionDb / (0.001 * sampleRate);            // full travel in 1 ms
    const double detRelease = std::exp (-1.0 / (0.01 * sampleRate));         // 10 ms
    const double muteStep = 1.0 / (0.005 * sampleRate);
    const bool muted = mute->get() >= 0.5f;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        double peak = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, (double) std::abs (buffer.getSample (ch, i)));
        envelope = peak > envelope ? peak : envelope * detRelease;

        const double levelDb = 20.0 * std::log10 (juce::jmax (envelope, 1.0e-9));
        const double targetDb = levelDb >= thr ? 0.0 : -juce::jmin (maxReductionDb, (expanderRatio - 1.0) * (thr - levelDb));
        if (targetDb > gainDb)
            gainDb = juce::jmin (targetDb, gainDb + openStep);
        else
            gainDb = juce::jmax (targetDb, gainDb - closeStep);

        muteGain = muted ? juce::jmax (0.0, muteGain - muteStep) : juce::jmin (1.0, muteGain + muteStep);
        const float g = (float) (std::pow (10.0, gainDb / 20.0) * muteGain);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

void NS2StyleNoiseSuppressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::gate_svg, IconData::gate_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
