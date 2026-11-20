#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace openguitarmultifx
{

/**
    Where one lane's output lands on the physical device outputs
    (docs/SignalRoutingAndOutputSplitPlan.md, phase 1). A route names an
    output PAIR (its first channel: 0, 2, 4, ...) and which of the pair's
    channels it uses:

      stereo -- lane L -> pair L, lane R -> pair R (a mono lane is the same
                on both, which is what every amp emits);
      left / right -- only that one physical channel, carrying the lane
                folded to mono ((L + R) / 2, so a mono-duplicated lane keeps
                its level and a stereo one keeps both sides).

    POD, built on the control thread, read by the audio thread.
*/
struct OutputRoute
{
    enum class Channels : uint8_t { stereo = 0, left = 1, right = 2 };

    int pairStart = 0;
    Channels channels = Channels::stereo;

    bool operator== (const OutputRoute&) const = default;

    /** Bit per physical channel this route writes (bit n = channel n). */
    uint32_t channelMask() const noexcept
    {
        if (pairStart < 0 || pairStart > 30)
            return 0;
        switch (channels)
        {
            case Channels::left:  return 1u << pairStart;
            case Channels::right: return 1u << (pairStart + 1);
            case Channels::stereo: break;
        }
        return 3u << pairStart;
    }

    /** Audio thread. Sums `src` (a lane buffer, 1 or 2 channels) into
        `device` along this route. Channels past the device's end are
        skipped, so a stale route from a bigger interface writes nothing
        rather than out of bounds. */
    void addInto (juce::AudioBuffer<float>& device, const juce::AudioBuffer<float>& src, int numSamples) const noexcept
    {
        const int srcChannels = src.getNumChannels();
        const int devChannels = device.getNumChannels();
        if (srcChannels <= 0 || pairStart < 0)
            return;

        if (channels == Channels::stereo)
        {
            for (int offset = 0; offset < 2; ++offset)
                if (pairStart + offset < devChannels)
                    device.addFrom (pairStart + offset, 0, src, offset % srcChannels, 0, numSamples);
            return;
        }

        const int dst = pairStart + (channels == Channels::right ? 1 : 0);
        if (dst >= devChannels)
            return;

        if (srcChannels == 1)
        {
            device.addFrom (dst, 0, src, 0, 0, numSamples);
            return;
        }

        device.addFrom (dst, 0, src, 0, 0, numSamples, 0.5f);
        device.addFrom (dst, 0, src, 1, 0, numSamples, 0.5f);
    }

    static const char* channelsToString (Channels c) noexcept
    {
        switch (c)
        {
            case Channels::left:  return "left";
            case Channels::right: return "right";
            case Channels::stereo: break;
        }
        return "stereo";
    }

    static Channels channelsFromString (const juce::String& s) noexcept
    {
        if (s == "left")  return Channels::left;
        if (s == "right") return Channels::right;
        return Channels::stereo;
    }
};

} // namespace openguitarmultifx
