#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstring>

namespace openguitarmultifx
{

/** True when a stereo block's two channels are bit-identical -- the usual case for a guitar (mono source feeding
    a stereo chain). The circuit-solver processors use this to run ONE channel's circuit and copy the result,
    halving their cost; see their process() for how the second channel's state is kept honest. */
inline bool blockIsDualMono (const juce::AudioBuffer<float>& buffer) noexcept
{
    return buffer.getNumChannels() >= 2
        && std::memcmp (buffer.getReadPointer (0), buffer.getReadPointer (1),
                        sizeof (float) * (size_t) buffer.getNumSamples()) == 0;
}

} // namespace openguitarmultifx
