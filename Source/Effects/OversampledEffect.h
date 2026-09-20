#pragma once

#include "EffectProcessor.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>

namespace openguitarmultifx
{

/**
    Runs another EffectProcessor at 2x or 4x the host sample rate, with
    polyphase half-band anti-aliasing filters (juce::dsp::Oversampling) on
    the way up and down. A nonlinear circuit (diode clipper, saturating
    transistor stage) generates harmonics far above Nyquist; without
    oversampling they fold back into the audible band as inharmonic "digital"
    fizz -- the harsh, artificial treble and the added hiss that unoversampled
    distortion has. A real circuit has no Nyquist, so it has neither.

    A decorator, so no processor needs to know it is being oversampled: the
    inner processor is simply prepared at the higher rate and sees blocks of
    N times the length. Parameters, state, name, icon and colour all forward.

    Latency: the IIR half-band filters are minimum-phase with a latency of
    a couple of samples at the base rate (not reported to the engine, which has
    no latency compensation today); they trade linear phase for that.
*/
class OversampledEffect : public EffectProcessor
{
public:
    /** order: 1 = 2x, 2 = 4x. */
    OversampledEffect (std::unique_ptr<EffectProcessor> innerProcessor, int oversamplingOrder)
        : inner (std::move (innerProcessor)), order (juce::jmax (1, oversamplingOrder)) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        // The engine re-prepares every processor in the chain whenever ANY block is dragged, mid-signal, at the
        // same rate. Rebuilding the oversampler would zero its filter state (an audible tick), and the inner
        // circuit processors already guard against a same-rate re-settle -- so an identical call is a no-op.
        if (oversampler != nullptr && juce::exactlyEqual (sampleRate, preparedRate) && maxBlockSize == preparedBlock && numChannels == preparedChannels)
            return;
        preparedRate = sampleRate;
        preparedBlock = maxBlockSize;
        preparedChannels = numChannels;

        const int factor = 1 << order;
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) juce::jmax (2, numChannels), (size_t) order,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampler->initProcessing ((size_t) maxBlockSize);
        inner->prepare (sampleRate * factor, maxBlockSize * factor, numChannels);
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        if (oversampler == nullptr || buffer.getNumSamples() == 0)
            return;

        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);

        // Wrap the oversampled block's channels in an AudioBuffer (pointer array lives on the stack; no allocation).
        float* channelPointers[2] = { up.getChannelPointer (0), up.getNumChannels() > 1 ? up.getChannelPointer (1) : nullptr };
        juce::AudioBuffer<float> high (channelPointers, (int) juce::jmin ((size_t) 2, up.getNumChannels()), (int) up.getNumSamples());
        inner->process (high);

        oversampler->processSamplesDown (block);
    }

    void reset() override
    {
        if (oversampler != nullptr)
            oversampler->reset();
        inner->reset();
    }

    juce::AudioProcessorParameterGroup* getParameters() override { return inner->getParameters(); }
    std::unique_ptr<juce::XmlElement> getState() const override { return inner->getState(); }
    void setState (const juce::XmlElement& state) override { inner->setState (state); }
    const char* getName() const override { return inner->getName(); }
    juce::String getStatusText() const override { return inner->getStatusText(); }
    juce::Colour getAccentColour() const override { return inner->getAccentColour(); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override { inner->drawIcon (g, b); }

    /** Extra samples of delay the up/down filters add at the base rate. */
    float getLatencyInSamples() const { return oversampler != nullptr ? oversampler->getLatencyInSamples() : 0.0f; }

    EffectProcessor& getInner() noexcept { return *inner; }

private:
    std::unique_ptr<EffectProcessor> inner;
    int order;
    double preparedRate = 0.0;
    int preparedBlock = 0, preparedChannels = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
};

} // namespace openguitarmultifx
