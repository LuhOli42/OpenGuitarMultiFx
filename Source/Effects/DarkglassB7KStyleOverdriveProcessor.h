#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Darkglass Microtubes B7K-style bass overdrive, modelled at component level on NodalCircuit
    (see docs/circuits/DarkglassB7KStyleOverdrive.md).

    Signal chain: JFET-buffer-equivalent input (500 kOhm) -> Grunt/Attack pre-clip shaping -> drive op-amp
    (TL072) -> CMOS-inverter clipper (4049-style saturating macro + anti-parallel 1N4148 shunt) -> blend of
    the clean buffered signal with the clipped path -> 4-band Baxandall-style EQ (Low 100 Hz shelf,
    Lo-Mid 1 kHz, Hi-Mid 3.3 kHz, Treble 5 kHz shelf, built on GE7's gyrator-band technique) -> Level.

    Front-panel controls: Blend, Level, Drive, Low, Lo Mid, Hi Mid, Treble + Grunt and Attack 3-way toggles.
*/
class DarkglassB7KStyleOverdriveProcessor : public EffectProcessor
{
public:
    DarkglassB7KStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Darkglass B7K-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb48f5f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    static constexpr int numBands = 4;

    struct Channel
    {
        NodalCircuit a, b, eq, d;
        int srcIn = 0;
        int srcB = 0;
        int srcDry = 0, srcWet = 0;
        int srcD = 0;
        NodalCircuit::Node nBuf = 0;
        NodalCircuit::Node nDrive = 0;
        NodalCircuit::Node nClip = 0;
        NodalCircuit::Node nEq = 0;
        NodalCircuit::Node nOut = 0;
        int rAttack = 0, cAttack = 0, cShunt = 0;   // Attack 3-way shaping
        int rGrunt = 0;                              // Grunt 3-way shaping
        int rFb = 0;                                 // drive feedback (Drive pot)
        int rWetIn = 0, rDryIn = 0;                  // Blend pot halves
        int rUp[numBands] {}, rDown[numBands] {};    // EQ band pot halves
        int rVolTop = 0, rVolBottom = 0;             // Level pot
    };

    void buildChannel (Channel& ch);
    void updatePots();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* blendParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* bandParam[numBands] {};
    juce::AudioParameterFloat* gruntParam = nullptr;
    juce::AudioParameterFloat* attackParam = nullptr;
    int appliedGrunt = -1, appliedAttack = -1;

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
