#pragma once

#include "Biquad.h"
#include "EffectProcessor.h"

#include <array>

namespace openguitarmultifx
{

/**
    A Solid State Logic G-series-style bus compressor. What SSL's own user guide (500 Series G Comp module, 2020) says of the
    SL 4000 G Bus Compressor, and what is modelled: the main VCA is always in circuit and the side-chain is a "classic dominant"
    one -- both channels are rectified independently by a true PEAK full-wave detector and the louder one controls the gain
    reduction of the whole stereo level; the ATTACK, RATIO and RELEASE controls are multi-position switches (attack 0.1, 0.3, 1, 3,
    10 and 30 ms; ratio 2:1, 4:1 and 10:1; release 0.1, 0.3, 0.6 and 1.2 s or Auto); THRESHOLD and MAKE-UP are continuous; a
    switched high-pass filter sits in the side-chain; and "the knee point of the compressor, set with the THRESHOLD control, purposely
    changes depending on the setting of the RATIO control: decreasing the RATIO setting lowers the effective threshold, hence maintaining
    the perceived loudness". The sizes of what the guide leaves out are estimates, listed in docs/circuits/GSeriesStyleBusCompressor.md:
    a 6 dB soft knee, the threshold shift with the ratio (0 / -3 / -6 dB for 10:1 / 4:1 / 2:1), the Auto release (a 0.1 s and a 1.2 s
    release in parallel, the slower one at 0.75 of the reduction) and the side-chain filter's corners (off / 60 / 90 Hz).

    Controls: Threshold, Ratio, Attack, Release, Make-up, Side-chain HPF.
*/
class GSeriesStyleBusCompressorProcessor : public EffectProcessor
{
public:
    GSeriesStyleBusCompressorProcessor();

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void reset() override;

    juce::AudioProcessorParameterGroup* getParameters() override { return parameters.get(); }
    const char* getName() const override { return "G-Series-Style Bus Compressor"; }
    juce::Colour getAccentColour() const override { return juce::Colour (0xff4a6fa5); }
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b) const override;

    double getGainReductionDb() const noexcept { return grDb; }

    static constexpr std::array<double, 3> ratios { 2.0, 4.0, 10.0 };
    static constexpr std::array<double, 3> thresholdShiftDb { -6.0, -3.0, 0.0 };
    static constexpr std::array<double, 6> attackSeconds { 0.0001, 0.0003, 0.001, 0.003, 0.010, 0.030 };
    static constexpr std::array<double, 4> releaseSeconds { 0.1, 0.3, 0.6, 1.2 };
    static constexpr double kneeDb = 6.0;

private:
    std::unique_ptr<juce::AudioProcessorParameterGroup> parameters;
    juce::AudioParameterFloat* threshold = nullptr;
    juce::AudioParameterFloat* ratio = nullptr;
    juce::AudioParameterFloat* attack = nullptr;
    juce::AudioParameterFloat* release = nullptr;
    juce::AudioParameterFloat* makeup = nullptr;
    juce::AudioParameterFloat* highPass = nullptr;

    std::array<Biquad, 2> scHpf;
    float appliedHpf = -1.0f;

    double sampleRate = 0.0;
    double grDb = 0.0, grFast = 0.0, grSlow = 0.0;
};

} // namespace openguitarmultifx
