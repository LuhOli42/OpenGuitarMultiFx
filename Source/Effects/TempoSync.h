#pragma once

#include <juce_core/juce_core.h>

namespace openguitarmultifx::tempoSync
{

/**
    Musical note subdivisions a tempo-synced time parameter can lock to,
    expressed as a multiplier on one quarter-note's duration (ms = mult *
    60000/bpm) -- the standard delay/tremolo-pedal subdivision menu (plain,
    dotted, and triplet values from a 32nd up to a whole note). Order here
    is display/cycling order (see ParameterPanel's inline ms/BPM toggle).
*/
struct Subdivision
{
    const char* label;
    float quarterNoteMultiplier;
};

inline constexpr Subdivision subdivisions[] = {
    { "1/32",  0.125f },
    { "1/16T", 1.0f / 6.0f },
    { "1/16",  0.25f },
    { "1/8T",  1.0f / 3.0f },
    { "1/8",   0.5f },
    { "1/8.",  0.75f },
    { "1/4T",  2.0f / 3.0f },
    { "1/4",   1.0f },
    { "1/4.",  1.5f },
    { "1/2T",  4.0f / 3.0f },
    { "1/2",   2.0f },
    { "1/2.",  3.0f },
    { "1/1",   4.0f },
};

inline constexpr int numSubdivisions = (int) (sizeof (subdivisions) / sizeof (subdivisions[0]));
inline constexpr int defaultSubdivisionIndex = 7; // "1/4"

/** 0 if bpm/index are out of range -- callers clamp into the parameter's
    own range afterward (a whole note at a slow BPM can exceed a short
    delay's max time). */
inline float msForSubdivision (int index, double bpm) noexcept
{
    if (bpm <= 0.0 || index < 0 || index >= numSubdivisions)
        return 0.0f;
    return (float) ((double) subdivisions[(size_t) index].quarterNoteMultiplier * (60000.0 / bpm));
}

} // namespace openguitarmultifx::tempoSync
