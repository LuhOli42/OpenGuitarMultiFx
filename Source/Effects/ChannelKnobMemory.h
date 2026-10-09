#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <vector>

namespace openguitarmultifx
{

/**
    Per-channel knob sets for multi-channel amps whose real front panel has a separate Gain/EQ/Volume per channel
    (Dual Rectifier, 5150 III, SLO-100, Rockerverb). The editor shows ONE set of knobs; this keeps a stored value per
    channel for each of them. When the Channel parameter changes (on whatever thread changed it -- the UI or a preset
    load, never the audio thread), the visible knobs are saved into the old channel's slot and loaded from the new
    one's, so each channel keeps its own settings exactly like the separate pots on the real amp. The audio thread
    only ever reads the knob parameters, as before.

    Presets: writeState()/readState() persist every channel's slots as extra attributes next to the base parameters.
*/
class ChannelKnobMemory : private juce::AudioProcessorParameter::Listener
{
public:
    /** channelToSlot (optional) groups channels that share one set of pots on the real amp, e.g. { 0, 0, 1, 1 } for
        an EQ shared by Clean/Crunch and another shared by the two Lead channels. Empty = one set per channel. */
    ChannelKnobMemory (juce::AudioParameterFloat& channelParameter, std::vector<juce::AudioParameterFloat*> perChannelKnobs,
                       std::vector<int> channelToSlot = {})
        : channel (channelParameter), knobs (std::move (perChannelKnobs)), slotOf (std::move (channelToSlot))
    {
        numChannels = juce::roundToInt (channel.getNormalisableRange().end - channel.getNormalisableRange().start) + 1;
        if (slotOf.empty())
            for (int c = 0; c < numChannels; ++c)
                slotOf.push_back (c);
        numSlots = *std::max_element (slotOf.begin(), slotOf.end()) + 1;
        active = currentSlot();
        values.assign ((size_t) numSlots, {});
        for (auto& slot : values)
            for (auto* k : knobs)
                slot.push_back (k->get());
        channel.addListener (this);
    }

    ~ChannelKnobMemory() override { channel.removeListener (this); }

    /** Sets a slot's stored starting values (constructor-time voicing defaults, e.g. a lower Gain on Clean). */
    void setDefaults (int ch, std::initializer_list<float> defaults)
    {
        auto& slot = values[(size_t) ch];
        size_t i = 0;
        for (float v : defaults)
            if (i < slot.size())
                slot[i++] = v;
        if (ch == active)
            loadSlot (ch);
    }

    void writeState (juce::XmlElement& xml) const
    {
        const_cast<ChannelKnobMemory*> (this)->saveSlot (active);
        for (int c = 0; c < numSlots; ++c)
            for (size_t i = 0; i < knobs.size(); ++i)
                xml.setAttribute (attributeName (c, i), (double) values[(size_t) c][i]);
    }

    /** Call after the base setState(): the knob parameters then hold the saved active channel's values. A preset from
        before per-channel memory existed has no slot attributes; every channel then starts from those same values. */
    void readState (const juce::XmlElement& xml)
    {
        active = currentSlot();
        for (int c = 0; c < numSlots; ++c)
            for (size_t i = 0; i < knobs.size(); ++i)
                values[(size_t) c][i] = (float) xml.getDoubleAttribute (attributeName (c, i), knobs[i]->get());
        loadSlot (active);
    }

    /** setState() writes the channel parameter before the knobs; suppress the swap while it does. */
    struct ScopedSuspend
    {
        explicit ScopedSuspend (ChannelKnobMemory& m) : memory (m) { memory.suspended = true; }
        ~ScopedSuspend() { memory.suspended = false; }
        ChannelKnobMemory& memory;
    };

private:
    int currentSlot() const
    {
        const auto& r = channel.getNormalisableRange();
        return slotOf[(size_t) juce::jlimit (0, numChannels - 1, juce::roundToInt (channel.get() - r.start))];
    }

    juce::String attributeName (int c, size_t i) const { return knobs[i]->paramID + "_ch" + juce::String (c); }

    void saveSlot (int c)
    {
        for (size_t i = 0; i < knobs.size(); ++i)
            values[(size_t) c][i] = knobs[i]->get();
    }

    void loadSlot (int c)
    {
        for (size_t i = 0; i < knobs.size(); ++i)
            *knobs[i] = values[(size_t) c][i];
    }

    void parameterValueChanged (int, float) override
    {
        if (suspended)
            return;
        const int next = currentSlot();
        if (next == active)
            return;
        saveSlot (active);
        active = next;
        loadSlot (active);
    }

    void parameterGestureChanged (int, bool) override {}

    juce::AudioParameterFloat& channel;
    std::vector<juce::AudioParameterFloat*> knobs;
    std::vector<int> slotOf;
    std::vector<std::vector<float>> values;
    int numChannels = 1, numSlots = 1;
    int active = 0;
    bool suspended = false;
};

} // namespace openguitarmultifx
