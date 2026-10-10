#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"
#include "TubeModels.h"

#include <array>

namespace openguitarmultifx
{

class EL84BoutiqueAmpCore : public EffectProcessor
{
public:
    enum class Model { matchlessHC30, hotCat30, carmenGhia, astroverb16 };
    enum class Probe { preampPlate, piPlateA, piPlateB, powerPlateA, powerPlateB, cathodeBias, speaker, screen };

    struct TubeFits
    {
        KorenTriode::Parameters triode;
        KorenPentode::Parameters ef86;
        KorenPentode::Parameters el84;
        KorenPentode::Parameters el84Pair;
    };

    EL84BoutiqueAmpCore (Model model, TubeFits fits, bool& reducedOrder, const char* displayName);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;
    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return displayName; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff2a2a2a); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> bounds) const override;

    bool isDcConverged() const noexcept { return dcOk; }
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugVoltage (Probe probe) const noexcept;
    double railPlates() const noexcept;
    double plateCurrentTotal() const noexcept;

protected:
    static constexpr double outputScale = 1.0 / 40.0;

private:
    struct Channel
    {
        NodalCircuit pre, pre2, power, supply;
        NodalCircuit::DynamicState preRest, pre2Rest, powerRest, supplyRest;
        int preInput = 0, preInput2 = 0, preRail = 0, preRail2 = 0;
        int preVolumeTop = -1, preVolumeBottom = -1, preVolume2Top = -1, preVolume2Bottom = -1;
        int rTrebleTop = -1, rTrebleBottom = -1, rBassTop = -1, rBassBottom = -1, rMid = -1;
        int toneCap = -1, rCut = -1, rPresence = -1;
        NodalCircuit::Node prePlate = 0, preOut = 0, preOut2 = 0, toneOut = 0;
        int powerInput = 0, powerRail = 0, powerCf = 0;
        int rCathodeBias = -1, rMasterTopA = -1, rMasterTopB = -1;
        int rSpeakerRe = -1, rSpeakerRp = -1, rSpeakerEddy = -1, capSpeakerCp = -1, grpSpeakerLe = -1, grpSpeakerLp = -1;
        int penA = -1, penB = -1;
        NodalCircuit::Node piPlateA = 0, piPlateB = 0, piGridA = 0, piGridB = 0, piTail = 0;
        NodalCircuit::Node powerPlateA = 0, powerPlateB = 0, cathodeBias = 0, speaker = 0;
        int supplyCurrent = 0, supplySource = 0, rectifierResistance = -1;
        NodalCircuit::Node supplyA = 0, supplyB = 0, preRailNode = 0;
        int supplyCounter = 0, currentCount = 0;
        double currentSum = 0.0, preRailFiltered = 0.0, outputState = 0.0, envelope = 0.0;
        double preDc = 0.0, pre2Dc = 0.0;
        int lastTonePosition = -1, lastSpeakerIndex = -1;
        // reducedOrder control folding (see updatePots' else branch)
        double bmDriveScale = 1.0, bmBias = 0.5, bmFeel = 1.0;
        double bmCutAlpha = 1.0, bmLp = 0.0, bmShelf = 0.0, bmPresAlpha = 0.0, bmLpPres = 0.0;
        long long failures = 0;
    };

    void buildChannel (Channel& channel);
    void buildPreamp (Channel& channel, NodalCircuit& circuit, bool channelTwo);
    void buildPower (Channel& channel);
    void buildSupply (Channel& channel);
    void updatePots (Channel& channel);
    void updateSupply (Channel& channel);
    double behavioralPowerStage (Channel& channel, double drive) const noexcept;
    juce::AudioParameterFloat* addFloat (juce::AudioProcessorParameterGroup& group, const char* id, const char* name, float value);
    juce::AudioParameterFloat* findParam (const char* id) const;
    double param (const juce::AudioParameterFloat* parameter, double fallback) const noexcept;

    Model model;
    TubeFits tubes;
    bool& reducedOrder;
    const char* displayName;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    juce::AudioParameterFloat* channelParam = nullptr;
    juce::AudioParameterFloat* volume1Param = nullptr;
    juce::AudioParameterFloat* bass1Param = nullptr;
    juce::AudioParameterFloat* treble1Param = nullptr;
    juce::AudioParameterFloat* volume2Param = nullptr;
    juce::AudioParameterFloat* tone2Param = nullptr;
    juce::AudioParameterFloat* cutParam = nullptr;
    juce::AudioParameterFloat* gainParam = nullptr;
    juce::AudioParameterFloat* bassParam = nullptr;
    juce::AudioParameterFloat* midParam = nullptr;
    juce::AudioParameterFloat* trebleParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::AudioParameterFloat* masterParam = nullptr;
    juce::AudioParameterFloat* preampParam = nullptr;
    juce::AudioParameterFloat* presenceParam = nullptr;
    juce::AudioParameterFloat* powerParam = nullptr;
    juce::AudioParameterFloat* biasParam = nullptr;
    juce::AudioParameterFloat* feelParam = nullptr;
    juce::AudioParameterFloat* speakerParam = nullptr;
    juce::AudioParameterFloat* outputParam = nullptr;

    double sampleRate = 0.0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;
    static constexpr int supplyInterval = 8;
};

} // namespace openguitarmultifx
