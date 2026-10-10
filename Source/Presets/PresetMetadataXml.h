#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace openguitarmultifx::presetmeta
{

/**
    The preset file's ORGANISATIONAL metadata -- the things a preset browser
    needs that aren't part of the sound itself: tags, a bank/foot-switch slot
    assignment, and which setlists (at which positions) the preset belongs to.

    Everything here is additive to the preset XML and optional: a preset saved
    before any of this existed (or by a build without it) simply has none of
    these elements/attributes and reads as untagged + unassigned -- see
    docs/PresetsAndSnapshots.md stage 4.

    Stored per-preset-file so the metadata travels with the preset through
    PresetManager::renamePreset()/duplicatePreset() (both copy the whole XML)
    and needs no sidecar file to keep in sync. Pure XML transforms like
    midilearnxml, unit-testable headlessly.

    Schema (all optional):
      <Preset ... bank="2" slot="5">
        <Tags><Tag name="lead"/><Tag name="gig"/></Tags>
        <Setlists><Member name="Friday gig" index="0"/></Setlists>
      </Preset>
    bank/slot are 0-based; bank < 0 (or absent) means unassigned. A setlist is
    defined entirely by its member presets -- an ordered list is built by
    sorting members on (index, name), so it needs no definition file and an
    emptied setlist just stops existing.
*/

struct BankSlot
{
    int bank = -1;  // -1 = not assigned to any bank
    int slot = -1;  // 0..7 within the bank when assigned
};

struct SetlistMember
{
    juce::String name;  // the setlist this preset belongs to
    int index = 0;      // position inside it (0-based)
};

inline juce::StringArray readTags (const juce::XmlElement& presetXml)
{
    juce::StringArray tags;
    if (auto* tagsXml = presetXml.getChildByName ("Tags"))
        for (auto* tagXml : tagsXml->getChildWithTagNameIterator ("Tag"))
            tags.addIfNotAlreadyThere (tagXml->getStringAttribute ("name"));
    tags.removeEmptyStrings();
    return tags;
}

/** Replaces (or removes, when `tags` is empty) the preset's <Tags> element. */
inline void writeTags (juce::XmlElement& presetXml, const juce::StringArray& tags)
{
    presetXml.deleteAllChildElementsWithTagName ("Tags");

    juce::StringArray clean;
    for (auto& t : tags)
        clean.addIfNotAlreadyThere (t.trim());
    clean.removeEmptyStrings();
    if (clean.isEmpty())
        return;

    auto* tagsXml = presetXml.createNewChildElement ("Tags");
    for (auto& t : clean)
    {
        auto* tagXml = tagsXml->createNewChildElement ("Tag");
        tagXml->setAttribute ("name", t);
    }
}

inline BankSlot readBankSlot (const juce::XmlElement& presetXml)
{
    BankSlot result;
    result.bank = presetXml.getIntAttribute ("bank", -1);
    result.slot = presetXml.getIntAttribute ("slot", -1);
    if (result.bank < 0 || result.slot < 0)
        result = {};
    return result;
}

/** Both indices 0-based; pass a negative bank/slot to clear the assignment
    (the attributes are removed, not written as -1, so an unassigned preset
    looks exactly like one from before banks existed). */
inline void writeBankSlot (juce::XmlElement& presetXml, int bank, int slot)
{
    if (bank >= 0 && slot >= 0)
    {
        presetXml.setAttribute ("bank", bank);
        presetXml.setAttribute ("slot", slot);
    }
    else
    {
        presetXml.removeAttribute ("bank");
        presetXml.removeAttribute ("slot");
    }
}

inline std::vector<SetlistMember> readSetlists (const juce::XmlElement& presetXml)
{
    std::vector<SetlistMember> members;
    if (auto* setlistsXml = presetXml.getChildByName ("Setlists"))
        for (auto* memberXml : setlistsXml->getChildWithTagNameIterator ("Member"))
            if (memberXml->getStringAttribute ("name").isNotEmpty())
                members.push_back ({ memberXml->getStringAttribute ("name"),
                                     memberXml->getIntAttribute ("index", 0) });
    return members;
}

/** Replaces (or removes, when `members` is empty) the preset's <Setlists> element. */
inline void writeSetlists (juce::XmlElement& presetXml, const std::vector<SetlistMember>& members)
{
    presetXml.deleteAllChildElementsWithTagName ("Setlists");

    if (members.empty())
        return;

    auto* setlistsXml = presetXml.createNewChildElement ("Setlists");
    for (auto& m : members)
    {
        if (m.name.trim().isEmpty())
            continue;
        auto* memberXml = setlistsXml->createNewChildElement ("Member");
        memberXml->setAttribute ("name", m.name);
        memberXml->setAttribute ("index", juce::jmax (0, m.index));
    }
}

/** Carries the organisational metadata from a previously-saved preset file
    into the fresh XML about to replace it. MainComponent::buildPresetXml()
    only captures the SOUND (blocks, routing, MIDI Learn) -- a resave would
    otherwise silently drop every tag/bank/setlist edit the browser made. */
inline void copyOrganisation (const juce::XmlElement& from, juce::XmlElement& to)
{
    writeTags (to, readTags (from));

    const auto bankSlot = readBankSlot (from);
    writeBankSlot (to, bankSlot.bank, bankSlot.slot);

    writeSetlists (to, readSetlists (from));
}

} // namespace openguitarmultifx::presetmeta
