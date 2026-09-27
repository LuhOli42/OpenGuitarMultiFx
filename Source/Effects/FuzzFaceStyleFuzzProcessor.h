#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "GuitarSource.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Fuzz Face-style fuzz, modelled at component level on NodalCircuit from the two General Guitar Gadgets
    drawings (germanium/PNP and silicon/NPN), checked against R.G. Keen's analysis (Geofex, "The Technology of the
    Fuzz Face"). See docs/circuits/FuzzFaceStyleFuzz.md -- read that file to understand this processor.

    Two transistors, and the whole sound comes from how they are wired, not from a clipper:
      - Q1 is a plain common-emitter stage whose collector drives Q2's base DIRECTLY;
      - Q2's emitter resistor (the Fuzz pot) feeds a 100k back to Q1's base. That one resistor both biases the pair
        (voltage-feedback bias) and, because the pot's wiper is bypassed by a 22 uF, sets the AC gain: with the pot
        at the emitter end Q2's gain is ~8, at the ground end it is the transistor's whole internal gain;
      - the input impedance of that arrangement is very LOW, so the guitar's own output impedance is part of the
        circuit. This model therefore includes a source resistance (`guitarSourceResistance`) -- every other pedal in
        the project reads its input as an ideal voltage, which is fine for a 500k-1M input and wrong here;
      - Q2's collector load is SPLIT (470 + 8.2k on the germanium, 330 + 8.2k on the silicon) and the output is taken
        at the junction, with the supply as AC ground: a volume control permanently set low.

    Two models, one class (see `Model`). Controls: Fuzz, Volume.
*/
class FuzzFaceStyleFuzzProcessor : public EffectProcessor
{
public:
    enum class Model
    {
        germanium, // PNP, positive ground: the original Dallas Arbiter (AC128 / NKT275 class)
        silicon    // NPN: the later Fuzz Face (BC108C class)
    };

    explicit FuzzFaceStyleFuzzProcessor (Model m = Model::germanium);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc7a96b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    /** DC volts (signed: negative rail on the PNP model) at Q1's collector, Q2's emitter and Q2's collector. */
    double debugQ1Collector() const noexcept { return channels[0].c.voltage (channels[0].nQ1Collector); }
    double debugQ2Emitter() const noexcept { return channels[0].c.voltage (channels[0].nQ2Emitter); }
    double debugQ2Collector() const noexcept { return channels[0].c.voltage (channels[0].nQ2Collector); }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].c.averageIterations(); }

    /** The guitar seen from the pedal: a single coil's DC resistance plus the cable. Deliberately a resistance only
        (no pickup inductance) -- see the doc for what that leaves out. */
    static constexpr double guitarSourceResistance = guitarSource::resistance;

    struct ModelSpec
    {
        const char* displayName;
        const char* idPrefix;
        bool pnp;              // true: negative supply, PNP transistors
        double railVolts;      // signed supply
        double satCurrent;     // Is
        double beta;           // forward beta of both transistors
        double rSupplySeries;  // R4, in series with Q2's collector load
        double rLeakage;       // collector-base leakage resistance (germanium only); 0 = none
        double tauF, cje, cjc; // transit time (1 / 2 pi fT), base-emitter and base-collector junction capacitance: the transistor's own bandwidth
    };
    static const ModelSpec& specFor (Model m) noexcept;

private:
    struct Channel
    {
        NodalCircuit c;
        int srcIn = 0;
        NodalCircuit::Node nQ1Collector = 0, nQ2Emitter = 0, nQ2Collector = 0, nOut = 0;
        int rFuzzTop = 0, rFuzzBottom = 0;
        int rVolumeTop = 0, rVolumeBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double fuzz, double volume);

    Model model;
    ModelSpec spec;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* fuzzParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;

    juce::SmoothedValue<float> smoothedFuzz, smoothedVolume;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    DualMonoShortcut shortcut;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
