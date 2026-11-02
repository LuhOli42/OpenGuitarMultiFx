#include "SnapshotPresetXml.h"

namespace openguitarmultifx::snapshotxml
{

BlockSnapshot captureBlock (EffectProcessor& processor)
{
    BlockSnapshot snapshot;
    snapshot.bypassed = processor.isBypassed();

    if (auto* group = processor.getParameters())
        for (auto* param : group->getParameters (true))
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                snapshot.params.emplace_back (floatParam->paramID, floatParam->get());

    return snapshot;
}

void applyBlock (EffectProcessor& processor, const BlockSnapshot& snapshot)
{
    // Parameters first, bypass last: a snapshot that switches a block ON
    // should already have its target knob values in place when the
    // crossfade brings the wet path up -- same order a player gets for
    // free when they turn knobs on an already-bypassed pedal.
    // The param list is copied so a processor's legacy-format migration
    // (e.g. a selector that grew positions) can remap values on recall.
    auto params = snapshot.params;
    processor.migrateLegacySnapshotParams (params);
    if (auto* group = processor.getParameters())
        for (auto* param : group->getParameters (true))
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (param))
                for (const auto& [id, value] : params)
                    if (floatParam->paramID == id)
                    {
                        *floatParam = value;
                        break;
                    }

    processor.setBypassed (snapshot.bypassed);
}

void write (juce::XmlElement& presetXml,
            const std::array<Snapshot, maxSnapshots>& snapshots,
            int currentIndex,
            int count)
{
    auto* snapshotsXml = presetXml.createNewChildElement ("Snapshots");
    snapshotsXml->setAttribute ("current", juce::jlimit (0, maxSnapshots - 1, currentIndex));
    snapshotsXml->setAttribute ("count", juce::jlimit (1, maxSnapshots, count));

    for (int i = 0; i < maxSnapshots; ++i)
    {
        const auto& snapshot = snapshots[(size_t) i];

        auto* snapshotXml = snapshotsXml->createNewChildElement ("Snapshot");
        snapshotXml->setAttribute ("index", i);
        if (snapshot.name.isNotEmpty())
            snapshotXml->setAttribute ("name", snapshot.name);
        if (snapshot.colour.isNotEmpty())
            snapshotXml->setAttribute ("colour", snapshot.colour);

        for (const auto& block : snapshot.blocks)
        {
            auto* blockXml = snapshotXml->createNewChildElement ("Block");
            if (block.bypassed)
                blockXml->setAttribute ("bypassed", true);

            // Same attribute-per-paramID convention EffectProcessor::getState()
            // already uses for <EffectState>.
            for (const auto& [id, value] : block.params)
                blockXml->setAttribute (id, (double) value);
        }
    }
}

bool read (const juce::XmlElement& presetXml,
           std::array<Snapshot, maxSnapshots>& snapshots,
           int& currentIndex,
           int* count)
{
    auto* snapshotsXml = presetXml.getChildByName ("Snapshots");
    if (snapshotsXml == nullptr)
        return false;

    snapshots = {};
    currentIndex = juce::jlimit (0, maxSnapshots - 1, snapshotsXml->getIntAttribute ("current", 0));

    for (auto* snapshotXml : snapshotsXml->getChildWithTagNameIterator ("Snapshot"))
    {
        const int index = snapshotXml->getIntAttribute ("index", -1);
        if (index < 0 || index >= maxSnapshots)
            continue;

        auto& snapshot = snapshots[(size_t) index];
        snapshot.name = snapshotXml->getStringAttribute ("name");
        snapshot.colour = snapshotXml->getStringAttribute ("colour");

        for (auto* blockXml : snapshotXml->getChildWithTagNameIterator ("Block"))
        {
            BlockSnapshot block;
            block.bypassed = blockXml->getBoolAttribute ("bypassed", false);

            // Param values are every attribute except the reserved
            // "bypassed" flag -- see write().
            for (int a = 0; a < blockXml->getNumAttributes(); ++a)
            {
                const auto name = blockXml->getAttributeName (a);
                if (name == "bypassed")
                    continue;
                block.params.emplace_back (name, (float) blockXml->getDoubleAttribute (name, 0.0));
            }

            snapshot.blocks.push_back (std::move (block));
        }
    }

    if (count != nullptr)
        *count = snapshotsXml->hasAttribute ("count")
                     ? juce::jlimit (currentIndex + 1, maxSnapshots, snapshotsXml->getIntAttribute ("count"))
                     : inferCount (snapshots, currentIndex);

    return true;
}

int inferCount (const std::array<Snapshot, maxSnapshots>& snapshots, int currentIndex)
{
    const auto sameBlocks = [] (const Snapshot& a, const Snapshot& b)
    {
        if (a.blocks.size() != b.blocks.size())
            return false;
        for (size_t i = 0; i < a.blocks.size(); ++i)
            if (a.blocks[i].bypassed != b.blocks[i].bypassed || a.blocks[i].params != b.blocks[i].params)
                return false;
        return true;
    };

    int count = juce::jlimit (0, maxSnapshots - 1, currentIndex) + 1;
    for (int i = 1; i < maxSnapshots; ++i)
    {
        const auto& s = snapshots[(size_t) i];
        if (s.name.isNotEmpty() || s.colour.isNotEmpty() || ! sameBlocks (s, snapshots[0]))
            count = juce::jmax (count, i + 1);
    }
    return count;
}

juce::String letterFor (int index)
{
    return index >= 0 && index < maxSnapshots
               ? juce::String::charToString ((juce::juce_wchar) ('A' + index))
               : juce::String();
}

juce::Colour defaultColourFor (int index)
{
    static const std::array<juce::Colour, maxSnapshots> palette {
        juce::Colour (0xff3d9970), // A green
        juce::Colour (0xff0074d9), // B blue
        juce::Colour (0xffff851b), // C orange
        juce::Colour (0xffb10dc9), // D purple
        juce::Colour (0xffffdc00), // E yellow
        juce::Colour (0xffff4136), // F red
        juce::Colour (0xff39cccc), // G teal
        juce::Colour (0xfff012be)  // H magenta
    };

    return index >= 0 && index < maxSnapshots ? palette[(size_t) index] : palette[0];
}

int indexForCcValue (int ccValue)
{
    return ccValue >= 0 && ccValue < maxSnapshots ? ccValue : -1;
}

} // namespace openguitarmultifx::snapshotxml
