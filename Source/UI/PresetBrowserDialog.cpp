#include "PresetBrowserDialog.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    juce::Colour cardBackground() { return theme::current().surface; }
    juce::Colour tileBackground() { return theme::current().raised; }
    constexpr int tileGap = 12;
    constexpr int longPressMs = 550; // same duration ParameterPanel's LongPressLabel uses for MIDI Learn
}

//==============================================================================
// PresetTile
//==============================================================================
void PresetBrowserDialog::PresetTile::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    const auto bounds = getLocalBounds().toFloat();
    const bool boldCurrent = isCurrent && t.id == theme::Id::boldBlocks && ! isEmpty;

    if (isEmpty)
    {
        g.setColour (t.surface);
        g.fillRoundedRectangle (bounds, t.radius);
        g.setColour (t.lineStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, 1.0f);
        if (heading.isNotEmpty())
        {
            g.setColour (t.textMuted);
            g.setFont (overlaychrome::font (12.0f, true));
            g.drawFittedText (heading, bounds.toNearestInt().removeFromTop (24), juce::Justification::centred, 1);
        }
        g.setColour (t.textDim);
        g.setFont (28.0f);
        g.drawText ("+", bounds.toNearestInt(), juce::Justification::centred);
        return;
    }

    g.setColour (boldCurrent ? accent : t.raised);
    g.fillRoundedRectangle (bounds, t.radius);

    if (isCurrent && ! boldCurrent)
    {
        g.setColour (accent.withAlpha (0.18f));
        g.drawRoundedRectangle (bounds.expanded (2.0f).reduced (1.0f), t.radius + 2.0f, 2.0f);
    }
    g.setColour (isCurrent ? accent : t.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, isCurrent ? 2.0f : 1.0f);

    auto area = bounds.reduced (14.0f, 12.0f);
    const auto bottomArea = area.removeFromBottom (16.0f);
    const auto headingArea = area.removeFromTop (18.0f);

    g.setColour (boldCurrent ? juce::Colours::black.withAlpha (0.62f) : t.textMuted);
    g.setFont (overlaychrome::font (12.0f, true));
    g.drawFittedText (heading, headingArea.toNearestInt(), juce::Justification::centredLeft, 1);

    g.setColour (boldCurrent ? juce::Colours::black : t.text);
    g.setFont (overlaychrome::font (17.0f, true));
    g.drawFittedText (name, area.toNearestInt(), juce::Justification::centredLeft, 2);

    if (subline.isNotEmpty())
    {
        g.setColour (boldCurrent ? juce::Colours::black.withAlpha (0.68f) : t.textMuted);
        g.setFont (overlaychrome::font (11.0f));
        g.drawFittedText (subline, bottomArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }
}

void PresetBrowserDialog::PresetTile::mouseDown (const juce::MouseEvent&)
{
    startTimer (longPressMs);
}

void PresetBrowserDialog::PresetTile::mouseUp (const juce::MouseEvent& event)
{
    const bool wasLongPress = ! isTimerRunning();
    stopTimer();

    if (event.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }

    if (! wasLongPress && ! event.mouseWasDraggedSinceMouseDown() && onTap)
        onTap();
}

void PresetBrowserDialog::PresetTile::timerCallback()
{
    stopTimer();
    if (onMenu)
        onMenu();
}

//==============================================================================
// TileGrid
//==============================================================================
void PresetBrowserDialog::TileGrid::clearTiles()
{
    tiles.clear (true);
}

PresetBrowserDialog::PresetTile& PresetBrowserDialog::TileGrid::addTile()
{
    auto* tile = tiles.add (new PresetTile());
    addAndMakeVisible (*tile);
    tile->onTap = [this, tile]
    {
        if (onTileTap)
            onTileTap (*tile);
    };
    tile->onMenu = [this, tile]
    {
        if (onTileMenu)
            onTileMenu (*tile);
    };
    return *tile;
}

int PresetBrowserDialog::TileGrid::layoutForWidth (int width)
{
    constexpr int tileH = 118;
    const int cols = juce::jmin (preferredColumns,
                                 juce::jmax (1, (width + tileGap) / (180 + tileGap)));
    const int rows = (tiles.size() + cols - 1) / cols;
    const int tileW = (width - tileGap * (cols - 1)) / cols;

    for (int i = 0; i < tiles.size(); ++i)
    {
        const int row = i / cols, col = i % cols;
        tiles[i]->setBounds (col * (tileW + tileGap), row * (tileH + tileGap), tileW, tileH);
    }

    return juce::jmax (rows, 1) * (tileH + tileGap);
}

//==============================================================================
// PresetBrowserDialog
//==============================================================================
PresetBrowserDialog::PresetBrowserDialog (PresetLibrary& libraryToUse)
    : library (libraryToUse)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Presets", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    auto setupModeButton = [this] (overlaychrome::ActionButton& button, Mode buttonMode)
    {
        addAndMakeVisible (button);
        button.selectedStyle = overlaychrome::SelectedStyle::tab;
        button.mutedIdle = true;
        button.selected = mode == buttonMode;
        button.onClick = [this, buttonMode] { setMode (buttonMode); };
    };
    setupModeButton (allModeButton, Mode::all);
    setupModeButton (bankModeButton, Mode::bank);
    setupModeButton (setlistModeButton, Mode::setlist);

    addAndMakeVisible (newPresetButton);
    newPresetButton.onClick = [this] { if (onNewRequested) onNewRequested(); };

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search presets...", theme::current().textMuted);
    overlaychrome::drawSearchFieldChrome (searchField);
    searchField.onTextChange = [this] { rebuildTiles(); };

    addAndMakeVisible (tagBox);
    tagBox.onChange = [this] { rebuildTiles(); };

    addAndMakeVisible (bankPrevButton);
    bankPrevButton.onClick = [this] { currentBank = juce::jmax (0, currentBank - 1); rebuildTiles(); };
    addAndMakeVisible (bankNextButton);
    bankNextButton.onClick = [this] { ++currentBank; rebuildTiles(); };
    addAndMakeVisible (bankLabel);
    bankLabel.setJustificationType (juce::Justification::centred);
    bankLabel.setFont (juce::Font (17.0f, juce::Font::bold));

    auto setupSizeButton = [this] (overlaychrome::ActionButton& button, int size)
    {
        addAndMakeVisible (button);
        button.onClick = [this, size] { bankSize = size; rebuildTiles(); };
    };
    setupSizeButton (bankSize4Button, 4);
    setupSizeButton (bankSize8Button, 8);

    addAndMakeVisible (setlistPrevButton);
    setlistPrevButton.onClick = [this]
    {
        const auto names = library.setlistNames();
        if (names.isEmpty())
            return;
        const int i = names.indexOf (currentSetlist);
        currentSetlist = names[i <= 0 ? names.size() - 1 : i - 1];
        rebuildTiles();
    };
    addAndMakeVisible (setlistNextButton);
    setlistNextButton.onClick = [this]
    {
        const auto names = library.setlistNames();
        if (names.isEmpty())
            return;
        const int i = names.indexOf (currentSetlist);
        currentSetlist = names[i < 0 || i >= names.size() - 1 ? 0 : i + 1];
        rebuildTiles();
    };
    addAndMakeVisible (setlistLabel);
    setlistLabel.setJustificationType (juce::Justification::centred);
    setlistLabel.setFont (juce::Font (17.0f, juce::Font::bold));

    addAndMakeVisible (setlistMenuButton);
    setlistMenuButton.onClick = [this]
    {
        if (currentSetlist.isEmpty())
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "Rename setlist...");
        menu.addItem (2, "Delete setlist");
        menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget)
                                                       .withTargetComponent (&setlistMenuButton),
            [this] (int result)
            {
                if (result == 1 && onRenameSetlistRequested)
                    onRenameSetlistRequested (currentSetlist);
                else if (result == 2 && onDeleteSetlistRequested)
                    onDeleteSetlistRequested (currentSetlist);
            });
    };

    addAndMakeVisible (stepPrevButton);
    stepPrevButton.onClick = [this]
    {
        if (currentSetlist.isNotEmpty() && onStepSetlist)
            onStepSetlist (currentSetlist, -1);
    };
    addAndMakeVisible (stepNextButton);
    stepNextButton.onClick = [this]
    {
        if (currentSetlist.isNotEmpty() && onStepSetlist)
            onStepSetlist (currentSetlist, 1);
    };

    tileGrid.onTileTap = [this] (PresetTile& tile)
    {
        if (! tile.isEmpty && tile.name.isNotEmpty() && onPresetChosen)
            onPresetChosen (tile.name);
    };
    tileGrid.onTileMenu = [this] (PresetTile& tile)
    {
        for (auto& e : library.entries())
            if (e.name == tile.name)
            {
                showTileMenu (e);
                return;
            }
    };

    addAndMakeVisible (gridViewport);
    gridViewport.setScrollBarsShown (true, false);
    gridViewport.setViewedComponent (&tileGrid, false);

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

    addAndMakeVisible (hintLabel);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
    hintLabel.setFont (13.0f);

    addAndMakeVisible (nameField);
    nameField.setTextToShowWhenEmpty ("Preset name...", theme::current().textDim);
    nameField.onReturnKey = [this] { saveButton.triggerClick(); };

    addAndMakeVisible (saveButton);
    saveButton.onClick = [this]
    {
        const auto n = nameField.getText().trim();
        if (n.isNotEmpty() && onSaveRequested)
            onSaveRequested (n);
    };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    refreshTagFilter();
    refresh();
    setSize (1180, 780);
}

juce::String PresetBrowserDialog::currentPresetName() const
{
    return getCurrentPresetName != nullptr ? getCurrentPresetName() : juce::String {};
}

void PresetBrowserDialog::setMode (Mode newMode)
{
    mode = newMode;
    allModeButton.selected = mode == Mode::all;
    bankModeButton.selected = mode == Mode::bank;
    setlistModeButton.selected = mode == Mode::setlist;
    allModeButton.repaint();
    bankModeButton.repaint();
    setlistModeButton.repaint();
    rebuildTiles();
}

void PresetBrowserDialog::refresh()
{
    refreshTagFilter();
    rebuildTiles();
}

void PresetBrowserDialog::refreshTagFilter()
{
    const auto selectedTag = tagBox.getSelectedId() > 1 ? tagBox.getText() : juce::String {};
    tagBox.clear (juce::dontSendNotification);
    tagBox.addItem ("All tags", 1);

    int itemId = 1, idToSelect = 1;
    for (auto& tag : library.allTags())
    {
        tagBox.addItem (tag, ++itemId);
        if (tag == selectedTag)
            idToSelect = itemId;
    }
    tagBox.setSelectedId (idToSelect, juce::dontSendNotification);
}

void PresetBrowserDialog::configureTile (PresetTile& tile, const PresetLibrary::Entry& entry, const juce::String& heading)
{
    tile.name = entry.name;
    tile.heading = heading;
    tile.subline = entry.tags.joinIntoString ("  ");
    tile.isCurrent = (entry.name == currentPresetName());
    tile.isEmpty = false;
}

void PresetBrowserDialog::rebuildTiles()
{
    bankSize4Button.kind = bankSize == 4 ? overlaychrome::ButtonKind::primary : overlaychrome::ButtonKind::normal;
    bankSize8Button.kind = bankSize == 8 ? overlaychrome::ButtonKind::primary : overlaychrome::ButtonKind::normal;
    bankSize4Button.repaint();
    bankSize8Button.repaint();

    const bool allVisible = (mode == Mode::all);
    const bool bankVisible = (mode == Mode::bank);
    const bool setlistVisible = (mode == Mode::setlist);

    searchField.setVisible (allVisible);
    tagBox.setVisible (allVisible);
    bankPrevButton.setVisible (bankVisible);
    bankNextButton.setVisible (bankVisible);
    bankLabel.setVisible (bankVisible);
    bankSize4Button.setVisible (bankVisible);
    bankSize8Button.setVisible (bankVisible);
    setlistPrevButton.setVisible (setlistVisible);
    setlistNextButton.setVisible (setlistVisible);
    setlistLabel.setVisible (setlistVisible);
    setlistMenuButton.setVisible (setlistVisible);
    stepPrevButton.setVisible (setlistVisible);
    stepNextButton.setVisible (setlistVisible);

    bankTiles.clear (true);
    tileGrid.clearTiles();

    const auto allEntries = library.entries(); // one directory scan per refresh

    if (allVisible)
    {
        const auto selectedTag = tagBox.getSelectedId() > 1 ? tagBox.getText() : juce::String {};
        const auto names = library.search (searchField.getText(), selectedTag);
        for (auto& e : allEntries)
            if (names.contains (e.name))
                configureTile (tileGrid.addTile(), e, "#" + juce::String (e.number));

        emptyStateLabel.setText (allEntries.empty() ? "No presets saved yet."
                                 : names.isEmpty()   ? "No presets match."
                                                     : juce::String {},
                                 juce::dontSendNotification);
        hintLabel.setText ("Tap to load -- long-press a tile for tags, setlists, bank slots", juce::dontSendNotification);
    }
    else if (bankVisible)
    {
        rebuildBankTiles (allEntries);
        emptyStateLabel.setText ({}, juce::dontSendNotification);
        hintLabel.setText ("Bank " + juce::String (currentBank + 1) + " -- tap a slot to load, tap + to assign a preset",
                           juce::dontSendNotification);
    }
    else
    {
        rebuildSetlistTiles (allEntries);
        hintLabel.setText ("Tap to load, Prev/Next to step through the setlist -- long-press to reorder or remove",
                           juce::dontSendNotification);
    }

    emptyStateLabel.setVisible (emptyStateLabel.getText().isNotEmpty());
    resized();
}

presetmeta::BankSlot PresetBrowserDialog::storedSlotForTile (int tileIndex) const
{
    const int linear = currentBank * bankSize + tileIndex;
    return { linear / PresetLibrary::maxSlotsPerBank, linear % PresetLibrary::maxSlotsPerBank };
}

void PresetBrowserDialog::rebuildBankTiles (const std::vector<PresetLibrary::Entry>& allEntries)
{
    bankLabel.setText ("Bank " + juce::String (currentBank + 1), juce::dontSendNotification);

    for (int slot = 0; slot < bankSize; ++slot)
    {
        auto* tile = bankTiles.add (new PresetTile());
        addAndMakeVisible (*tile);

        const auto stored = storedSlotForTile (slot);
        const PresetLibrary::Entry* occupant = nullptr;
        for (auto& e : allEntries)
            if (e.bankSlot.bank == stored.bank && e.bankSlot.slot == stored.slot)
            {
                occupant = &e;
                break;
            }

        if (occupant == nullptr)
        {
            tile->isEmpty = true;
            tile->name = {};
            tile->heading = "Slot " + juce::String (slot + 1);
            tile->onTap = [this, stored] { showSlotPicker (stored.bank, stored.slot); };
        }
        else
        {
            configureTile (*tile, *occupant, juce::String (slot + 1));
            const auto presetName = occupant->name;
            tile->onTap = [this, presetName] { if (onPresetChosen) onPresetChosen (presetName); };
            tile->onMenu = [this, presetName]
            {
                for (auto& e : library.entries())
                    if (e.name == presetName)
                    {
                        showTileMenu (e);
                        return;
                    }
            };
        }
    }
}

void PresetBrowserDialog::rebuildSetlistTiles (const std::vector<PresetLibrary::Entry>& allEntries)
{
    juce::StringArray names;
    for (auto& e : allEntries)
        for (auto& m : e.setlists)
            names.addIfNotAlreadyThere (m.name);
    names.sort (true);

    if (names.isEmpty())
    {
        currentSetlist.clear();
        setlistLabel.setText ("No setlists", juce::dontSendNotification);
        emptyStateLabel.setText ("No setlists yet -- add a preset to one from its tile menu (long-press).",
                                 juce::dontSendNotification);
        return;
    }

    if (! names.contains (currentSetlist))
        currentSetlist = names[0];

    setlistLabel.setText (currentSetlist, juce::dontSendNotification);

    // Members in (index, preset number) order -- the same ordering
    // PresetLibrary::setlistMembers computes, redone over the shared scan.
    std::vector<const PresetLibrary::Entry*> members;
    for (auto& e : allEntries)
        if (e.isInSetlist (currentSetlist))
            members.push_back (&e);
    std::sort (members.begin(), members.end(), [this] (const PresetLibrary::Entry* a, const PresetLibrary::Entry* b)
    {
        auto indexOf = [this] (const PresetLibrary::Entry* e)
        {
            for (auto& m : e->setlists)
                if (m.name == currentSetlist)
                    return m.index;
            return 0;
        };
        const auto ai = indexOf (a), bi = indexOf (b);
        return ai != bi ? ai < bi : a->number < b->number;
    });

    int position = 0;
    for (auto* e : members)
        configureTile (tileGrid.addTile(), *e, juce::String (++position));

    emptyStateLabel.setText (tileGrid.tiles.isEmpty() ? "This setlist is empty." : juce::String {},
                             juce::dontSendNotification);
}

void PresetBrowserDialog::showSlotPicker (int bank, int slot)
{
    const auto all = library.entries();
    if (all.empty())
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < (int) all.size(); ++i)
        menu.addItem (i + 1, "#" + juce::String (all[(size_t) i].number) + "  " + all[(size_t) i].name);

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, all, bank, slot] (int result)
        {
            if (result <= 0 || result > (int) all.size())
                return;
            library.assignToSlot (all[(size_t) result - 1].name, bank, slot);
            refresh();
        });
}

void PresetBrowserDialog::showAddToSetlistMenu (const juce::String& presetName)
{
    juce::PopupMenu menu;
    const auto setlists = library.setlistNames();
    for (int i = 0; i < setlists.size(); ++i)
        menu.addItem (i + 1, setlists[i]);
    if (! setlists.isEmpty())
        menu.addSeparator();
    menu.addItem (setlists.size() + 1, "New setlist...");

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, presetName, setlists] (int result)
        {
            if (result <= 0)
                return;
            if (result <= setlists.size())
            {
                library.addToSetlist (presetName, setlists[result - 1]);
                refresh();
            }
            else if (onNewSetlistRequested)
            {
                onNewSetlistRequested (presetName);
            }
        });
}

void PresetBrowserDialog::showAssignToBankMenu (const juce::String& presetName)
{
    juce::PopupMenu menu;
    const auto allEntries = library.entries();

    // Bank numbers here are DISPLAYED pages like the bank view's, so a pick
    // lands where the user just saw it: pages of `bankSize` tiles, linear
    // over the same stored space (8 stored banks). See storedSlotForTile.
    int itemId = 0;
    std::vector<std::pair<int, int>> idToBankSlot;
    const int numPages = (8 * PresetLibrary::maxSlotsPerBank) / bankSize;
    for (int page = 0; page < numPages; ++page)
    {
        juce::PopupMenu bankMenu;
        for (int slot = 0; slot < bankSize; ++slot)
        {
            const int linear = page * bankSize + slot;
            const presetmeta::BankSlot stored { linear / PresetLibrary::maxSlotsPerBank,
                                                linear % PresetLibrary::maxSlotsPerBank };
            juce::String occupant;
            for (auto& e : allEntries)
                if (e.bankSlot.bank == stored.bank && e.bankSlot.slot == stored.slot)
                {
                    occupant = e.name;
                    break;
                }
            ++itemId;
            idToBankSlot.push_back ({ stored.bank, stored.slot });
            bankMenu.addItem (itemId, "Slot " + juce::String (slot + 1)
                                  + (occupant.isNotEmpty() ? "  (" + occupant + ")" : ""));
        }
        menu.addSubMenu ("Bank " + juce::String (page + 1), bankMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, presetName, idToBankSlot] (int result)
        {
            if (result <= 0 || result > (int) idToBankSlot.size())
                return;
            const auto [bank, slot] = idToBankSlot[(size_t) result - 1];
            library.assignToSlot (presetName, bank, slot);
            refresh();
        });
}

void PresetBrowserDialog::showTileMenu (const PresetLibrary::Entry& entry)
{
    const auto presetName = entry.name;

    juce::PopupMenu menu;
    // Menu item IDs map 1:1 into this flat action list (same trick as
    // MainComponent::showAddEffectMenu's idToKey -- PopupMenu only reports
    // the chosen ID, never which submenu it lived in).
    std::vector<std::function<void()>> actions;
    auto item = [&menu, &actions] (const juce::String& label, std::function<void()> action, bool enabled = true)
    {
        actions.push_back (std::move (action));
        menu.addItem ((int) actions.size(), label, enabled);
    };

    item ("Load", [this, presetName] { if (onPresetChosen) onPresetChosen (presetName); });
    menu.addSeparator();

    if (mode == Mode::setlist && entry.isInSetlist (currentSetlist))
    {
        const auto members = library.setlistMembers (currentSetlist);
        const int pos = members.indexOf (presetName);
        item ("Move earlier", [this, presetName] { library.moveInSetlist (currentSetlist, presetName, -1); refresh(); },
              pos > 0);
        item ("Move later", [this, presetName] { library.moveInSetlist (currentSetlist, presetName, 1); refresh(); },
              pos >= 0 && pos < members.size() - 1);
        item ("Remove from \"" + currentSetlist + "\"",
              [this, presetName] { library.removeFromSetlist (presetName, currentSetlist); refresh(); });
        menu.addSeparator();
    }

    if (entry.bankSlot.bank >= 0)
        item ("Clear bank slot", [this, presetName] { library.clearSlotAssignment (presetName); refresh(); });

    item ("Add to setlist...", [this, presetName] { showAddToSetlistMenu (presetName); });
    item ("Assign to bank...", [this, presetName] { showAssignToBankMenu (presetName); });
    menu.addSeparator();
    item ("Rename...", [this, presetName] { if (onRenameRequested) onRenameRequested (presetName); });
    item ("Duplicate...", [this, presetName] { if (onDuplicateRequested) onDuplicateRequested (presetName); });
    item ("Edit tags...", [this, presetName] { if (onEditTagsRequested) onEditTagsRequested (presetName); });
    menu.addSeparator();
    item ("Delete...", [this, presetName] { if (onDeleteRequested) onDeleteRequested (presetName); });

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [actions] (int result)
        {
            if (result > 0 && result <= (int) actions.size())
                actions[(size_t) result - 1]();
        });
}

void PresetBrowserDialog::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (76).reduced (20, 0);

    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    titleLabel.setBounds (header.removeFromLeft (150));

    auto placeTab = [&header] (overlaychrome::ActionButton& button, int width)
    {
        button.setBounds (header.removeFromLeft (width).withSizeKeepingCentre (width, 48));
        header.removeFromLeft (8);
    };
    placeTab (newPresetButton, 88);
    placeTab (allModeButton, 84);
    placeTab (bankModeButton, 102);
    placeTab (setlistModeButton, 118);

    if (searchField.isVisible())
    {
        header.removeFromLeft (juce::jmax (0, header.getWidth() - 300));
        searchField.setBounds (header.removeFromRight (300).withSizeKeepingCentre (300, 52));
    }

    auto body = area;
    auto selectorRow = body.removeFromTop (56);

    if (mode == Mode::all)
    {
        tagBox.setBounds (selectorRow);
    }
    else if (mode == Mode::bank)
    {
        bankSize8Button.setBounds (selectorRow.removeFromRight (56).withSizeKeepingCentre (56, 48));
        selectorRow.removeFromRight (6);
        bankSize4Button.setBounds (selectorRow.removeFromRight (56).withSizeKeepingCentre (56, 48));
        selectorRow.removeFromRight (16);

        bankPrevButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 48));
        bankLabel.setBounds (selectorRow.removeFromLeft (180));
        bankNextButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 48));
    }
    else
    {
        stepNextButton.setBounds (selectorRow.removeFromRight (132).withSizeKeepingCentre (132, 48));
        selectorRow.removeFromRight (8);
        stepPrevButton.setBounds (selectorRow.removeFromRight (132).withSizeKeepingCentre (132, 48));
        selectorRow.removeFromRight (16);

        setlistMenuButton.setBounds (selectorRow.removeFromRight (48).withSizeKeepingCentre (48, 48));
        selectorRow.removeFromRight (8);

        setlistPrevButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 48));
        setlistNextButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 48));
        selectorRow.removeFromLeft (8);
        setlistLabel.setBounds (selectorRow);
    }

    body.removeFromTop (8);

    auto bottomRow = body.removeFromBottom (76);
    saveButton.setBounds (bottomRow.removeFromRight (150).withSizeKeepingCentre (150, 52));
    bottomRow.removeFromRight (10);
    hintLabel.setBounds (bottomRow.removeFromRight (juce::jmin (360, bottomRow.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (360, bottomRow.getWidth() / 3), 24));
    nameField.setBounds (bottomRow.withSizeKeepingCentre (bottomRow.getWidth(), 52));
    overlaychrome::drawSearchFieldChrome (nameField);
    body.removeFromBottom (10);

    if (mode == Mode::bank)
    {
        gridViewport.setVisible (false);
        const int n = juce::jmax (1, bankTiles.size());
        const int cols = 4;
        const int rows = (n + cols - 1) / cols;
        const int gap = 16;
        const int tileW = (body.getWidth() - gap * (cols - 1)) / cols;
        const int tileH = (body.getHeight() - gap * (rows - 1)) / rows;
        for (int i = 0; i < n; ++i)
        {
            const int row = i / cols, col = i % cols;
            bankTiles[i]->setBounds (body.getX() + col * (tileW + gap),
                                     body.getY() + row * (tileH + gap), tileW, tileH);
        }
    }
    else
    {
        for (auto* tile : bankTiles)
            tile->setVisible (false);

        gridViewport.setVisible (true);
        gridViewport.setBounds (body);
        emptyStateLabel.setBounds (body);

        const int contentWidth = juce::jmax (1, body.getWidth() - gridViewport.getScrollBarThickness());
        tileGrid.setSize (contentWidth, tileGrid.layoutForWidth (contentWidth));
    }

    emptyStateLabel.setVisible (emptyStateLabel.getText().isNotEmpty());
}

void PresetBrowserDialog::paint (juce::Graphics& g)
{
    overlaychrome::paintCardBackground (g, getLocalBounds().toFloat());
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 76.0f));
}

} // namespace openguitarmultifx
