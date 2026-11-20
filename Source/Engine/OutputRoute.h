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
    // Per-route mix (phase 3): linear level, balance (-1 = only L .. +1 = only R,
    // stereo routes only; it never boosts the other side) and polarity.
    float gain = 1.0f;
    float pan = 0.0f;
    bool invert = false;

    bool operator== (const OutputRoute&) const = default;

    /** Same physical destination, whatever the level/pan/polarity. */
    bool sameDestination (const OutputRoute& other) const noexcept
    {
        return pairStart == other.pairStart && channels == other.channels;
    }

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

        const float g = invert ? -gain : gain;

        if (channels == Channels::stereo)
        {
            const float p = juce::jlimit (-1.0f, 1.0f, pan);
            const float sideGain[2] = { g * juce::jmin (1.0f, 1.0f - p), g * juce::jmin (1.0f, 1.0f + p) };
            for (int offset = 0; offset < 2; ++offset)
                if (pairStart + offset < devChannels)
                    device.addFrom (pairStart + offset, 0, src, offset % srcChannels, 0, numSamples, sideGain[offset]);
            return;
        }

        const int dst = pairStart + (channels == Channels::right ? 1 : 0);
        if (dst >= devChannels)
            return;

        if (srcChannels == 1)
        {
            device.addFrom (dst, 0, src, 0, 0, numSamples, g);
            return;
        }

        device.addFrom (dst, 0, src, 0, 0, numSamples, 0.5f * g);
        device.addFrom (dst, 0, src, 1, 0, numSamples, 0.5f * g);
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

/**
    The mix on one row -> row link (phase 3): level, polarity and how the
    source's two channels map onto the target's. A default LaneLink is the
    plain sum the graph always did.
*/
struct LaneLink
{
    enum class ChannelMap : uint8_t
    {
        stereo = 0,   // L -> L, R -> R
        monoSum,      // (L + R) / 2 -> both
        leftOnly,     // L -> both (the left half of a split L/R)
        rightOnly,    // R -> both
        swapLR        // L -> R, R -> L
    };

    float gain = 1.0f;
    bool invert = false;
    ChannelMap map = ChannelMap::stereo;

    bool operator== (const LaneLink&) const = default;

    /** Audio thread. Sums `src` into `dst` (both lane buffers). */
    void addInto (juce::AudioBuffer<float>& dst, const juce::AudioBuffer<float>& src, int numSamples) const noexcept
    {
        const int dstChannels = dst.getNumChannels();
        const int srcChannels = src.getNumChannels();
        if (dstChannels <= 0 || srcChannels <= 0)
            return;

        const float g = invert ? -gain : gain;
        const int lastSrc = srcChannels - 1;

        for (int ch = 0; ch < dstChannels; ++ch)
        {
            switch (map)
            {
                case ChannelMap::stereo:
                    if (ch < srcChannels)
                        dst.addFrom (ch, 0, src, ch, 0, numSamples, g);
                    break;
                case ChannelMap::monoSum:
                    if (srcChannels == 1)
                        dst.addFrom (ch, 0, src, 0, 0, numSamples, g);
                    else
                    {
                        dst.addFrom (ch, 0, src, 0, 0, numSamples, 0.5f * g);
                        dst.addFrom (ch, 0, src, 1, 0, numSamples, 0.5f * g);
                    }
                    break;
                case ChannelMap::leftOnly:  dst.addFrom (ch, 0, src, 0, 0, numSamples, g); break;
                case ChannelMap::rightOnly: dst.addFrom (ch, 0, src, juce::jmin (1, lastSrc), 0, numSamples, g); break;
                case ChannelMap::swapLR:    dst.addFrom (ch, 0, src, juce::jmin (lastSrc, ch == 0 ? 1 : 0), 0, numSamples, g); break;
            }
        }
    }

    static const char* mapToString (ChannelMap m) noexcept
    {
        switch (m)
        {
            case ChannelMap::monoSum:   return "monoSum";
            case ChannelMap::leftOnly:  return "leftOnly";
            case ChannelMap::rightOnly: return "rightOnly";
            case ChannelMap::swapLR:    return "swapLR";
            case ChannelMap::stereo:    break;
        }
        return "stereo";
    }

    static ChannelMap mapFromString (const juce::String& s) noexcept
    {
        if (s == "monoSum")   return ChannelMap::monoSum;
        if (s == "leftOnly")  return ChannelMap::leftOnly;
        if (s == "rightOnly") return ChannelMap::rightOnly;
        if (s == "swapLR")    return ChannelMap::swapLR;
        return ChannelMap::stereo;
    }
};

} // namespace openguitarmultifx
