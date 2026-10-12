#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "BbdChorusParts.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Electro-Harmonix Small Clone-style chorus (vintage MN3007 version), from the circuit as documented by Aion FX's
    Lithium (docs/circuits/SmallCloneChorus.md -- read that, not this comment).

    Audio path per channel: C1/R1 input, IC1A non-inverting pre-emphasis (R3 33K, R4 6K8 + C2 10n: +15 dB above 2.3 kHz),
    the 3rd-order input filter (R11 3K3/C6 3n3, R12 10K, C8 15n to Q1's emitter, R13 10K/C7 470p), the MN3007, the
    output filter (R19 10K/C10 4n7, R20 39K, C11 2n7 to Q2's emitter, R21 39K/C12 180p), and IC1B: dry (R5 22K) and wet
    (C13 + R24 27K) meet at one node that feeds R6 10K into the inverting input, whose feedback is the matching
    de-emphasis (R7 33K || R8 6K8 + C3 10n). The mix is fixed, as on the pedal.

    Modulation: the LM358 triangle LFO (IC3; Rate = the 1M reverse-log pot + R26 into C14 2.2u), the Depth switch, and
    the CD4047 clock -- its RC pin is reset by D3 and then relaxes through R31 (39K) toward the LFO voltage held on C16
    (10u) until it reaches the chip's threshold; the 4047's output flip-flop halves it.

    Controls: Rate, Depth (Low / High switch).
*/
class SmallCloneStyleChorusProcessor : public EffectProcessor
{
public:
    SmallCloneStyleChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Small Clone"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6fb3c9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugClockHz() const noexcept { return lastClockHz; }
    double debugDelaySeconds() const noexcept { return channels[0].bbd.delaySeconds (lastClockHz); }
    double debugLfoHz() const noexcept { return lfo.frequencyHz(); }

private:
    struct Channel
    {
        analog::Filter inputCoupling, preEmphasis, wetCoupling, mixDry, mixWet;
        BucketBrigadeDelay bbd;
    };

    double advanceModulation() noexcept;
    void updateControls (double rate) noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr; // Low / High switch
    juce::SmoothedValue<float> smoothedRate;

    bbdchorus::TriangleLfo lfo;
    double controlVolts = 1.8; // C16
    double sampleRate = 0.0, dt = 0.0, lastClockHz = 80000.0;
};

} // namespace openguitarmultifx
