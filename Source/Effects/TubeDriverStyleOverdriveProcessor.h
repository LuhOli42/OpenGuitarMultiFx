#pragma once

#include "DualMono.h"
#include "EffectProcessor.h"
#include "NodalCircuit.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Chandler Tube Driver-style overdrive, modelled at component level on NodalCircuit from General Guitar Gadgets' redrawing
    of the circuit (JD Sleep, revision 2003-12-09; docs/circuits/TubeDriverStyleOverdrive.md -- read that file, not this comment).

    Two blocks per channel:
      a. the input filter (10K, 0.033 uF, 1M, 47 pF), a unity-gain op-amp buffer, 5 uF and 1K5 into a second op-amp (inverting,
         feedback = the 500K Tube Drive pot as a rheostat || 120 pF: gain 0 to 333);
      b. a 12AX7 ("starved plate": both triodes have their cathode on the NEGATIVE rail and their grid returned to it, so they
         run at zero bias on ~28 V total), two 68K plate loads, the two stages coupled by 0.047 uF / 10K, and a passive
         Hi / Lo tone network (330 pF, 22K, two 0.047 uF, 0.1 uF, 2K2, 500K and 100K pots) into the Level pot.
    The +/- rails come from a half-wave rectified 12.6 V AC winding (a computed operating point, see the doc).

    Controls: Tube Drive, Hi, Lo, Level.
*/
class TubeDriverStyleOverdriveProcessor : public EffectProcessor
{
public:
    TubeDriverStyleOverdriveProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override {}

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "Tube Driver-Style Overdrive"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffb5793b); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    // ---- diagnostics for tests ----
    bool dcConverged() const noexcept { return dcOk; }
    double debugOpAmpOutput() const noexcept { return channels[0].a.voltage (channels[0].nOp2); }
    double debugPlate1() const noexcept { return channels[0].b.voltage (channels[0].nPlate1); }
    double debugPlate2() const noexcept { return channels[0].b.voltage (channels[0].nPlate2); }
    double debugGrid1() const noexcept { return channels[0].b.voltage (channels[0].nGrid1); }
    double debugGrid2() const noexcept { return channels[0].b.voltage (channels[0].nGrid2); }
    static double railPositive() noexcept;
    static double railNegative() noexcept;
    double getSolveFailureRate() const noexcept
    {
        return sampleCount > 0 ? (double) failureCount / (double) sampleCount : 0.0;
    }
    double debugIterations() const noexcept { return channels[0].b.averageIterations() + channels[0].a.averageIterations(); }

private:
    struct Channel
    {
        NodalCircuit a, b;
        int srcIn = 0, srcOp = 0;
        int rDrive = 0, rHiTop = 0, rHiBottom = 0, rLo = 0, rLevelTop = 0, rLevelBottom = 0;
        NodalCircuit::Node nOp2 = 0, nPlate1 = 0, nPlate2 = 0, nGrid1 = 0, nGrid2 = 0, nOut = 0;
    };

    void buildChannel (Channel& ch);
    void updatePots (double drive, double hi, double lo, double level);

    std::array<Channel, 2> channels;
    DualMonoShortcut shortcut;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* hiParam = nullptr;
    juce::AudioParameterFloat* loParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;

    juce::SmoothedValue<float> smoothedDrive, smoothedHi, smoothedLo, smoothedLevel;

    double sampleRate = 0.0;
    int controlCounter = 0;
    long long sampleCount = 0, failureCount = 0;
    bool dcOk = false;

    static constexpr int controlInterval = 16;
};

} // namespace openguitarmultifx
