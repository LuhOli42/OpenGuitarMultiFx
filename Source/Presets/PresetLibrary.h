#pragma once

#include "PresetManager.h"
#include "PresetMetadataXml.h"

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    The preset browser's data model: scans the presets directory and answers
    every organisational question the grid UI asks -- tags, bank/slot
    assignments, setlists and their order, search + tag filtering -- plus the
    small writes that edit those fields in place.

    Same "control-thread only, plain XML" discipline as PresetManager, which
    it composes rather than extends: PresetManager stays the dumb file-I/O
    layer, this is the layer that understands the metadata schema
    (PresetMetadataXml.h) and nothing about the sound content (it never
    touches <Block>/<Routing>/<MidiLearn>).

    Pedalboard-preset-count scale (dozens, not thousands): every read scans
    the directory fresh rather than caching, so the browser can never show
    stale metadata after a save/delete/rename.
*/
class PresetLibrary
{
public:
    /** One saved preset's organisational view -- everything the browser
        shows about it beyond its name. */
    struct Entry
    {
        juce::String name;
        int number = 0;                     // the stable preset number (PresetManager::numberForExistingPreset)
        juce::StringArray tags;
        presetmeta::BankSlot bankSlot;      // .bank < 0 = not in any bank
        std::vector<presetmeta::SetlistMember> setlists;

        bool isInSetlist (const juce::String& setlist) const;
    };

    explicit PresetLibrary (juce::File directoryToUse);

    /** Every saved preset, sorted by (number, name) -- numbered order like a
        hardware pedalboard's slot list, not alphabetical. Empty (not an
        error) when there are no presets yet. */
    std::vector<Entry> entries() const;

    /** Names matching a case-insensitive substring of the preset name AND
        (when `requiredTag` isn't empty) carrying that tag. Empty text +
        empty tag returns everything, in entries() order. */
    juce::StringArray search (const juce::String& text, const juce::String& requiredTag) const;

    /** Every tag in use across all presets, sorted -- what the browser's tag
        filter offers. */
    juce::StringArray allTags() const;

    // ---- Banks (one tile per foot-switch position) ----

    static constexpr int maxSlotsPerBank = 8;

    /** The preset assigned to (bank, slot), or empty. */
    juce::String presetInSlot (int bank, int slot) const;

    /** Puts `name` in (bank, slot). Evicts whatever preset already held that
        slot -- a foot-switch position can only mean one preset. False if
        `name` isn't a saved preset. */
    bool assignToSlot (const juce::String& name, int bank, int slot) const;

    bool clearSlotAssignment (const juce::String& name) const;

    // ---- Setlists ----

    /** Every setlist name in use, sorted. A setlist exists only through its
        members -- remove the last one and it's gone. */
    juce::StringArray setlistNames() const;

    /** The setlist's members in performance order (sorted on the stored
        index, then preset number as a tiebreak for legacy/duplicate
        indices). */
    juce::StringArray setlistMembers (const juce::String& setlist) const;

    /** The member `delta` steps after `currentName` in the setlist, wrapping
        around at both ends like a hardware up/down foot-switch. `currentName`
        not being a member (or empty) steps to the first/last member instead.
        Empty when the setlist is empty. */
    juce::String stepSetlist (const juce::String& setlist, const juce::String& currentName, int delta) const;

    /** Appends `preset` at the end of `setlist` (creating the setlist). A
        preset can sit in several setlists and, within one setlist, only
        once -- re-adding is a no-op that still returns true. */
    bool addToSetlist (const juce::String& preset, const juce::String& setlist) const;

    bool removeFromSetlist (const juce::String& preset, const juce::String& setlist) const;

    /** Swaps `preset` with the member `delta` positions away in the setlist
        (+1 = later, -1 = earlier). No-op (returns false) at the ends -- no
        wrap on manual reordering. */
    bool moveInSetlist (const juce::String& setlist, const juce::String& preset, int delta) const;

    /** Renames the setlist across every member preset. False if the new name
        is taken or the setlist doesn't exist. */
    bool renameSetlist (const juce::String& oldName, const juce::String& newName) const;

    /** Removes the setlist from every member (the presets themselves stay). */
    bool deleteSetlist (const juce::String& setlist) const;

    // ---- Tags ----

    /** Replaces the preset's whole tag list. False if `name` isn't a saved preset. */
    bool setTags (const juce::String& name, const juce::StringArray& tags) const;

    /** Removes bank assignment + every setlist membership but keeps tags --
        what a duplicated preset should get: the copy is a different preset
        and must not steal the original's foot-switch slot or setlist seat. */
    bool clearMemberships (const juce::String& name) const;

private:
    /** Loads `name`'s XML, runs `edit` on it and saves it back. False when
        the preset doesn't exist or won't parse. */
    bool editPresetXml (const juce::String& name, const std::function<void (juce::XmlElement&)>& edit) const;

    PresetManager presets;
};

} // namespace openguitarmultifx
