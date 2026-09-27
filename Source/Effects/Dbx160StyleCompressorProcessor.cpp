#include "Dbx160StyleCompressorProcessor.h"
#include "CompressorCommon.h"
#include "IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

Dbx160StyleCompressorProcessor::Dbx160StyleCompressorProcessor()
{
    auto thr = std::make_unique<juce::AudioParameterFloat> ("dbx160_threshold", "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.5f), -20.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
    auto rat = std::make_unique<juce::AudioParameterFloat> ("dbx160_ratio", "Ratio", juce::NormalisableRange<float> (1.0f, 40.0f, 0.0f, 0.4f), 4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 39.5f ? juce::String ("inf:1") : juce::String (v, 1) + ":1"; }));
    auto out = std::make_unique<juce::AudioParameterFloat> ("dbx160_output", "Output", juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }));
    auto oe = std::make_unique<juce::AudioParameterFloat> ("dbx160_overeasy", "Over Easy", juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v < 0.5f ? "Hard knee" : "Over Easy"); }));

    threshold = thr.get(); ratio = rat.get(); output = out.get(); overEasy = oe.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("dbx160", "dbx 160-Style Compressor", "|", std::move (thr), std::move (rat),
                                                                        std::move (out), std::move (oe));
}

void Dbx160StyleCompressorProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    reset();
}

void Dbx160StyleCompressorProcessor::reset()
{
    power = 0.0;
    levelDb = -120.0;
    grDb = 0.0;
}

void Dbx160StyleCompressorProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const double alpha = dyn::smoothingCoefficient (rmsSeconds, sampleRate);
    const double thr = threshold->get();
    const double r = ratio->get() >= 39.5f ? 1.0e4 : (double) ratio->get(); // the top of the knob is infinity : 1
    const double knee = overEasy->get() >= 0.5f ? kneeDb : 0.0;
    const double outGain = dyn::fromDb (output->get());

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const double x = (double) buffer.getSample (ch, i);
            sum += x * x;
        }
        power += (sum / (double) juce::jmax (1, numChannels) - power) * alpha; // true RMS: a first-order average of the power
        levelDb = 10.0 * std::log10 (std::max (power, 1.0e-18));

        grDb = dyn::staticReductionDb (levelDb, thr, r, knee); // feed-forward: from the input's own level
        const float g = (float) (dyn::fromDb (-grDb) * outGain);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

void Dbx160StyleCompressorProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::pulse_svg, IconData::pulse_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
