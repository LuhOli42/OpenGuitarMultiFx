#pragma once

#include "EffectProcessor.h"
#include "TubeAmpCommon.h"

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
    // `limit` is opt-in and reserved for amp models: a pedal's Level knob is allowed to drive the
    // next effect several volts hot (that's what boosters are for), but an amp emitting over full
    // scale just hard-clips in the host -- real amps cannot swing past their rails anyway.
    OutputTrimEffect (std::unique_ptr<EffectProcessor> innerProcessor, float trimDecibels, bool limit = false)
        : inner (std::move (innerProcessor)), gain (juce::Decibels::decibelsToGain (trimDecibels)),
          limitOutput (limit) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override { inner->prepare (sampleRate, maxBlockSize, numChannels); }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        inner->process (buffer);
        buffer.applyGain (gain);
        if (limitOutput)
        {
            // The trim can push a hot emit over full scale -- a model must not hand the host an
            // unbounded float that hard-clips downstream, so the post-trim level saturates once,
            // at the shared ceiling (emits stay unceiled so nothing gets limited twice).
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* s = buffer.getWritePointer (ch);
                const int n = buffer.getNumSamples();
                for (int i = 0; i < n; ++i)
                    s[i] = (float) tubeamp::outputCeiling (s[i]);
            }
        }
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
    bool limitOutput = false;
};

} // namespace openguitarmultifx
