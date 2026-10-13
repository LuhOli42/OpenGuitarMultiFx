#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/BbdChorusParts.h"
#include "Effects/Modulation/Chorus/EnsembleDelay.h"
#include "Effects/Modulation/Phaser/PhaserParts.h"

#include <array>

namespace openguitarmultifx
{

enum class VibratoModel
{
    vb2,            // Boss VB-2 -- MN3207 + MN3102, the CE-2 family's filters, a true-sine LFO, Rise Time
    magnatone,      // Magnatone 280 -- varistors swinging an RC phase network: the amp's true pitch vibrato, stereo
    cloneTheory,    // EHX Clone Theory -- MN3007, 4047 clock trimmed by a 2N4302, Chorus/Vibrato blend, Vibraflange
    hammondScanner  // Hammond scanner vibrato -- an LC delay line read by a rotating capacitive scanner, V1-V3 / C1-C3
};

/**
    Pitch vibratos from real hardware (docs/circuits/<model>.md). A vibrato is the wet path alone -- a delay (or phase)
    swept fast enough that the pitch moves; the Clone Theory's knob and the Hammond's C modes mix the dry back in.
*/
class VibratoPedalProcessor : public EffectProcessor
{
public:
    explicit VibratoPedalProcessor (VibratoModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff6fc0b0); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugDelaySeconds() const noexcept { return lastDelay; }
    double debugRise() const noexcept { return rise; }

private:
    struct Channel
    {
        BucketBrigadeDelay bbd;
        analog::Filter pre, post;
        EnsembleDelay line;
        WetLowpass lineLoss;
        std::array<phaser::AllpassStage, 3> phase;
        double lastWet = 0.0;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});

    VibratoModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *riseTime = nullptr, *mode = nullptr, *blend = nullptr;
    juce::SmoothedValue<float> smoothedDepth;

    double phase = 0.0, rise = 1.0, sampleRate = 0.0, lastDelay = 0.0;
};

} // namespace openguitarmultifx
