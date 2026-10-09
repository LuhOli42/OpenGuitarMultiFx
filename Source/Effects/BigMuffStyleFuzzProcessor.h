#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Big Muff Pi-style fuzz, modelled at component level on NodalCircuit from two traced schematics (see
    docs/circuits/BigMuffStyleFuzz.md -- read that file to understand this processor, not this comment or the .cpp).

    The circuit is four NPN stages in a row, all of them the same shunt-feedback-biased common-emitter cell:
      1. input stage -- sets the level into the clippers and nothing else;
      2. and 3. the two CLIPPING stages: each has an anti-parallel silicon diode pair, in series with a capacitor,
         bridging base to collector. The diodes are in the FEEDBACK path (not shunting the signal to ground), so the
         stage's gain collapses smoothly as they conduct -- the long, compressed, sustaining "muff" rather than a
         hard shunt clip, and the reason the sustain is as long as it is;
      4. the output recovery stage, after the tone control.
    Between stages 3 and 4 sits the passive tone stack: one treble branch (a small series cap into a resistor to
    ground) and one bass branch (a series resistor into a cap to ground), the pot blending between them -- so the
    middle is scooped at every setting, which is the other half of the Big Muff sound.

    Three models, one class (see `Model`): the USA Version 3, the Russian "Green/Civil War" one and the first Sovtek.
    The two Russian-made ones are ONE circuit (Kit Rae: the "Sovtek" family's 1st and 2nd editions differ only in
    the feedback/filter caps, 430 pF vs 500 pF; the bubble-font/Black Russian uses a single 470 pF), so the third
    model is the second with that one value changed. USA vs Russian: same topology, different values throughout -- the Russian has lower collector and much higher emitter resistors (less gain per
    stage), much smaller capacitors in series with the clipping diodes (less bass reaches the clippers, so it keeps
    a fatter bottom end) and a different tone-stack corner. Controls: Sustain, Tone, Volume.
*/
class BigMuffStyleFuzzProcessor : public EffectProcessor
{
public:
    enum class Model
    {
        usV3,               // USA Version 3 (1976 "76#3" trace), BC239 transistors
        russianGreen,       // Version 7B/7C, all-green Civil War / Tall Font Russian (500 pF feedback caps)
        sovtekFirstEdition  // Version 7, 1st edition: the first Russian-made Big Muff (1990-91, Red Army Overdrive), 430 pF caps
    };

    explicit BigMuffStyleFuzzProcessor (Model m = Model::usV3);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return spec.displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb4485f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    /** DC/instantaneous collector voltages of the four stages in SIGNAL order (input, clip 1, clip 2, output). */
    double debugCollector (int stage) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].c.averageIterations(); }

    /** Everything that differs between the two models. Values are from the two Kit Rae circuit traces; see the doc. */
    struct ModelSpec
    {
        const char* displayName;
        const char* idPrefix;
        double rIn;            // R2, input series
        double cIn;            // C1, input coupling
        double rInBase;        // R14, input stage base to ground
        double cFeedback;      // C10/C12/C11, the feedback/filter cap of every stage
        double rCollector;     // R13/R18/R11, collector load of stages 1-3
        double rEmitter1;      // R22, input stage emitter
        double rEmitterClip;   // R21/R10, clipping stages' emitters
        double cStageOut;      // C4, input stage output coupling
        double cClipSeries;    // C6/C7, in series with the clipping diodes
        double cToneTreble;    // C9
        double rToneBass;      // R8
        double rOutBaseTop;    // R7, output stage base to +9V
        double rOutCollector;  // R6
        double rOutEmitter;    // R4
        double beta;           // transistor forward beta (assumed, see the doc)
    };
    static const ModelSpec& specFor (Model m) noexcept;

private:
    struct Channel
    {
        NodalCircuit c;
        int srcIn = 0;
        NodalCircuit::Node nCollector[4] {};   // signal order: input, clip 1, clip 2, output
        NodalCircuit::Node nOut = 0;           // volume wiper
        int rSustainTop = 0, rSustainBottom = 0;
        int rToneTreble = 0, rToneBass = 0;    // the two halves of the tone pot
        int rVolumeTop = 0, rVolumeBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double sustain, double tone, double volume);

    Model model;
    ModelSpec spec;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;

    juce::SmoothedValue<float> smoothedSustain, smoothedTone, smoothedVolume;

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
