#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/BbdChorusParts.h"

#include <array>

namespace openguitarmultifx
{

/** The BBD (and one sample-and-hold digital) delay pedals this processor models (docs/circuits/<model>.md each). */
enum class DelayModel
{
    dm2,             // Boss DM-2 -- MN3005 + MN3101, NE570 compander, the CE-2's emphasis, 5th-order reconstruction
    memoryMan,       // EHX Deluxe Memory Man -- 2 x MN3005 (8192 stages), 4047 clock, NE570, overdriving preamp, chorus/vibrato
    mxr118,          // MXR M118 Analog Delay -- six SAD1024 sections (3072 stages), NE571 compander
    ad9,             // Ibanez AD9 -- MN3205 + MN3102, NE571, 5th-order reconstruction
    ad80,            // Ibanez AD80 -- MN3005 + MN3101, NE570, Blend
    fx90,            // DOD FX90 -- MN3005 + MN3101, NE570, 27K filters
    sad1,            // Arion SAD-1 -- MN3205 + MN3102, uPC1571, stereo outputs (dry + wet / dry - wet)
    dd2              // Boss DD-2 -- 12-bit converter + DRAM at a variable sample rate (modelled as a quantised S/H queue), NE570
};

/**
    A delay pedal built from one model's schematic: input stage and pre-emphasis, the compander, the regeneration summer
    (bounded by the op-amps' rails, so a pedal that self-oscillates at full Repeat still does, and stays finite), the
    delay line with the anti-alias / reconstruction filters drawn around it (BucketBrigadeDelay + analog::ladderTransfer
    -- for the DD-2 the same queue at its 20-82 kHz sample rate with 12-bit samples), the de-emphasis and the mix.

    Delay time: each clock is an RC oscillator whose period is proportional to the Delay pot's resistance, so the delay
    is linear in that resistance (through the pot's taper) between the pedal's published minimum and maximum.

    Long delays mean a clock far below the audio rate, so the BBD costs little here; the compander is what keeps
    these chips' hiss down in the real pedals and, with no hiss modelled, shapes the repeats' dynamics.
*/
class BbdDelayProcessor : public EffectProcessor
{
public:
    explicit BbdDelayProcessor (DelayModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    bool hasTrails() const override { return true; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc98f4f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugDelaySeconds() const noexcept { return lastDelay; }

    enum class Role { time, repeat, level, blend, preampLevel, modDepth, modSwitch, mode };

private:
    struct Channel
    {
        analog::Filter preamp, input, dryMix, wetMix, regenFilter;
        BucketBrigadeDelay line;
        std::array<BucketBrigadeDelay, 2> shortLines; // DD-2 S and M modes: 1/16 and 1/4 of the memory
        bbdchorus::Ne570 compressor, expander;
        double lastRaw = 0.0;
    };

    struct Knob
    {
        Role role;
        juce::AudioParameterFloat* param;
        double taperMid; // 0.15 audio, 0.5 linear, 0.85 reverse
    };

    float knob (Role role, float fallback) const noexcept;
    double knobTapered (Role role, float fallback) const noexcept;
    double processSample (Channel& ch, double x, double clockHz, double repeat, double wet, double dry, double preGain, bool hold) noexcept;

    DelayModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::vector<Knob> knobs;

    int stages = 4096;
    double minDelay = 0.02, maxDelay = 0.3;
    double loopMax = 1.0, regenNorm = 1.0, wetNorm = 1.0, railVolts = 3.5, preampRail = 6.5;
    bool hasPreamp = false, stereo = false, dryFromInput = false;
    int lineIndex = 2;

    bbdchorus::TriangleLfo lfo;
    juce::SmoothedValue<float> smoothedTime;
    double lastDelay = 0.1, sampleRate = 0.0, dt = 0.0;
};

} // namespace openguitarmultifx
