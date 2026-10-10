#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <vector>

namespace openguitarmultifx
{

/**
    A knob read through a short linear ramp, so a value that JUMPS -- a snapshot switch, a MIDI CC, a preset edit --
    glides instead of stepping. Most processors read their knobs once per block and apply them as constants, and a
    constant that changes between two blocks is a step in the audio: the click Tests/ParameterJumpClickProbe.cpp
    measures (docs/PresetsAndSnapshots.md, stage 2).

    Two ways to read it, once per block on the audio thread:
    - ramp(numSamples): one value per sample, for anything applied per sample (mix, level, depth). The same array
      serves every channel, so it works with channel-outer loops.
    - skipBlock(numSamples): the value at the end of the block, for knobs that become a filter coefficient or a
      feedback amount once per block (decay, tone). Each block then moves only a small step.
*/
class SmoothedParameter
{
public:
    static constexpr double defaultRampSeconds = 0.02;

    void attach (juce::AudioParameterFloat* parameterToFollow) { param = parameterToFollow; }

    /** Control thread, from the processor's prepare(). Snaps to the knob's current value (no ramp on load). */
    void prepare (double sampleRate, int maxBlockSize, double rampSeconds = defaultRampSeconds)
    {
        smoother.reset (sampleRate, rampSeconds);
        smoother.setCurrentAndTargetValue (param != nullptr ? param->get() : 0.0f);
        // Generous: some tests and offline renders hand processors bigger blocks than prepare() said.
        values.assign ((size_t) juce::jmax (4096, maxBlockSize), smoother.getCurrentValue());
    }

    /** Per-sample values for this block (numSamples <= the size prepared). Valid until the next call. */
    const float* ramp (int numSamples)
    {
        jassert (numSamples <= (int) values.size());
        const int n = juce::jmin (numSamples, (int) values.size());
        smoother.setTargetValue (param->get());
        if (smoother.isSmoothing())
            for (int i = 0; i < n; ++i)
                values[(size_t) i] = smoother.getNextValue();
        else
            std::fill (values.begin(), values.begin() + n, smoother.getCurrentValue());
        return values.data();
    }

    /** The value at the end of this block. */
    float skipBlock (int numSamples)
    {
        smoother.setTargetValue (param->get());
        return smoother.skip (numSamples);
    }

    float getCurrentValue() const { return smoother.getCurrentValue(); }

private:
    juce::AudioParameterFloat* param = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
    std::vector<float> values;
};

} // namespace openguitarmultifx
