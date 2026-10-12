#include "StringEnsembleProcessor.h"
#include "Effects/FastMath.h"
#include "Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr double centreMs = 5.0;
    constexpr double slowHz = 0.6, fastHz = 6.0;
    constexpr double slowDepthMs = 1.0, fastDepthMs = 0.15; // ~4 and ~6 cents of pitch sweep at the nominal setting
    constexpr double wetBandHz = 8000.0;

    double speedScale (double knob) { return std::pow (2.0, 2.0 * knob - 1.0); } // 0.5x .. 2x, 1x at noon
}

StringEnsembleProcessor::StringEnsembleProcessor()
{
    using Range = juce::NormalisableRange<float>;
    auto speed = std::make_unique<juce::AudioParameterFloat> ("ensemble_speed", "Speed", Range (0.0f, 1.0f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (speedScale (v), 2) + "x"; }));
    auto depth = std::make_unique<juce::AudioParameterFloat> ("ensemble_depth", "Depth", Range (0.0f, 1.0f), 0.5f);
    auto mix = std::make_unique<juce::AudioParameterFloat> ("ensemble_mix", "Mix", Range (0.0f, 1.0f), 0.5f);
    speedParam = speed.get();
    depthParam = depth.get();
    mixParam = mix.get();
    parameters = std::make_unique<juce::AudioProcessorParameterGroup> ("ensemble", "String Ensemble", "|", std::move (speed), std::move (depth), std::move (mix));
}

void StringEnsembleProcessor::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    line.prepare (newSampleRate, (centreMs + 2.0 * (slowDepthMs + fastDepthMs) + 2.0) * 1.0e-3);
    lowpassL.prepare (newSampleRate, wetBandHz);
    lowpassR.prepare (newSampleRate, wetBandHz);
    for (auto* s : { &smoothedSpeed, &smoothedDepth, &smoothedMix })
        s->reset (newSampleRate, paramRampSeconds);
    smoothedSpeed.setCurrentAndTargetValue (speedParam->get());
    smoothedDepth.setCurrentAndTargetValue (depthParam->get());
    smoothedMix.setCurrentAndTargetValue (mixParam->get());
    reset();
}

void StringEnsembleProcessor::reset()
{
    line.reset();
    lowpassL.reset();
    lowpassR.reset();
    slowPhase = fastPhase = 0.0;
}

void StringEnsembleProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (sampleRate <= 0.0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    smoothedSpeed.setTargetValue (speedParam->get());
    smoothedDepth.setTargetValue (depthParam->get());
    smoothedMix.setTargetValue (mixParam->get());

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const double msToSamples = 1.0e-3 * sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const double scale = speedScale (smoothedSpeed.getNextValue());
        const double depth = 2.0 * smoothedDepth.getNextValue(); // nominal at noon
        const double mix = smoothedMix.getNextValue();

        const float x = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        line.push (x);

        double voice[3];
        for (int v = 0; v < 3; ++v)
        {
            const double offset = v / 3.0;
            const double sweepMs = depth * (slowDepthMs * fast::sinCycles (slowPhase + offset) + fastDepthMs * fast::sinCycles (fastPhase + offset));
            voice[v] = line.read ((centreMs + sweepMs) * msToSamples);
        }
        slowPhase += scale * slowHz / sampleRate;
        fastPhase += scale * fastHz / sampleRate;
        if (slowPhase >= 1.0) slowPhase -= 1.0;
        if (fastPhase >= 1.0) fastPhase -= 1.0;

        // Equal-loudness crossfade dry -> ensemble (the original's output is the ensemble alone).
        const double dryGain = std::cos (0.5 * juce::MathConstants<double>::pi * mix);
        const double wetGain = std::sin (0.5 * juce::MathConstants<double>::pi * mix);
        if (right != nullptr)
        {
            left[i] = (float) (dryGain * x + wetGain * lowpassL.process ((2.0 * voice[0] + voice[1]) / 3.0));
            right[i] = (float) (dryGain * x + wetGain * lowpassR.process ((2.0 * voice[2] + voice[1]) / 3.0));
        }
        else
        {
            left[i] = (float) (dryGain * x + wetGain * lowpassL.process ((voice[0] + voice[1] + voice[2]) / 3.0));
        }
    }

    for (int c = 2; c < numChannels; ++c)
        buffer.copyFrom (c, 0, buffer, 0, 0, numSamples);
}

void StringEnsembleProcessor::drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const
{
    static const std::unique_ptr<juce::Drawable> svg = icon::loadSvg (IconData::chorus_svg, IconData::chorus_svgSize);
    icon::drawSvg (g, b, svg.get());
}

} // namespace openguitarmultifx
