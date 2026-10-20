#pragma once

#include "Effects/AnalogFilterDesign.h"
#include "Effects/DualMono.h"
#include "Effects/EffectProcessor.h"
#include "Effects/NodalCircuit.h"
#include "Effects/Modulation/Flanger/DiodeLimiters.h"

#include <array>

namespace openguitarmultifx
{

enum class OctaveFuzzModel
{
    octavia,     // Roger Mayer Octavia -- gain stage, phase splitter, 1N4148 full-wave rectifier (Hendrix)
    greenRinger, // Dan Armstrong Green Ringer -- a x3 stage, a complementary splitter, an NPN used as a dual diode
    superFuzz,   // Univox Super-Fuzz -- gain, splitter, a tied-collector transistor pair (cosh octave), 1N34A clipping, Notch
    toneMachine  // Foxx Tone Machine -- gain, Octave switch into 1N34A, drive stage clipping into Ge diodes, Tone
};

/**
    Octave-up fuzzes (docs/circuits/OctaveFuzzes.md). They all double the frequency the same way -- a transistor
    phase splitter (equal collector and emitter resistors) gives the signal and its inverse, a diode on each rectifies
    one half, and their sum is |x|: the fundamental cancels, the octave remains. That core is solved as a netlist on
    NodalCircuit with each pedal's own diodes (silicon or germanium -- the octave's character is in their knee); the
    level, notch and tone networks around it are linear and run as exact analog transfers.
*/
class OctaveFuzzProcessor : public EffectProcessor
{
public:
    explicit OctaveFuzzProcessor (OctaveFuzzModel model);

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override;
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd8a04f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double getSolveFailureRate() const noexcept { return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0; }
    double debugAverageIterations() const noexcept { return channels[0].c.averageIterations(); }
    double debugNodeVoltage (int node) const noexcept { return node <= channels[0].c.nodes() ? channels[0].c.voltage (node) : -99.0; }

private:
    struct Channel
    {
        NodalCircuit c, pre; // pre: the Super-Fuzz's input stage (block A), driving c through the Drive pot
        int srcIn = 0, srcPre = 0;
        int rDriveBypass = -1, rDriveSeries = -1, rOctaveSwitch = -1; // -1 = the model has no such element
        NodalCircuit::Node nRect = 0, nPre = 0;
        NodalCircuit::DynamicState restOff, restOn;
        double rectRest = 0.0, rectRestOff = 0.0, rectRestOn = 0.0;
        double preRest = 0.0; // operating point the Super-Fuzz AC tap is measured from (no start-up step)
        analog::Filter post, toneLow, toneHigh;
        double clipState = 0.0;
    };

    enum class ToggleRamp { steady, down, up };

    juce::AudioParameterFloat* add (const juce::String& id, const juce::String& name, float def, int positions = 0,
                                    juce::StringArray labels = {});
    void buildChannel (Channel& ch);
    void updatePots();
    void applyToneMachineSwitchState (int octaveOn) noexcept;

    OctaveFuzzModel model;
    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat *drive = nullptr, *level = nullptr, *tone = nullptr, *toggle = nullptr;
    juce::SmoothedValue<float> smoothedLevel;
    // Both the Tone Machine's sustain stage and the Super-Fuzz's tied collectors drive their 1N34A pair from a 10K collector.
    std::array<diodes::ShuntClipper, 2> clippers { diodes::ShuntClipper (10.0e3, diodes::germanium), diodes::ShuntClipper (10.0e3, diodes::germanium) };

    double sampleRate = 0.0, lastDrive = -1.0, driveFraction = 0.5;
    double octaveGain = 1.0, octaveGainStep = 0.0;
    int lastToggle = -1, pendingToggle = -1;
    ToggleRamp toggleRamp = ToggleRamp::steady;
    long long sampleCount = 0, failureCount = 0;
};

} // namespace openguitarmultifx
