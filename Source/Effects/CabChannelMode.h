#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <memory>

namespace openguitarmultifx::cabchannels
{

/**
    Which output channels a cabinet block processes: both (default), only the left, or only the right. The other channel
    passes through untouched -- the usual "cab'd amp on one output, the direct signal on the other" rig, e.g. the left
    output to the stage speaker and the right (XLR) to the PA without a cabinet in front of it.

    A stepped 0-2 float like "IR Source", so the parameter panel draws it as a selector with no extra UI, and it is
    appended AFTER a cab's existing parameters so saved presets keep loading (a missing value stays at the default).
*/
constexpr int stereo = 0;
constexpr int leftOnly = 1;
constexpr int rightOnly = 2;

inline std::unique_ptr<juce::AudioParameterFloat> makeParameter (const char* paramId)
{
    return std::make_unique<juce::AudioParameterFloat> (
        paramId, "Channels", juce::NormalisableRange<float> (0.0f, 2.0f, 1.0f), (float) stereo,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) -> juce::String
            {
                const int mode = (int) std::lround (v);
                return mode == leftOnly ? "Left" : mode == rightOnly ? "Right" : "L + R";
            })
            .withValueFromStringFunction ([] (const juce::String& text) -> float
            {
                return text.startsWithIgnoreCase ("Left") ? (float) leftOnly
                     : text.startsWithIgnoreCase ("Right") ? (float) rightOnly : (float) stereo;
            }));
}

inline int modeOf (const juce::AudioParameterFloat* param) noexcept
{
    return param == nullptr ? stereo : juce::jlimit (stereo, rightOnly, (int) std::lround (param->get()));
}

/** Does the cabinet process channel `ch` in this mode? (Channels beyond the second follow "stereo".) */
inline bool processes (int mode, int ch) noexcept
{
    return mode == stereo || (mode == leftOnly && ch == 0) || (mode == rightOnly && ch == 1);
}

} // namespace openguitarmultifx::cabchannels
