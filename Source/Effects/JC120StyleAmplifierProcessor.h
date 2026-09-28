#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "EnvelopeFollower.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Roland JC-120 Jazz Chorus-style solid-state combo amplifier, modelled at component level on NodalCircuit from
    Roland's own factory schematic ("JC-120 JC-160", dated 1979; docs/circuits/JC120JazzChorus.md -- read that file to
    understand this processor). Unlike every other modelled amp in this project, this one is SOLID STATE: op-amp
    preamps (TA-7122AP) and a discrete-transistor boost/clipper feed a power stage represented by a single saturating
    op-amp (real, rail-limited gain, no separate output-transistor Newton port) rather than the real amp's own
    discrete quasi-complementary Class AB output pair -- a from-scratch transistor-level output stage was attempted
    and its DC bias point could not be made to pass any AC signal in the time available (docs/circuits/JC120JazzChorus.md
    has the full story); no tubes, no NodalCircuit power-tube machinery reused here either way.

    Scope: the clean amplification path AND the amp's own real BBD chorus/vibrato circuit (an MN3002 bucket-brigade
    device driven by a modulated clock, per explicit user request 2026-09-28 -- an exception to this project's usual
    "never model a built-in modulation effect" call, since the chorus is this amp's entire reason for existing,
    unlike a secondary reverb tank). The real amp's spring reverb tank is NOT modelled (same "never model an
    effects loop" call as every other amp here -- ReverbProcessor covers that ground) and the built-in Distortion
    footswitch clipper IS modelled (it is part of the amp's own core signal path, not an add-on effect).

    Controls, page 1: Input (Channel 1 / Channel 2 / Both), Volume, Treble, Bass, Middle, Output. Page 2: Distortion
    (footswitch on/off), Effect (Off / Vibrato / Chorus), Speed, Depth, Speaker (4 / 8 / 16 ohm).
*/
class JC120StyleAmplifierProcessor : public EffectProcessor
{
public:
    JC120StyleAmplifierProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "JC-120-Style Amplifier"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff7ec9e8); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    /** Amplifier output (speaker-terminal volts, 8 ohm tap) scaled to a signal level: rated 60 W/channel into 8 ohm
        is ~21.9 V rms / 31 V peak. */
    static constexpr double outputScale = 1.0 / 31.0;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    enum class Probe { channelOnePlate, channelTwoPlate, mixNode, driverBase, outputNodeA, outputNodeB, speaker };
    double debugVoltage (Probe p) const noexcept;
    double getSolveFailureRate() const noexcept { return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0; }
    int debugRecoveries() const noexcept { return recoveries; }
    bool debugLastSampleOk() const noexcept { return lastSampleOk; }
    void debugSetResistiveLoad (double ohms);

private:
    struct Channel
    {
        NodalCircuit pre, power;
        NodalCircuit::DynamicState preRest, powerRest;
        int failStreak = 0;
        int restRefreshCounter = 0;
        double lastEmitted = 0.0, declick = 0.0;
        bool alignOutput = false;

        // preamp: both channels' input op-amp + tone stack live here, mixing into pMix
        int pSrcIn1 = 0, pSrcIn2 = 0;
        int rTrebleTop[2] {}, rTrebleBottom[2] {}, rBass[2] {}, rMid[2] {}, rVolTop[2] {}, rVolBot[2] {};
        NodalCircuit::Node pPlate1 = 0, pPlate2 = 0, pMix = 0;

        // power: master gain, distortion clipper, driver, output pair
        int wSrcCf = 0;
        int rMasterTop = 0, rMasterBot = 0;
        int rDistCouple = 0, rCleanCouple = 0;
        int rSpkRe = 0, rSpkRp = 0, rSpkEddy = 0, capSpkCp = 0, grpSpkLe = 0, grpSpkLp = 0;
        NodalCircuit::Node wDriverBase = 0, wOutA = 0, wOutB = 0, wOut = 0;

        // BBD chorus/vibrato state (own DSP, not on NodalCircuit -- see .cpp)
        static constexpr int bbdMaxDelay = 4096;
        std::array<float, bbdMaxDelay> bbdBuffer {};
        int bbdWritePos = 0;
        double bbdReadPos = 0.0;
        double bbdClockPhase = 0.0;
        double bbdHeldDelaySamples = 0.0;
        double bbdLfoPhase = 0.0;
        double bbdPreLpState = 0.0, bbdPostLpState = 0.0;
    };

    void buildChannel (Channel& ch);
    struct Knobs
    {
        double volume, treble, bass, middle, distortion, effectMode, speed, depth;
        int speaker;
    };
    void updatePots (const Knobs& k);
    void recover (Channel& ch) const;
    void applySpeaker (Channel& ch, int index) const;
    int appliedSpeaker = -1;
    double speakerGain = 1.0;
    bool resistiveLoadForced = false;
    mutable int recoveries = 0;
    Knobs lastKnobs {};
    double bbdChorus (Channel& ch, double dry, double speedHz, double depthAmount, bool chorusNotVibrato) noexcept;

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* inputParam = nullptr;
    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* middleParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;
    juce::AudioParameterFloat* distortionParam = nullptr;
    juce::AudioParameterFloat* effectParam = nullptr;
    juce::AudioParameterFloat* speedParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;

    juce::SmoothedValue<float> smoothedVolume, smoothedTreble, smoothedBass, smoothedMiddle, smoothedOutput,
        smoothedSpeed, smoothedDepth;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    bool lastSampleOk = true;

    static constexpr int controlInterval = 16;
    static constexpr int restRefreshInterval = 512;
};

} // namespace openguitarmultifx
