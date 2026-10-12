#pragma once

#include "../Presets/PresetLibrary.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    The stage-4 preset browser (docs/PresetsAndSnapshots.md): a grid of preset
    tiles instead of the old pick-a-row list, opened from the preset badge as
    an OverlayHost card like every other dialog here.

    Three modes sharing the one card:
      - All: every preset as a tile, filtered live by the search field and
        the tag dropdown. Tap a tile = load it.
      - Banks: one row of 4 or 8 slots per bank -- the performance layout,
        one tile per foot-switch position. Tap a filled slot = load; tap an
        empty "+" slot = pick a preset to assign.
      - Setlists: the members of one named setlist in order, with Prev/Next
        step buttons for stepping through it live (the browser stays open).

    Tile management (rename, duplicate, delete, tags, setlist add/remove,
    bank-slot clearing) sits behind a long-press on the tile -- same hidden
    gesture the app already uses for MIDI Learn on knob labels -- so the
    grid itself keeps exactly one meaning for a tap: load.
*/
class PresetBrowserDialog : public juce::Component
{
public:
    PresetBrowserDialog (PresetLibrary& libraryToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Re-scans the library and rebuilds the tiles -- call after anything
        mutates the preset store underneath the dialog. */
    void refresh();

    // ---- callbacks wired by MainComponent ----

    /** Fires when a preset tile is tapped (load this preset). */
    std::function<void (juce::String presetName)> onPresetChosen;
    /** The 'New' button in the title row (moved here off the top bar per user
        request 2026-10-12: "na tab de presets, do lado do all") -- the owner
        clears the board to a fresh unnamed preset, asking to save first. */
    std::function<void()> onNewRequested;
    /** Fires when the user taps Save with a name typed in. */
    std::function<void (juce::String presetName)> onSaveRequested;
    /** Management actions routed back to the owner because they prompt and/or
        touch the loaded preset's state. */
    std::function<void (juce::String presetName)> onDeleteRequested;
    std::function<void (juce::String presetName)> onRenameRequested;
    std::function<void (juce::String presetName)> onDuplicateRequested;
    std::function<void (juce::String presetName)> onEditTagsRequested;
    /** "New setlist..." picked in a tile's Add-to-setlist menu -- needs a
        text prompt the owner hosts. */
    std::function<void (juce::String presetName)> onNewSetlistRequested;
    /** Rename/delete the setlist being browsed (prompted/confirmed by owner). */
    std::function<void (juce::String setlistName)> onRenameSetlistRequested;
    std::function<void (juce::String setlistName)> onDeleteSetlistRequested;
    /** Prev/Next step inside the browsed setlist: delta is +/-1. */
    std::function<void (juce::String setlistName, int delta)> onStepSetlist;
    std::function<void()> onPopOverlay;
    /** Whichever preset is loaded right now (drawn highlighted). */
    std::function<juce::String()> getCurrentPresetName;

private:
    /** One grid cell. Tap = onTap; press-and-hold (~550ms, same duration as
        ParameterPanel's long-press labels) or right-click = onMenu. An
        "empty" tile (a bank slot with nothing assigned) paints a dim "+". */
    class PresetTile : public juce::Component,
                       private juce::Timer
    {
    public:
        juce::String name;
        juce::String heading;   // top line: preset number / slot / setlist position
        juce::String subline;   // bottom line: tags, or setlist position info
        bool isCurrent = false;
        bool isEmpty = false;

        std::function<void()> onTap;
        std::function<void()> onMenu;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;

    private:
        void timerCallback() override;
    };

    /** Free-flowing rows of PresetTiles inside a Viewport. */
    class TileGrid : public juce::Component
    {
    public:
        juce::OwnedArray<PresetTile> tiles;
        std::function<void (PresetTile&)> onTileTap;
        std::function<void (PresetTile&)> onTileMenu;

        void clearTiles();
        PresetTile& addTile();
        /** Lays the tiles out across `width` and returns the height they
            occupy -- the caller then sizes this component to it. */
        int layoutForWidth (int width);
    };

    enum class Mode { all, bank, setlist };

    PresetLibrary& library;
    Mode mode = Mode::all;
    int currentBank = 0;
    int bankSize = 8;
    juce::String currentSetlist;

    /** Rebuilds the visible tiles for the current mode from the library.
        `allEntries` is the one directory scan shared by the whole refresh. */
    void rebuildTiles();
    void rebuildBankTiles (const std::vector<PresetLibrary::Entry>& allEntries);
    void rebuildSetlistTiles (const std::vector<PresetLibrary::Entry>& allEntries);
    void setMode (Mode newMode);

    void configureTile (PresetTile& tile, const PresetLibrary::Entry& entry, const juce::String& heading);
    /** The long-press tile menu; what it offers depends on the mode. */
    void showTileMenu (const PresetLibrary::Entry& entry);
    /** Which stored bank/slot a displayed tile stands for. Displayed pages
        are linear over the stored space: in 8-slot mode page N = stored bank
        N; in 4-slot mode pages 2N and 2N+1 show bank N's slots 0-3 and 4-7,
        so a slot assigned in one size stays reachable in the other. */
    presetmeta::BankSlot storedSlotForTile (int tileIndex) const;
    /** "+" slot tapped in bank mode: pick a preset to put there. */
    void showSlotPicker (int bank, int slot);
    void showAddToSetlistMenu (const juce::String& presetName);
    void showAssignToBankMenu (const juce::String& presetName);
    void refreshTagFilter();
    juce::String currentPresetName() const;

    juce::Label titleLabel;
    juce::TextButton allModeButton { "All" }, bankModeButton { "Banks" }, setlistModeButton { "Setlists" };
    juce::TextButton newPresetButton { "New" };

    // All-mode filter row
    juce::TextEditor searchField;
    juce::ComboBox tagBox;

    // Bank-mode selector row
    juce::TextButton bankPrevButton { juce::CharPointer_UTF8 ("\xe2\x97\x80") }; // "◀"
    juce::TextButton bankNextButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6") }; // "▶"
    juce::Label bankLabel;
    juce::TextButton bankSize4Button { "4" }, bankSize8Button { "8" };
    juce::OwnedArray<PresetTile> bankTiles;

    // Setlist-mode selector row
    juce::TextButton setlistPrevButton { juce::CharPointer_UTF8 ("\xe2\x97\x80") };
    juce::TextButton setlistNextButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6") };
    juce::Label setlistLabel;
    juce::TextButton setlistMenuButton { juce::String::fromUTF8 ("\xe2\x8b\xae") }; // "⋮"
    juce::TextButton stepPrevButton { juce::CharPointer_UTF8 ("\xe2\x97\x80 Prev") };
    juce::TextButton stepNextButton { juce::CharPointer_UTF8 ("Next \xe2\x96\xb6") };

    juce::Viewport gridViewport;
    TileGrid tileGrid;
    juce::Label emptyStateLabel;
    juce::Label hintLabel;

    juce::TextEditor nameField;
    juce::TextButton saveButton { "Save current" };
    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
