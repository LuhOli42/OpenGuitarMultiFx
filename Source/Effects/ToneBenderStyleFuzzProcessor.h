#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "GuitarSource.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Tone Bender Mk II-style fuzz (the three-transistor Colorsound "Professional"), modelled at component level on
    NodalCircuit from the two General Guitar Gadgets drawings (germanium/PNP and silicon/NPN). See
    docs/circuits/ToneBenderStyleFuzz.md -- read that file to understand this processor.

    It is a Fuzz Face (Q2 + Q3: a directly coupled pair biased by a 100k from Q3's emitter back to Q2's base, with the
    Attack pot's wiper bypassed by 4.7 uF, and a split collector load with the output at the junction) preceded by an
    extra common-emitter stage (Q1) coupled through 0.1 uF. That one extra stage is the whole difference in character:
    more gain, more compression, and the input loads the guitar through a 0.01 uF cap to ground (with the guitar's own
    source resistance a low-pass at ~2.7 kHz).

    What the two drawings tell you about the original: the germanium one gives Q1 NO bias network at all -- its base
    goes to ground through a 100k and that is it. It works because a germanium transistor leaks: the leakage through
    the collector-base junction is what forward-biases the base. The silicon drawing, by the same author, adds a
    470k collector-base bias resistor and emitter resistors to Q1, Q2 and Q3 (and a trimmer for Q3's collector load)
    because a silicon part does not leak. So the germanium model needs an explicit collector-base leakage, and its
    Q1 bias point depends on it. Controls: Attack, Volume.
*/
class ToneBenderStyleFuzzProcessor : public EffectProcessor
{
public:
    enum class Model
    {
        germanium, // PNP, positive ground (the original)
        silicon    // NPN, with the bias network a silicon part needs
    };

    explicit ToneBenderStyleFuzzProcessor (Model m = Model::germanium);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd0a04a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests (signed volts: negative on the PNP model) ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugQ1Collector() const noexcept { return channels[0].c.voltage (channels[0].nC1); }
    double debugQ2Collector() const noexcept { return channels[0].c.voltage (channels[0].nC2); }
    double debugQ3Collector() const noexcept { return channels[0].c.voltage (channels[0].nC3); }
    double debugQ3Emitter() const noexcept { return channels[0].c.voltage (channels[0].nE3); }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].c.averageIterations(); }

    struct ModelSpec
    {
        const char* displayName;
        const char* idPrefix;
        bool pnp;
        double railVolts;
        double satCurrent;
        double betaQ1, betaQ2, betaQ3;
        double rQ1CollectorBase;  // R10, silicon only (0 = none)
        double rE1, rE2, rE3;     // R11, R12, R13 (0 = emitter straight to ground / the pot)
        double rQ3Collector;      // R6 (8k2), or the silicon trimmer's set value
        double cOut;              // C3
        double rLeakage;          // collector-base leakage, germanium only (0 = none)
        double tauF, cje, cjc;    // transit time (1 / 2 pi fT), base-emitter and base-collector junction capacitance
    };
    static const ModelSpec& specFor (Model m) noexcept;

private:
    struct Channel
    {
        NodalCircuit c;
        int srcIn = 0;
        NodalCircuit::Node nC1 = 0, nC2 = 0, nC3 = 0, nE3 = 0, nOut = 0;
        int rAttackTop = 0, rAttackBottom = 0;
        int rVolumeTop = 0, rVolumeBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double attack, double volume);

    Model model;
    ModelSpec spec;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;

    juce::SmoothedValue<float> smoothedAttack, smoothedVolume;

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

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
