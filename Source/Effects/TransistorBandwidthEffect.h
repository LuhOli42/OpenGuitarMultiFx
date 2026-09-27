#pragma once

#include "Biquad.h"
#include "EffectProcessor.h"

#include <array>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    A single-transistor gain stage's own limited bandwidth (fT), applied to the pedal's whole output: for
    PositiveGroundBoosterProcessor and EPStyleBoosterProcessor, whose hand-derived companion-model solve (see their
    own docs) has no capacitor anywhere that limits treble -- both are pure bass-cutting RC networks around an
    Ebers-Moll transistor of INFINITE bandwidth. A real germanium (Rangemaster-class) or JFET+BJT (EP-3-class) stage
    does not have infinite bandwidth, and with a high-gain common-emitter stage and no explicit Miller/collector
    capacitor in the real schematic either, the device's own fT IS the circuit's only treble limit -- leaving it out
    is why these measured an unbounded, ever-rising gain with frequency (23+ dB from 80 Hz to 3 kHz, docs/circuits/
    HarshnessDiagnosis.md) instead of the boost settling or falling off like the real pedals do.

    Modelled as a single real pole (a plain one-pole lowpass, Biquad.h) at the gain stage's own fT-derived corner --
    an approximation of the internal solve's missing capacitor, not a rebuild of it (touching the hand-verified 3
    -terminal Newton solve for a sign-sensitive companion model is not something to risk without the real transistor's
    part number and measured fT). `cornerHz` is an estimate per pedal, not a datasheet value.
*/
class TransistorBandwidthEffect : public EffectProcessor
{
public:
    TransistorBandwidthEffect (std::unique_ptr<EffectProcessor> innerProcessor, double cornerHz)
        : inner (std::move (innerProcessor)), corner (cornerHz) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        inner->prepare (sampleRate, maxBlockSize, numChannels);
        filters.assign ((size_t) std::max (1, numChannels), Biquad {});
        for (auto& f : filters)
            f.makeLowPass (sampleRate, corner);
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        inner->process (buffer);
        const int channels = std::min (buffer.getNumChannels(), (int) filters.size());
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                d[i] = (float) filters[(size_t) ch].process (d[i]);
        }
    }

    void reset() override
    {
        inner->reset();
        for (auto& f : filters)
            f.reset();
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
    double corner;
    std::vector<Biquad> filters;
};

} // namespace openguitarmultifx
