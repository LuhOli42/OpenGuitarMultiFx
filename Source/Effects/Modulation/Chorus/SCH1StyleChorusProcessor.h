#pragma once

#include "Effects/EffectProcessor.h"
#include "BbdChorusParts.h"

#include <array>

namespace openguitarmultifx
{

/**
    An Arion SCH-1-style stereo chorus, from the SCH-1 schematic (docs/circuits/SCH1Chorus.md -- read that, not this
    comment).

    The SCH-1 is built on the CE-2's recipe -- the same IC1a pre-emphasis (47K, 10K + 6.8n, 47K || 100p) and the same
    10K/3.3n/8.2n/470p Sallen-Key in front of the BBD -- around an MN3207 + MN3102. What it adds: a Tone control on the
    dry path (IC1b: a passive bass/treble blend -- C37 2.2n to the pot's H end, R56 33K + C36 15n to its L end, C39 10n
    from the wiper -- followed by a 1 + 51K / (33K + 10n) treble lift), a Q5 phase splitter after the output filter,
    and two IC2 mixers with the matching de-emphasis: OUT A = dry + wet, OUT B = dry - wet. The wet reaches each mixer
    through 22n + 39K, a 185 Hz high-pass that keeps the chorus out of the bass.

    The pedal has one input; a stereo chain gets OUT A left and OUT B right (a mono chain gets OUT A). Its input is the
    mono sum of whatever arrives.

    Controls: Rate, Depth, Tone.
*/
class SCH1StyleChorusProcessor : public EffectProcessor
{
public:
    SCH1StyleChorusProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "SCH-1"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xffd88a4f); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double debugClockHz() const noexcept { return lastClockHz; }
    double debugDelaySeconds() const noexcept { return bbd.delaySeconds (lastClockHz); }
    double debugLfoHz() const noexcept { return lfo.frequencyHz(); }

private:
    static constexpr int toneSteps = 32;

    double advanceModulation() noexcept;
    void updateControls (double rate, double depth, double tone) noexcept;

    analog::Filter inputCoupling, preEmphasis, wetCoupling, tone, mixDry, mixWetA, mixWetB;
    std::array<analog::Filter, toneSteps + 1> toneTable;
    BucketBrigadeDelay bbd;

    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* depthParam = nullptr;
    juce::AudioParameterFloat* toneParam = nullptr;
    juce::SmoothedValue<float> smoothedRate, smoothedDepth, smoothedTone;

    bbdchorus::TriangleLfo lfo;
    bbdchorus::RcLowpass lfoSmoothing;
    double depthFraction = 0.5, smoothingGain = 1.0;
    double sampleRate = 0.0, dt = 0.0, lastClockHz = 100000.0;
};

} // namespace openguitarmultifx
