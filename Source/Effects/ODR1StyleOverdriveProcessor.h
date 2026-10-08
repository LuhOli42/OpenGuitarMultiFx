#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Nobels ODR-1-style overdrive, modelled at component level on NodalCircuit from Nobels Electronics' own circuit
    diagram (ODR-1, rev 5, Dec 2000; docs/circuits/ODR1StyleOverdrive.md -- read that file, not this comment).

    Three 4558 stages and a JFET/BJT front and back:
      - stage 1 (U1A): a non-inverting gain stage with a Tube-Screamer-like feedback: two 4148 back to back and 120 pF across
        the Drive pot (250KA, with its wiper tied back through 1K8), a gain leg of two shelves (820 + 82 nF, 1K5 + 2.2 uF);
        gain from ~4 to ~470;
      - then a shunt clipper (R20 12K + 2.7 nF into two 4148 to ground) and a passive filter (39K, 82 nF/1 nF/12K, 10K);
      - stage 2 (U2A): the SPECTRUM control. The 30K pot mixes between U2A's (-) and (+) inputs, and its WIPER hangs a
        network on the signal: 27 nF to ground and, through 0.1 uF, a transistor (Q2, C2362G) wired as a bootstrapped
        emitter follower (8.2 nF, 150K, 2.2K, 3.3K) that behaves like an inductor -- a resonant shape whose position
        the pot moves. Q2 is a real transistor here, not an ideal follower: its finite base current is what sets that
        "inductor";
      - stage 3 (U2B): a 4.1 kHz low-pass into a non-inverting stage with a 22K || 4.7 nF || (5K1 + 82 nF) feedback;
      - Level (50KA), then the same JFET switch and buffer output stage as the DT-1 (NobelsCommon.h).
    The electronic bypass is not modelled (the effect path is the one always on here).

    Controls: Drive, Spectrum, Level.
*/
class ODR1StyleOverdriveProcessor : public EffectProcessor
{
public:
    ODR1StyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "ODR-1-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff3f8f4f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugStage1Out() const noexcept { return channels[0].a.voltage (channels[0].nOp1); }
    double debugStage2Out() const noexcept { return channels[0].b1.voltage (channels[0].nOp2); }
    double debugStage3Out() const noexcept { return channels[0].b2.voltage (channels[0].nOp3); }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept
    {
        return channels[0].a.averageIterations() + channels[0].b1.averageIterations() + channels[0].b2.averageIterations();
    }

private:
    struct Channel
    {
        NodalCircuit a, b1, b2;
        int srcIn = 0, srcOp1 = 0, srcOp2 = 0;
        NodalCircuit::Node nOp1 = 0, nOp2 = 0, nOp3 = 0, nOut = 0;
        int rDriveOut = 0, rDriveIn = 0;             // the Drive pot's two segments (a)
        int rSpectrumA = 0, rSpectrumB = 0;          // the Spectrum pot's two segments (b1)
        int rLevelTop = 0, rLevelBottom = 0;         // (b2)
    };

    void buildChannel (Channel& ch);
    void updatePots (double drive, double spectrum, double level);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* spectrumParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedDrive, smoothedSpectrum, smoothedLevel;

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

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
