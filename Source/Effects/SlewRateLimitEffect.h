#pragma once

#include "EffectProcessor.h"

#include <atomic>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    A real op-amp's output cannot move faster than its slew rate (volts per microsecond): an ideal solver's output
    can, and every op-amp-macro pedal in this project used to. That is a piece of "the physical degradation of a
    normal circuit" this project was not calculating: it rounds the edges of a hard-clipped square wave and removes
    high-frequency content no real part actually produces. Applied to the pedal's own output (after its netlist,
    before any oversampling downsample), at whatever rate `prepare()` gives it -- the oversampled rate, if wrapped
    inside one, which is the physically correct rate to slew-limit at.

    Not a full circuit model of slewing (a real slew-limited op-amp's feedback network also changes behaviour while
    slewing); this is the same first-order approximation project-wide: clamp the output's rate of change to the real
    part's spec. docs/circuits/HarshnessDiagnosis.md.
*/
class SlewRateLimitEffect : public EffectProcessor
{
public:
    /** Diagnostic: how many samples, across every instance, have actually needed clamping (dev use only, PedalDiagnostics). */
    static inline std::atomic<long long> clampedSamples { 0 };

    SlewRateLimitEffect (std::unique_ptr<EffectProcessor> innerProcessor, double voltsPerMicrosecond)
        : inner (std::move (innerProcessor)), slewPerSecond (voltsPerMicrosecond * 1.0e6) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        inner->prepare (sampleRate, maxBlockSize, numChannels);
        maxStep = slewPerSecond / sampleRate;
        state.assign ((size_t) std::max (1, numChannels), 0.0);
        primed = false;
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        inner->process (buffer);

        const int channels = std::min (buffer.getNumChannels(), (int) state.size());
        if (! primed)
        {
            for (int ch = 0; ch < channels; ++ch)
                state[(size_t) ch] = buffer.getNumSamples() > 0 ? (double) buffer.getSample (ch, 0) : 0.0;
            primed = true;
        }
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            double& y = state[(size_t) ch];
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const double x = (double) d[i];
                const double raw = x - y;
                const double step = juce::jlimit (-maxStep, maxStep, raw);
                if (step != raw)
                    clampedSamples.fetch_add (1, std::memory_order_relaxed);
                y += step;
                d[i] = (float) y;
            }
        }
    }

    void reset() override
    {
        inner->reset();
        std::fill (state.begin(), state.end(), 0.0);
        primed = false;
    }

    juce::AudioProcessorParameterGroup* getParameters() override { return inner->getParameters(); }
    std::unique_ptr<juce::XmlElement> getState() const override { return inner->getState(); }
    void setState (const juce::XmlElement& s) override { inner->setState (s); }
    const char* getName() const override { return inner->getName(); }
    juce::String getStatusText() const override { return inner->getStatusText(); }
    juce::Colour getAccentColour() const override { return inner->getAccentColour(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { inner->drawIcon (g, b); }

    EffectProcessor& getInner() noexcept { return *inner; }

private:
    std::unique_ptr<EffectProcessor> inner;
    double slewPerSecond;
    double maxStep = 1.0e9;
    std::vector<double> state;
    bool primed = false;
};

} // namespace openguitarmultifx
