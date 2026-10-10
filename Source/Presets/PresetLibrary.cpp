#include "PresetLibrary.h"

namespace openguitarmultifx
{

bool PresetLibrary::Entry::isInSetlist (const juce::String& setlist) const
{
    for (auto& m : setlists)
        if (m.name == setlist)
            return true;
    return false;
}

PresetLibrary::PresetLibrary (juce::File directoryToUse)
    : presets (std::move (directoryToUse))
{
}

std::vector<PresetLibrary::Entry> PresetLibrary::entries() const
{
    std::vector<Entry> result;

    for (auto& name : presets.listPresetNames())
    {
        auto xml = presets.loadPreset (name);
        if (xml == nullptr)
            continue; // a file that won't parse isn't shown rather than breaking the whole browser

        Entry e;
        e.name = name;
        e.number = xml->getIntAttribute ("number", 0);
        e.tags = presetmeta::readTags (*xml);
        e.bankSlot = presetmeta::readBankSlot (*xml);
        e.setlists = presetmeta::readSetlists (*xml);
        result.push_back (std::move (e));
    }

    std::sort (result.begin(), result.end(), [] (const Entry& a, const Entry& b)
    {
        return a.number != b.number ? a.number < b.number : a.name < b.name;
    });
    return result;
}

juce::StringArray PresetLibrary::search (const juce::String& text, const juce::String& requiredTag) const
{
    juce::StringArray names;
    const auto needle = text.trim().toLowerCase();

    for (auto& e : entries())
    {
        if (needle.isNotEmpty() && ! e.name.toLowerCase().contains (needle))
            continue;
        if (requiredTag.isNotEmpty() && ! e.tags.contains (requiredTag))
            continue;
        names.add (e.name);
    }
    return names;
}

juce::StringArray PresetLibrary::allTags() const
{
    juce::StringArray tags;
    for (auto& e : entries())
        for (auto& t : e.tags)
            tags.addIfNotAlreadyThere (t);
    tags.sort (true);
    return tags;
}

juce::String PresetLibrary::presetInSlot (int bank, int slot) const
{
    for (auto& e : entries())
        if (e.bankSlot.bank == bank && e.bankSlot.slot == slot)
            return e.name;
    return {};
}

bool PresetLibrary::assignToSlot (const juce::String& name, int bank, int slot) const
{
    if (! presets.presetExists (name) || bank < 0 || slot < 0 || slot >= maxSlotsPerBank)
        return false;

    // The slot can only mean one preset -- clear whoever held it (and this
    // preset's old slot, so it isn't in two places at once).
    for (auto& e : entries())
        if (e.bankSlot.bank >= 0 && (e.name == name || (e.bankSlot.bank == bank && e.bankSlot.slot == slot)))
            editPresetXml (e.name, [] (juce::XmlElement& xml) { presetmeta::writeBankSlot (xml, -1, -1); });

    return editPresetXml (name, [bank, slot] (juce::XmlElement& xml) { presetmeta::writeBankSlot (xml, bank, slot); });
}

bool PresetLibrary::clearSlotAssignment (const juce::String& name) const
{
    return editPresetXml (name, [] (juce::XmlElement& xml) { presetmeta::writeBankSlot (xml, -1, -1); });
}

juce::StringArray PresetLibrary::setlistNames() const
{
    juce::StringArray names;
    for (auto& e : entries())
        for (auto& m : e.setlists)
            names.addIfNotAlreadyThere (m.name);
    names.sort (true);
    return names;
}

juce::StringArray PresetLibrary::setlistMembers (const juce::String& setlist) const
{
    std::vector<Entry> members;
    for (auto& e : entries())
        if (e.isInSetlist (setlist))
            members.push_back (e);

    std::sort (members.begin(), members.end(), [&setlist] (const Entry& a, const Entry& b)
    {
        int ai = 0, bi = 0;
        for (auto& m : a.setlists) if (m.name == setlist) { ai = m.index; break; }
        for (auto& m : b.setlists) if (m.name == setlist) { bi = m.index; break; }
        return ai != bi ? ai < bi : a.number < b.number;
    });

    juce::StringArray names;
    for (auto& e : members)
        names.add (e.name);
    return names;
}

juce::String PresetLibrary::stepSetlist (const juce::String& setlist, const juce::String& currentName, int delta) const
{
    const auto members = setlistMembers (setlist);
    if (members.isEmpty())
        return {};

    const int n = members.size();
    const int current = members.indexOf (currentName);

    if (current < 0)
        return delta >= 0 ? members[0] : members[n - 1];

    return members[((current + delta) % n + n) % n];
}

namespace
{
    /** Applies `rewrite` to this preset's <Setlists> content and writes it back. */
    void rewriteMemberships (juce::XmlElement& xml, const std::function<void (std::vector<presetmeta::SetlistMember>&)>& rewrite)
    {
        auto members = presetmeta::readSetlists (xml);
        rewrite (members);
        presetmeta::writeSetlists (xml, members);
    }
}

bool PresetLibrary::addToSetlist (const juce::String& preset, const juce::String& setlist) const
{
    if (setlist.trim().isEmpty() || ! presets.presetExists (preset))
        return false;

    if (setlistMembers (setlist).contains (preset))
        return true; // already a member -- one seat per preset per setlist

    // The new member's seat is one past the highest index the setlist
    // currently has across EVERY member file (each preset carries only its
    // own seats, so the end of the list has to be found by scanning all).
    int next = 0;
    for (auto& e : entries())
        for (auto& m : e.setlists)
            if (m.name == setlist)
                next = juce::jmax (next, m.index + 1);

    return editPresetXml (preset, [&setlist, next] (juce::XmlElement& xml)
    {
        rewriteMemberships (xml, [&setlist, next] (std::vector<presetmeta::SetlistMember>& members)
        {
            members.push_back ({ setlist, next });
        });
    });
}

bool PresetLibrary::removeFromSetlist (const juce::String& preset, const juce::String& setlist) const
{
    return editPresetXml (preset, [&setlist] (juce::XmlElement& xml)
    {
        rewriteMemberships (xml, [&setlist] (std::vector<presetmeta::SetlistMember>& members)
        {
            std::erase_if (members, [&setlist] (const presetmeta::SetlistMember& m) { return m.name == setlist; });
        });
    });
}

bool PresetLibrary::moveInSetlist (const juce::String& setlist, const juce::String& preset, int delta) const
{
    auto members = setlistMembers (setlist);
    const int from = members.indexOf (preset);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= members.size())
        return false;

    // Swap the two members' stored indices -- each file keeps its own seat.
    const auto other = members[(size_t) to];
    int fromIndex = 0, otherIndex = 0;
    for (auto& e : entries())
    {
        for (auto& m : e.setlists)
        {
            if (m.name != setlist)
                continue;
            if (e.name == preset)
                fromIndex = m.index;
            if (e.name == other)
                otherIndex = m.index;
        }
    }

    editPresetXml (preset, [&] (juce::XmlElement& xml)
    {
        rewriteMemberships (xml, [&] (std::vector<presetmeta::SetlistMember>& ms)
        {
            for (auto& m : ms) if (m.name == setlist) m.index = otherIndex;
        });
    });
    editPresetXml (other, [&] (juce::XmlElement& xml)
    {
        rewriteMemberships (xml, [&] (std::vector<presetmeta::SetlistMember>& ms)
        {
            for (auto& m : ms) if (m.name == setlist) m.index = fromIndex;
        });
    });
    return true;
}

bool PresetLibrary::renameSetlist (const juce::String& oldName, const juce::String& newName) const
{
    if (newName.trim().isEmpty() || newName == oldName)
        return false;
    if (setlistNames().contains (newName) || ! setlistNames().contains (oldName))
        return false;

    for (auto& member : setlistMembers (oldName))
        editPresetXml (member, [&] (juce::XmlElement& xml)
        {
            rewriteMemberships (xml, [&] (std::vector<presetmeta::SetlistMember>& ms)
            {
                for (auto& m : ms) if (m.name == oldName) m.name = newName;
            });
        });
    return true;
}

bool PresetLibrary::deleteSetlist (const juce::String& setlist) const
{
    const auto members = setlistMembers (setlist);
    if (members.isEmpty())
        return false;
    for (auto& member : members)
        removeFromSetlist (member, setlist);
    return true;
}

bool PresetLibrary::setTags (const juce::String& name, const juce::StringArray& tags) const
{
    return editPresetXml (name, [&tags] (juce::XmlElement& xml) { presetmeta::writeTags (xml, tags); });
}

bool PresetLibrary::clearMemberships (const juce::String& name) const
{
    return editPresetXml (name, [] (juce::XmlElement& xml)
    {
        presetmeta::writeBankSlot (xml, -1, -1);
        presetmeta::writeSetlists (xml, {});
    });
}

bool PresetLibrary::editPresetXml (const juce::String& name, const std::function<void (juce::XmlElement&)>& edit) const
{
    auto xml = presets.loadPreset (name);
    if (xml == nullptr)
        return false;
    edit (*xml);
    return presets.savePreset (name, *xml);
}

} // namespace openguitarmultifx
