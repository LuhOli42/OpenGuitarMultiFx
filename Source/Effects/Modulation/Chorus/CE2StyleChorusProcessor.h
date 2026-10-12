#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "BbdChorusParts.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss CE-2-style chorus, from the CE-2 schematic (docs/circuits/CE2Chorus.md -- read that, not this comment).

    Audio path per channel: Q1 emitter-follower input (its coupling caps as high-passes), IC1a inverting pre-emphasis
    shelf (+15 dB above 2.3 kHz, R4/R5/C3/R6/C4), a 48 Hz coupling high-pass, then the MN3007 (1024 stages) with its two
    3rd-order Sallen-Key filters (10K/3.3n/8.2n/470p on each side) as BucketBrigadeDelay, the 14.6 Hz / C14-into-R22
    couplings, and IC1b: a 47K/47K/47K summing amp whose feedback is the matching de-emphasis -- so dry and wet are mixed
    50/50 and the dry path comes out flat.

    Modulation, shared by both channels: the TL022 Schmitt + integrator triangle LFO (Rate = VR1 dividing the square into
    R32/C19), Depth = VR2 dividing the triangle about the bias, R35/C21 smoothing into the Q4 follower, and the MN3101
    clock: C22 (47p) charges through R38 (150K) from the LFO-set offset to the chip's threshold, Q5 resets it; the MN3101
    divides by two.

    Controls: Rate, Depth (both linear "B" pots, as on the pedal).
*/
class CE2StyleChorusProcessor : public EffectProcessor
{
public:
    CE2StyleChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "CE-2"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4f8fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    double debugClockHz() const noexcept { return lastClockHz; }
    double debugDelaySeconds() const noexcept { return channels[0].bbd.delaySeconds (lastClockHz); }
    double debugLfoHz() const noexcept { return lfo.frequencyHz(); }

private:
    struct Channel
    {
        analog::Filter inputCoupling, preEmphasis, wetCoupling, bbdOutCoupling, mixDry, mixWet;
        BucketBrigadeDelay bbd;
    };

    double advanceModulation() noexcept;
    void updateControls (double rate, double depth) noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::SmoothedValue<float> smoothedRate, smoothedDepth;

    bbdchorus::TriangleLfo lfo;
    bbdchorus::RcLowpass lfoSmoothing;
    double depthFraction = 0.5, smoothingGain = 1.0;
    double sampleRate = 0.0, dt = 0.0, lastClockHz = 100000.0;
};

} // namespace openguitarmultifx
