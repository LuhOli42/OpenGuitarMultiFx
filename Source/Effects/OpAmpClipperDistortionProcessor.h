#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    The "op-amp then diodes to ground" distortion, one class and two registered models -- the MXR Distortion+ and the
    DOD Overdrive 250 -- because the two pedals are the same circuit with different values (the 250's own builders call
    it "almost exactly the same as the Distortion+"): a 741 non-inverting stage whose gain is set by a rheostat in the
    (-) leg, a coupling cap, a 10K resistor into a back-to-back diode pair with a small cap across it, and the Output
    pot across the diodes. See docs/circuits/DistortionPlusStyleDistortion.md, which is the file to read to understand
    this processor, not this comment or the .cpp.

    What makes both sound like they do is the 741 itself: at a gain of ~200 its 1 MHz gain-bandwidth product leaves
    ~5 kHz of bandwidth, which is why the op-amp is a macro-model (NodalCircuit::addOpAmpMacro: finite gain, dominant
    pole, output resistance, swing) and not an ideal one.

    Two controls, as on the pedals: Distortion (Gain on the 250), Output (Level on the 250). One NodalCircuit block per
    channel (a single Newton pair for the diodes; the op-amp adds none).
*/
class OpAmpClipperDistortionProcessor : public EffectProcessor
{
public:
    enum class Model { distortionPlus, dod250 };

    explicit OpAmpClipperDistortionProcessor (Model model = Model::distortionPlus);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (spec.accent); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the op-amp's output (before the coupling cap), the diode node, and the (-) input. */
    double debugOpAmpOut() const noexcept { return channels[0].c.voltage (channels[0].nOpOut); }
    double debugDiodeNode() const noexcept { return channels[0].c.voltage (channels[0].nDiodes); }
    double debugInverting() const noexcept { return channels[0].c.voltage (channels[0].nMinus); }

    /** True when the DC operating point converged in prepare(). */
    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

    /** What differs between the two pedals (everything else is shared); values from the schematics named in the doc. */
    struct ModelSpec
    {
        const char* displayName;
        const char* idPrefix;
        const char* distortionLabel;
        const char* outputLabel;
        juce::uint32 accent;

        double biasLegOhms;         // each half of the supply divider that makes the 4.5 V reference
        double biasCapFarads;       // its decoupling cap
        double inputCapFarads;      // C2: input -> R1
        double inputSeriesOhms;     // R1 into the (+) pin
        double plusBiasOhms;        // (+) pin to the 4.5 V reference
        double feedbackOhms;        // R4/R8
        double feedbackCapFarads;   // across it (0 = none)
        double legCapFarads;        // C3/C4: (-) leg coupling
        double legOhms;             // R3/R6: fixed part of the (-) leg
        double distortionPotOhms;   // the rheostat
        double couplingCapFarads;   // op-amp out -> clipper
        double clipSeriesOhms;      // R5/R9
        double clipCapFarads;       // across the diodes
        double diodeIs, diodeNVt;
        double outputPotOhms;       // across the diodes
        bool distortionGlyph;       // the hard-clipped icon (a distortion) rather than the rounded one (an overdrive)
    };

    static const ModelSpec& specFor (Model model) noexcept;

private:
    struct Channel
    {
        NodalCircuit c;

        int srcIn = 0;
        NodalCircuit::Node nOpOut = 0, nDiodes = 0, nMinus = 0, nOut = 0;
        int rDistortion = 0;                 // the Distortion pot, as the rheostat in the (-) leg
        int rOutTop = 0, rOutBottom = 0;     // Output pot segments (diode node -> wiper, wiper -> ground)
    };

    void buildChannel (Channel& ch);
    void updatePots (double distortionKnob, double outputKnob);

    const ModelSpec& spec;
    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* distortion = nullptr;
    juce::AudioParameterFloat* output = nullptr;

    juce::SmoothedValue<float> smoothedDistortion, smoothedOutput;

        /** JUCE's reset() must return the circuit to its DC operating point -- an
        empty reset() (the bug this fixes) left stale capacitor state forever.
        prepare() deliberately no-ops on a same-rate re-prepare to protect live
        state during the UI's chain-reorder, so reset() re-arms the rate and
        forces the rebuild. Control thread only. */
    void forceReprepare()
    {
        if (sampleRate <= 0.0)
            return;
        const double sr = sampleRate;
        sampleRate = 0.0;
        prepare (sr, 0, 0);
    }

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 16; // samples between pot -> resistance updates
};

} // namespace openguitarmultifx
