#pragma once

#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "Effects/Modulation/Chorus/BbdChorusParts.h"
#include "DiodeLimiters.h"

#include <array>
#include <optional>

namespace openguitarmultifx
{

/** The eight BBD flanger pedals this processor models (docs/circuits/<model>.md for each). */
enum class FlangerModel
{
    mxr117,           // MXR M117 Flanger -- SAD1024, NE5532/4558, Regen through a diode-limited summer
    electricMistress, // EHX Electric Mistress (model 3, 1981) -- SAD1024A, almost no anti-alias filtering, Color, Filter Matrix
    bf2,              // Boss BF-2 -- MN3207 + MN3102, the CE-2's pre/de-emphasis, Res
    ada,              // A/DA Flanger -- from its published specs (no reliable schematic): 40:1 sweep, Even/Odd, Threshold
    tycobrahe,        // Tycobrahe Pedalflanger -- MN3001, 4046 VCO (51-509 kHz, from the drawing), Ge-diode Intensity
    echoFlanger,      // EHX Echo Flanger (EH-7700) -- NE570 compander, 1024 or (Slapback) 2048 stages, four modes
    fx75,             // DOD FX75 -- MN3007 + MN3101, 1N4148 shunt clamp before the BBD
    fl301             // Ibanez FL-301 -- MN3007 + MN3101, all TL022
};

/**
    A bucket-brigade flanger, built from one model's schematic: the input stage (and its pre-emphasis), the summer where
    the regeneration re-enters (with that pedal's own limiter -- diodes in the op-amp's feedback, a shunt clamp, or
    just the rails), the BBD with the anti-alias / reconstruction filters drawn around it (BucketBrigadeDelay +
    analog::ladderTransfer), the output mixer (de-emphasis), and the LFO.

    What each pedal's drawing does NOT pin down is its clock VCO's control law; the delay range is taken from the
    pedal's specification or the drawing's own annotations where they exist (Tycobrahe), and otherwise calibrated --
    every model's doc says which. Delay follows the VCO exponentially between its ends (a current-starved VCO's
    frequency is close to exponential in the sweep voltage over a flanger's 10-40:1 range).

    Mono pedal: a stereo chain runs one circuit per channel (dual-mono shortcut when both are identical).
*/
class BbdFlangerProcessor : public EffectProcessor
{
public:
    explicit BbdFlangerProcessor (FlangerModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4fb0c9); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugDelaySeconds() const noexcept { return lastDelay; }
    double debugLfoHz() const noexcept { return lfoHz; }

    enum class Role { manual, width, rate, regen, threshold, matrix, evenOdd, autoMode, echoMode, blend };

private:
    struct Channel
    {
        analog::Filter input, regenFilter, dryMix, wetMix;
        BucketBrigadeDelay bbd, bbdLong;
        std::optional<diodes::FeedbackLimiter> feedbackLimiter;
        std::optional<diodes::ShuntClipper> shuntClipper;
        bbdchorus::Ne570 compressor, expander;
        double lastWet = 0.0;
    };

    struct Knob
    {
        Role role;
        juce::AudioParameterFloat* param;
    };

    void buildParameters();
    float knob (Role role, float fallback) const noexcept;
    double processSample (Channel& ch, double x, double clockHz, bool longLine, double regenGain, double wetGain, double dryGain) noexcept;

    FlangerModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::vector<Knob> knobs;

    // From the model, set in prepare()
    int stages = 1024;
    double minDelay = 0.5e-3, maxDelay = 10.0e-3;
    double rateMin = 0.1, rateMax = 10.0;
    double loopMax = 0.85;     // regeneration loop gain at full knob (1 kHz)
    double regenNorm = 1.0;    // makes the regen path's 1 kHz gain = the knob's loop gain
    double wetNorm = 1.0;      // wet / dry balance as the circuit sets it (1 kHz)
    double limiterInputOhms = 1.0;
    double railVolts = 3.5;
    bool exponentialLfo = false, compander = false, compressorOnly = false;

    bbdchorus::TriangleLfo lfo;
    juce::SmoothedValue<float> smoothedManual, smoothedWidth, smoothedRegen;
    double envelope = 0.0, envCoeff = 0.0, gateGain = 1.0, gateCoeff = 0.0;
    double lfoHz = 0.5, lastDelay = 1.0e-3, sampleRate = 0.0, dt = 0.0;
};

} // namespace openguitarmultifx
