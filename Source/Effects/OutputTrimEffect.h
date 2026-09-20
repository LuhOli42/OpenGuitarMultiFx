#pragma once

#include "EffectProcessor.h"

#include <memory>

namespace openguitarmultifx
{

/**
    Multiplies another EffectProcessor's output by a fixed gain.

    Each circuit-modelled pedal has its own natural loudness: measured with the same guitar-like signal and every
    knob at noon, the Klon and the BD-2 came out ~12 dB louder than bypass while the OD-1 and the Tube Screamers
    were ~8 dB quieter. Real pedals are like that (each has a different Level range), but in a chain feeding an
    amp model the pedals must be swappable at "unity" without the amp being driven 20 dB harder by one than by
    another. So the registry wraps each pedal in the trim that makes its noon-everything setting unity for a
    reference signal (see EffectRegistry.cpp; `PedalUnityLevelTests` keeps it honest). The Level knob keeps its
    whole natural range around that point.
*/
class OutputTrimEffect : public EffectProcessor
{
public:
    OutputTrimEffect (std::unique_ptr<EffectProcessor> innerProcessor, float trimDecibels)
        : inner (std::move (innerProcessor)), gain (juce::Decibels::decibelsToGain (trimDecibels)) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override { inner->prepare (sampleRate, maxBlockSize, numChannels); }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        inner->process (buffer);
        buffer.applyGain (gain);
    }

    void reset() override { inner->reset(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return inner->getParameters(); }
    std::unique_ptr<juce::XmlElement> getState() const override { return inner->getState(); }
    void setState (const juce::XmlElement& state) override { inner->setState (state); }
    const char* getName() const override { return inner->getName(); }
    juce::String getStatusText() const override { return inner->getStatusText(); }
    juce::Colour getAccentColour() const override { return inner->getAccentColour(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { inner->drawIcon (g, b); }

    EffectProcessor& getInner() noexcept { return *inner; }

private:
    std::unique_ptr<EffectProcessor> inner;
    float gain;
};

} // namespace openguitarmultifx
