#pragma once

#include "EffectProcessor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openguitarmultifx
{

class GuitarPedalSTM32Processor : public EffectProcessor
{
public:
    GuitarPedalSTM32Processor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "GuitarML STM32"; }
    bool hasTrails() const override { return true; }
    double trailLatencySeconds() const override { return 2.0; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff26a9c2); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> bounds) const override;

private:
    struct FdnLine
    {
        std::vector<float> samples;
        size_t writePosition = 0;
        uint16_t randomState = 0;
        size_t segmentLength = 1;
        size_t segmentPosition = 0;
        double baseDelaySamples = 0.0;
        double depthSamples = 0.0;
        double currentVariation = 0.0;
        double segmentStartVariation = 0.0;
        double targetVariation = 0.0;
    };

    struct ReverseDelayState
    {
        std::vector<float> samples;
        size_t writePosition = 0;
        size_t readPosition1 = 0;
        size_t readPosition2 = 0;
        size_t headDifference = 0;
        size_t delaySamples = 1;
        size_t fadeSamples = 1;
        bool fading = false;
        bool playingHead2 = false;
        float fadePosition = 0.0f;
    };

    float processReverb (float input, float feedback, float damping) noexcept;
    float processDelay (float input, float feedback, int mode) noexcept;
    float readForwardDelay (int speed) const noexcept;
    float readReverseDelay() const noexcept;
    void writeReverseDelay (float value) noexcept;
    void advanceFdnWander (FdnLine& line) noexcept;
    static float nextFdnRandom (FdnLine& line) noexcept;
    static float interpolateFdnLine (const FdnLine& line, double delaySamples) noexcept;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* reverbMix = nullptr;
    juce::AudioParameterFloat* reverbTime = nullptr;
    juce::AudioParameterFloat* reverbFilter = nullptr;
    juce::AudioParameterFloat* delayMix = nullptr;
    juce::AudioParameterFloat* delayTime = nullptr;
    juce::AudioParameterFloat* delayFeedback = nullptr;
    juce::AudioParameterFloat* routing = nullptr;
    juce::AudioParameterFloat* delayMode = nullptr;
    juce::AudioParameterFloat* hold = nullptr;

    std::array<FdnLine, 8> fdnLines;
    std::array<float, 8> fdnFilterStates {};
    std::vector<float> forwardDelay;
    ReverseDelayState reverseDelay;
    juce::SmoothedValue<float> smoothedReverbMix;
    juce::SmoothedValue<float> smoothedDelayMix;

    double currentSampleRate = 0.0;
    double currentDelaySamples = 1.0;
    double delaySmoothingCoefficient = 0.0;
    size_t forwardWritePosition = 0;
    float octaveIc1 = 0.0f;
    float octaveIc2 = 0.0f;
    float octaveA1 = 1.0f;
    float octaveA2 = 0.0f;
    float octaveA3 = 0.0f;
};

} // namespace openguitarmultifx
