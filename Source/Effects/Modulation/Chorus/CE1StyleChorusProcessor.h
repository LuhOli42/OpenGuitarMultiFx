#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "BbdChorusParts.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Boss CE-1 Chorus Ensemble-style chorus/vibrato, from the CE-1 service schematic (ET-10C board;
    docs/circuits/CE1Chorus.md -- read that, not this comment).

    The CE-1's sound starts at its preamp: the Level pot (VR11 50KA) into IC-1 (TA7136), a non-inverting stage with a
    gain of 1 + R11/R12 = 48 above ~34 Hz on +-13 V rails -- the "CE-1 warmth" is this stage running into its rails. The
    wet path then has a passive treble emphasis (R13 22K || C06 6.8n into R5 10K) feeding a Sallen-Key (R14/R16 10K,
    C12 4.7n to Q2's emitter, C11 1n), the MN3002 (512 stages) and a 4th-order output filter (R41 22K/C28 4.7n,
    R42 6.8K/C26 1n and R43 15K/C29 1n to Q10's emitter, R44 15K/C27 470p).

    Chorus mode: dry + wet at the levels the service notes' waveforms give (M 240 mV dry, N 350 mV wet), the triangle
    LFO's speed set by Chorus Intensity (VR12 dividing the Schmitt output into the integrator, R82/R65/R57/R76, C43).
    Vibrato mode: wet only, Rate and Depth. The clock is a discrete multivibrator whose period the service notes give
    (5..16 us); its control law is taken from those annotations, not derived part by part (see the doc).

    Controls: Level, Intensity (chorus), Mode (Chorus / Vibrato), Rate, Depth (vibrato).
*/
class CE1StyleChorusProcessor : public EffectProcessor
{
public:
    CE1StyleChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "CE-1"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3f7fbf); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugClockHz() const noexcept { return lastClockHz; }
    double debugDelaySeconds() const noexcept { return channels[0].bbd.delaySeconds (lastClockHz); }
    double debugLfoHz() const noexcept { return vibrato ? vibratoHz : lfo.frequencyHz(); }

private:
    struct Channel
    {
        analog::Filter preamp, wetCoupling;
        BucketBrigadeDelay bbd;
    };

    double advanceModulation() noexcept;
    void updateControls (double intensity, double rate) noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterFloat* intensityParam = nullptr;
    juce::AudioParameterFloat* modeParam = nullptr;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::SmoothedValue<float> smoothedLevel, smoothedIntensity, smoothedRate, smoothedDepth;

    bbdchorus::TriangleLfo lfo;
    double vibratoPhase = 0.0, vibratoHz = 3.0;
    bool vibrato = false;
    double dryGain = 0.06, wetGain = 0.09;
    double sampleRate = 0.0, dt = 0.0, lastClockHz = 85000.0;
};

} // namespace openguitarmultifx
