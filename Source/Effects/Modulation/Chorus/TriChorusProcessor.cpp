#include "TriChorusProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double minDelayMs = 4.0, maxDelayMs = 20.0, maxDepthMs = 3.0;
    constexpr double wetBandHz = 9000.0;

    double rateHz (double knob) { return 0.1 * std::pow (40.0, knob); } // 0.1 .. 4 Hz, ~0.63 Hz at noon
}

TriChorusProcessor::TriChorusProcessor()
{
    using Range = juce::NormalisableRange<float>;
    auto rate = std::make_unique<juce::AudioParameterFloat> ("trichorus_rate", "Rate", Range (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (rateHz (v), 2) + " Hz"; }));
    auto depth = std::make_unique<juce::AudioParameterFloat> ("trichorus_depth", "Depth", Range (0.0f, 1.0f), 0.5f);
    auto delay = std::make_unique<juce::AudioParameterFloat> ("trichorus_delay", "Delay", Range ((float) minDelayMs, (float) maxDelayMs, 0.1f), 10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ms"; }));
    auto mix = std::make_unique<juce::AudioParameterFloat> ("trichorus_mix", "Mix", Range (0.0f, 1.0f), 0.5f);
    rateParam = rate.get();
    depthParam = depth.get();
    delayParam = delay.get();
    mixParam = mix.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("trichorus", "Tri-Chorus", "|", std::move (rate), std::move (depth),
                                                                        std::move (delay), std::move (mix));
}

void TriChorusProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    line.prepare (newSampleRate, (maxDelayMs + maxDepthMs + 2.0) * 1.0e-3);
    lowpassL.prepare (newSampleRate, wetBandHz);
    lowpassR.prepare (newSampleRate, wetBandHz);
    for (auto* s : { &smoothedRate, &smoothedDepth, &smoothedDelay, &smoothedMix })
        s->reset (newSampleRate, paramRampSeconds);
    smoothedRate.setCurrentAndTargetValue (rateParam->get());
    smoothedDepth.setCurrentAndTargetValue (depthParam->get());
    smoothedDelay.setCurrentAndTargetValue (delayParam->get());
    smoothedMix.setCurrentAndTargetValue (mixParam->get());
    reset();
}

void TriChorusProcessor::reset()
{
    line.reset();
    lowpassL.reset();
    lowpassR.reset();
    phase = 0.0;
}

void TriChorusProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    smoothedRate.setTargetValue (rateParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    smoothedDelay.setTargetValue (delayParam->get());
    smoothedMix.setTargetValue (mixParam->get());

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const double msToSamples = 1.0e-3 * sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const double inc = rateHz (smoothedRate.getNextValue()) / sampleRate;
        const double depthMs = maxDepthMs * smoothedDepth.getNextValue();
        const double centreMs = smoothedDelay.getNextValue();
        const double mix = smoothedMix.getNextValue();

        const float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        line.push (x);

        double voice[3];
        for (int v = 0; v < 3; ++v)
        {
            const double lfo = fast::sinCycles (phase + v / 3.0);
            voice[v] = line.read ((centreMs + depthMs * lfo) * msToSamples);
        }
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;

        if (right != nullptr)
        {
            const double wetL = lowpassL.process ((2.0 * voice[0] + voice[1]) / 3.0);
            const double wetR = lowpassR.process ((2.0 * voice[2] + voice[1]) / 3.0);
            left[i] = (float) (x + mix * wetL);
            right[i] = (float) (x + mix * wetR);
        }
        else
        {
            left[i] = (float) (x + mix * lowpassL.process ((voice[0] + voice[1] + voice[2]) / 3.0));
        }
    }

    for (int c = 2; c < numChannels; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void TriChorusProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
