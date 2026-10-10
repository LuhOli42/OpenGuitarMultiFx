#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Tech 21 SansAmp Bass Driver DI (V1) style bass preamp/overdrive, modelled at component level on
    NodalCircuit (see docs/circuits/SansAmpBDDIStyleOverdrive.md).

    Signal chain: buffer -> 750 Hz bridged-T notch (the signature mid-scoop) + 72 Hz cabinet HPF ->
    Presence gain stage (gain and high-pass knee move together, 2.2 kHz feedback LP) -> Drive stage
    (op-amp saturation -- the V1 has no discrete clipper; the 3.3 V zener pair of the later revisions is
    approximated by the macro's +/-3 V rail swing) -> cabinet-sim filters (450 Hz bridged-T + cascaded
    LPs) -> Blend of clean/emulated -> Baxandall Bass/Treble -> Level.

    Controls (the V1 front panel): Presence, Drive, Blend, Bass, Treble, Level.
*/
class SansAmpBDDIStyleOverdriveProcessor : public EffectProcessor
{
public:
    SansAmpBDDIStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "SansAmp BDDI-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb48f5f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }

private:
    struct Channel
    {
        NodalCircuit a, b, c, d, e;
        int srcIn = 0, srcB = 0, srcC = 0, srcD = 0;
        int srcDry = 0, srcWet = 0;
        NodalCircuit::Node nNotch = 0;
        NodalCircuit::Node nBuf = 0;
        NodalCircuit::Node nPres = 0;
        NodalCircuit::Node nDrive = 0;
        NodalCircuit::Node nCab = 0;
        NodalCircuit::Node nOut = 0;
        int rPresUp = 0, rPresDown = 0;
        int rFb = 0;
        int rDryIn = 0, rWetIn = 0;
        int rBassUp = 0, rBassDown = 0, rTrebleUp = 0, rTrebleDown = 0;
        int rVolTop = 0, rVolBottom = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots();

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* blendParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

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
