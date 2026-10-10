#pragma once

#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Acoustic 360-style solid-state bass amplifier, modelled at component level on NodalCircuit from
    the preamp's published documentation (see docs/circuits/Acoustic360.md -- read that file to
    understand this processor).

    No tubes anywhere: two solver blocks.
      1. the preamp -- two common-emitter NPN stages on the amp's 24 V rail around the passive
         Fender-style bass/treble network, the "Variamp" active tone control (a selectable L-C trap
         with its Effect depth pot) and the Volume pot with its Bright treble-bleed switch;
      2. the power amplifier -- a 200 W solid-state output stage modelled as a saturating op-amp
         gain block driving the speaker impedance model directly (no output transformer, exactly
         as in the amp).

    Controls, page 1: Volume, Bright (the Volume's pull/switch treble bleed), Bass, Treble, Effect
    (Variamp depth) and Variamp Freq (the 5-position rotary). Page 2 (synthetic): Output and Speaker
    (4 / 8 ohm).
*/
class Acoustic360StyleAmplifierProcessor : public EffectProcessor
{
public:
    Acoustic360StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override { forceReprepare(); }

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "360-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffc9823a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts) is scaled by this to get a signal level. */
    static constexpr double outputScale = 1.0 / 40.0; // ~200 W into 4 ohm is ~28 Vrms at the terminal

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { firstCollector, toneStackOut, secondCollector, variampNode, preampOut, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    /** Test hook: replaces the speaker by a plain resistor. */
    void debugSetResistiveLoad (double ohms);
    double debugIterations (int block) const noexcept;
    long long debugPreFailures() const noexcept { return failuresPre; }
    long long debugPowerFailures() const noexcept { return failuresPower; }
    int debugRecoveries() const noexcept { return recoveries; }

private:
    struct Channel
    {
        NodalCircuit pre, power;
        NodalCircuit::DynamicState preRest, powerRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp
        int pSrcVcc = 0, pSrcIn = 0;
        int rBassTop = 0, rBassBot = 0, rTrebleTop = 0, rTrebleBot = 0;
        int rVolumeTop = 0, rVolumeBot = 0, rEffect = 0, capBright = 0;
        int grpVarL = 0, capVarC = 0;
        NodalCircuit::Node pCol1 = 0, pTone = 0, pCol2 = 0, pVar = 0, pOut = 0;

        // power amp
        int wSrcPre = 0;
        int rSpkRe = 0, rSpkRp = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        NodalCircuit::Node wOut = 0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, bright, bass, treble, effect;
        int variamp; // 0..4
        int speaker; // 0 = 4 ohm, 1 = 8
    };
    void updatePots (const Knobs& k);
    void applySpeaker (Channel& ch, int index) const;
    void applyVariamp (Channel& ch, int index) const;
    void recover (Channel& ch) const;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;

    std::array<Channel, 2> channels;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* brightParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* effectParam = nullptr;
    juce::AudioParameterFloat* variampParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedBass, smoothedTreble,
        smoothedEffect, smoothedOutput;
    int appliedSpeaker = -1;
    int appliedVariamp = -1;

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
    long long sampleCount = 0, failureCount = 0, failuresPre = 0, failuresPower = 0;
    bool dcOk = false;
    bool channel1Stale = false;
    bool channelsSynced = true;
    long long identicalRun = 0;

    static constexpr int controlInterval = 16;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
