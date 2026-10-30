#pragma once

#include "../Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace openguitarmultifx::bypasspedal
{

/**
    Footswitch -> effect on/off. Each stomp on the learned CC toggles one block's bypass (the same crossfade + trails as
    tapping the switch on screen). Per preset, like MIDI Learn: the pedal is wired to a block of THIS preset, so it is
    saved as "block index" inside the preset and resolved again on load. Scenes are the other way to flip effects with a
    pedal (a scene stores every block's on/off); this is for one block regardless of the scene.
*/

/** CC number -> the block it toggles. Same shape as MainComponent::midiCcBindings. */
using Bindings = std::map<int, EffectProcessor*>;

/** One entry per <Block> element of the preset, in order (nullptr for a block that was skipped on load) -- a binding
    stores "block index" because the processor pointer today is destroyed and recreated on every preset load. */
using Blocks = std::vector<EffectProcessor*>;

/** Appends a <BypassPedals> element with one <Pedal cc block/> per binding whose block is among `blocks`. Nothing is
    written when no binding survives, so a preset without pedals looks exactly as it did. */
inline void write (juce::XmlElement& presetXml, const Bindings& bindings, const Blocks& blocks)
{
    juce::XmlElement* pedalsXml = nullptr;

    for (auto& [cc, processor] : bindings)
        for (int block = 0; block < (int) blocks.size(); ++block)
        {
            if (blocks[(size_t) block] != processor)
                continue;

            if (pedalsXml == nullptr)
                pedalsXml = presetXml.createNewChildElement ("BypassPedals");

            auto* pedalXml = pedalsXml->createNewChildElement ("Pedal");
            pedalXml->setAttribute ("cc", cc);
            pedalXml->setAttribute ("block", block);
            break;
        }
}

/** The bindings stored in `presetXml`, resolved against the freshly-created `blocks`. An entry whose block does not
    exist (skipped on load, or a hand-edited file) is dropped rather than failing the load; so is a repeated CC or a
    CC outside 0-127. CC 69 is the reserved snapshot-select CC. */
inline Bindings read (const juce::XmlElement& presetXml, const Blocks& blocks)
{
    Bindings bindings;

    auto* pedalsXml = presetXml.getChildByName ("BypassPedals");
    if (pedalsXml == nullptr)
        return bindings;

    for (auto* pedalXml : pedalsXml->getChildWithTagNameIterator ("Pedal"))
    {
        const int cc = pedalXml->getIntAttribute ("cc", -1);
        const int block = pedalXml->getIntAttribute ("block", -1);

        if (cc < 0 || cc > 127 || cc == 69 || bindings.count (cc) != 0
            || block < 0 || block >= (int) blocks.size() || blocks[(size_t) block] == nullptr)
            continue;

        bindings[cc] = blocks[(size_t) block];
    }

    return bindings;
}

} // namespace openguitarmultifx::bypasspedal
