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

/** The dual-mono shortcut bookkeeping the circuit-solver pedals share (see their process()): identical INPUT does not
    imply identical OUTPUT unless the two circuits also have identical STATE (capacitor charges), so the shortcut runs
    only while the channels are known to be in sync. While it runs, channel 1's circuit is left behind; it is caught up by
    copying channel 0's the moment the channels diverge. After a divergence the shortcut stays off until the input has
    been identical for 10 s, by which time the circuits' memory has decayed enough that re-syncing by copy is inaudible.
    `Channels` is a std::array of two copyable channel structs. */
class DualMonoShortcut
{
public:
    /** A freshly built pair of channels holds identical state. */
    void reset() noexcept
    {
        synced = true;
        stale = false;
        identicalRun = 0;
    }

    /** Call at the start of a block; returns how many channels to solve (1 when channel 1 is copied at the end). */
    template <typename Channels>
    int begin (Channels& channels, const juce::AudioBuffer<float>& buffer, double sampleRate) noexcept
    {
        const int numChannels = juce::jmin (buffer.getNumChannels(), (int) channels.size());
        const int numSamples = buffer.getNumSamples();
        const bool dualMono = numChannels == 2 && blockIsDualMono (buffer);
        active = false;

        if (numChannels == 2)
        {
            identicalRun = dualMono ? identicalRun + numSamples : 0;

            if (! synced && identicalRun >= (long long) (10.0 * sampleRate))
            {
                channels[1] = channels[0];
                synced = true;
                stale = false;
            }

            active = dualMono && synced;

            if (! active && stale)
            {
                channels[1] = channels[0];
                stale = false;
            }

            if (! dualMono)
                synced = false;
        }

        if (active)
            stale = true;
        return active ? 1 : numChannels;
    }

    /** Call at the end of a block. */
    void end (juce::AudioBuffer<float>& buffer) const noexcept
    {
        if (active)
            buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

private:
    bool synced = true, stale = false, active = false;
    long long identicalRun = 0;
};

} // namespace openguitarmultifx
