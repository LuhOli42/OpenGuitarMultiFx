#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "PhaserParts.h"

#include <array>

namespace openguitarmultifx
{

enum class PhaserModel
{
    phase90,    // MXR Phase 90 -- 4 x (24K || 2N5952) / 47n, Script (no R28) or Block (R28 feedback into stage 2)
    phase45,    // MXR Phase 45 -- 2 x (10K || 2N5952) / 47n: one notch
    smallStone, // EHX Small Stone -- 4 CA3094 OTA stages / 6.8n, Color = feedback
    ps1,        // Maestro / Oberheim PS-1 -- 6 x (100K || 2N4302) / 10n, three fixed speeds
    biPhase,    // Mu-Tron Bi-Phase -- two 6-stage opto phasers, sine / square sweeps, stereo
    uniVibe,    // Shin-ei Uni-Vibe -- 4 transistor phase splitters with LDRs lit by a lamp, 15n / 220n / 470p / 4.7n
    pt909,      // Ibanez PT-909 -- 4 x (30K || 2SK30A) / 47n, Width, Feedback
    microFazer  // Univox Micro Fazer -- 4 x (100K || 2SK34D) / 50n
};

/**
    A phaser pedal from its schematic (docs/circuits/<model>.md): the all-pass stages with that pedal's capacitors and
    fixed resistors, its voltage-controlled element (JFET, OTA or lamp + LDR) driven by its LFO, its feedback, and its
    dry/wet mix. Mono pedals run one circuit per channel; the Bi-Phase is stereo (phaser A left, B right).
*/
class PhaserPedalProcessor : public EffectProcessor
{
public:
    explicit PhaserPedalProcessor (PhaserModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb06fd8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** The first stage's corner (Hz) at this moment -- for tests. */
    double debugCornerHz() const noexcept { return lastCorner; }

private:
    static constexpr int maxStages = 6;

    struct Chain
    {
        std::array<phaser::AllpassStage, maxStages> stages;
        std::array<phaser::LdrCell, maxStages> ldr;
        double lastWet = 0.0;
    };
    struct Channel
    {
        Chain a, b;
    };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});
    double runChain (Chain& chain, double x, double sweep, double feedback, bool second) noexcept;
    double elementOhms (Chain& chain, int stage, double sweep, double vds) noexcept;

    PhaserModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *rate = nullptr, *depth = nullptr, *feedback = nullptr, *modeSwitch = nullptr, *shape = nullptr;

    int numStages = 4;
    std::array<double, maxStages> caps {};
    double fixedOhms = 24.0e3;
    phaser::Jfet jfet;
    double vgsCentre = -1.9, vgsSwing = 0.55;
    double rateMin = 0.1, rateMax = 6.0;
    int feedbackStage = 0; // stage whose input the feedback joins

    double phaseA = 0.0, phaseB = 0.25, sampleRate = 0.0, lastCorner = 0.0;
    double lampDrive = 0.0;
};

} // namespace openguitarmultifx
