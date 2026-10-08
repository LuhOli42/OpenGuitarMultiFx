#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    The MXR Dyna Comp-style compressor and its descendant, the Ross Compressor, modelled component by component from ElectroSmash's
    analysis and schematic of the Dyna Comp (the two share the circuit; the Ross's differences are Aion FX's Ross / Dyna table) -- see
    docs/circuits/DynaCompStyleCompressor.md, which is the file to read to understand this processor, not this comment or the .cpp.

    A feedback compressor: an emitter-follower input, a CA3080 operational transconductance amplifier (OTA) whose two inputs see the
    guitar through different filters (so it amplifies their difference), a phase splitter (Q2) whose two outputs feed a pair of
    transistor rectifiers that discharge a 10 uF capacitor, and an emitter follower from that capacitor back into the OTA's bias-current pin:
    the louder the guitar, the lower the bias current, the lower the OTA's transconductance. The OTA is the one part the netlist solver
    has no device for: its output current Iabc * tanh (Vd / 2 Vt) is computed from block A's input-pair voltage and the bias current of the
    previous sample (the envelope moves over milliseconds) and injected into block B. Two NodalCircuit blocks per channel.

    Controls, as on the pedals: Sustain (the OTA bias resistance: more sustain = more gain and more compression) and Level.
*/
class DynaCompStyleCompressorProcessor : public EffectProcessor
{
public:
    enum class Model { dynaComp, ross };

    explicit DynaCompStyleCompressorProcessor (Model model = Model::dynaComp);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffe6b422); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Diagnostics for tests (channel 0): the OTA's bias current, its differential input, the envelope capacitor and the phase splitter. */
    double debugBiasCurrent() const noexcept { return channels[0].iabc; }
    double debugDifferential() const noexcept { return channels[0].a.voltage (channels[0].nPlus) - channels[0].a.voltage (channels[0].nMinus); }
    double debugEnvelope() const noexcept { return channels[0].b.voltage (channels[0].nEnv); }
    double debugSplitterEmitter() const noexcept { return channels[0].b.voltage (channels[0].nE2); }

    bool dcConverged() const noexcept { return dcOk; }

    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Spec
    {
        const char* displayName;
        double vRef;             // the bias rail ("VR"): 2.54 V (Dyna), 2.93 V (Ross: 56K / 27K)
        double inputBiasOhms;    // the input follower's base bias to VR
        double inputShuntFarads; // C1 to ground at the jack (Ross 150 pF; the Dyna has none)
        double otaFeedOhms;      // R5: the +9 V feed of the OTA inputs' bias node
        double outputCoupling;   // C9 / C13
        double beta;             // 2N3904 / 2N5088
        bool sustainReverseLog;  // Ross: 500kC; the Dyna's 500K taper is not documented (taken linear)
    };

    static const Spec& specFor (Model) noexcept;

    struct Channel
    {
        NodalCircuit a, b;

        int srcIn = 0;                               // a
        NodalCircuit::Node nPlus = 0, nMinus = 0;    // a: the OTA's inputs
        int hOta = 0;                                // b: the OTA's output current source
        NodalCircuit::Node nE5 = 0, nEnv = 0, nE2 = 0, nOut = 0; // b
        int rSustain = 0;                            // b: the OTA bias resistor chain
        int rLevelTop = 0, rLevelBottom = 0;         // b
        double iabc = 0.0;                           // the OTA's bias current (A), one sample old
    };

    void buildChannel (Channel& ch);
    void updatePots (double sustainKnob, double levelKnob);

    Spec spec;
    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* sustain = nullptr;
    juce::AudioParameterFloat* level = nullptr;

    juce::SmoothedValue<float> smoothedSustain, smoothedLevel;

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
    double biasChainOhms = 27.0e3;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
