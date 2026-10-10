#include "PresetBrowserDialog.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    const juce::Colour cardBackground { 0xff1a1a1a };
    const juce::Colour tileBackground { 0xff242424 };
    const juce::Colour currentAccent { 0xff4db8a8 };
    constexpr int tileGap = 8;
    constexpr int longPressMs = 550; // same duration ParameterPanel's LongPressLabel uses for MIDI Learn
}

//==============================================================================
// PresetTile
//==============================================================================
void PresetBrowserDialog::PresetTile::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    if (isEmpty)
    {
        g.setColour (juce::Colour (0xff181818));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.5f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

        g.setColour (juce::Colours::grey);
        g.setFont (28.0f);
        g.drawText ("+", bounds.toNearestInt(), juce::Justification::centred);

        if (heading.isNotEmpty())
        {
            g.setFont (13.0f);
            g.drawText (heading, bounds.toNearestInt().removeFromTop (22), juce::Justification::centred);
        }
        return;
    }

    g.setColour (tileBackground);
    g.fillRoundedRectangle (bounds, 8.0f);

    if (isCurrent)
    {
        g.setColour (currentAccent.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds, 8.0f);
    }

    g.setColour (isCurrent ? currentAccent : juce::Colours::grey.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 8.0f, isCurrent ? 3.0f : 1.5f);

    auto area = bounds.toNearestInt().reduced (8, 4);

    if (heading.isNotEmpty())
    {
        g.setColour (currentAccent.withAlpha (isCurrent ? 1.0f : 0.75f));
        g.setFont (juce::Font (15.0f, juce::Font::bold));
        g.drawText (heading, area.removeFromTop (20), juce::Justification::centredLeft);
    }

    auto nameArea = area;
    if (subline.isNotEmpty())
        nameArea = area.removeFromTop (area.getHeight() - 16);

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText (name, nameArea, juce::Justification::centred, 2);

    if (subline.isNotEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (12.0f);
        g.drawFittedText (subline, area, juce::Justification::centredLeft, 1);
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
    constexpr int tileW = 168, tileH = 116;
    const int cols = juce::jmax (1, (width + tileGap) / (tileW + tileGap));
    const int rows = (tiles.size() + cols - 1) / cols;

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
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    auto setupModeButton = [this] (juce::TextButton& button, Mode buttonMode)
    {
        addAndMakeVisible (button);
        button.setClickingTogglesState (false);
        button.onClick = [this, buttonMode] { setMode (buttonMode); };
    };
    setupModeButton (allModeButton, Mode::all);
    setupModeButton (bankModeButton, Mode::bank);
    setupModeButton (setlistModeButton, Mode::setlist);

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search presets...", juce::Colours::grey);
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

    auto setupSizeButton = [this] (juce::TextButton& button, int size)
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
    emptyStateLabel.setColour (juce::Label::textColourId, juce::Colours::grey);

    addAndMakeVisible (hintLabel);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setFont (13.0f);

    addAndMakeVisible (nameField);
    nameField.setTextToShowWhenEmpty ("Preset name...", juce::Colours::grey);
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
    setSize (980, 640);
}

juce::String PresetBrowserDialog::currentPresetName() const
{
    return getCurrentPresetName != nullptr ? getCurrentPresetName() : juce::String {};
}

void PresetBrowserDialog::setMode (Mode newMode)
{
    mode = newMode;
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
    auto area = getLocalBounds().reduced (14);

    auto titleRow = area.removeFromTop (touch::minTapTarget);
    titleLabel.setBounds (titleRow.removeFromLeft (140));
    setlistModeButton.setBounds (titleRow.removeFromRight (116));
    titleRow.removeFromRight (6);
    bankModeButton.setBounds (titleRow.removeFromRight (104));
    titleRow.removeFromRight (6);
    allModeButton.setBounds (titleRow.removeFromRight (88));

    area.removeFromTop (6);

    auto selectorRow = area.removeFromTop (touch::minTapTarget);
    if (mode == Mode::all)
    {
        tagBox.setBounds (selectorRow.removeFromRight (190));
        selectorRow.removeFromRight (8);
        searchField.setBounds (selectorRow);
    }
    else if (mode == Mode::bank)
    {
        bankSize8Button.setBounds (selectorRow.removeFromRight (touch::minTapTarget));
        selectorRow.removeFromRight (6);
        bankSize4Button.setBounds (selectorRow.removeFromRight (touch::minTapTarget));

        bankPrevButton.setBounds (selectorRow.removeFromLeft (touch::minTapTarget));
        bankLabel.setBounds (selectorRow.removeFromLeft (160));
        bankNextButton.setBounds (selectorRow.removeFromLeft (touch::minTapTarget));
    }
    else
    {
        stepNextButton.setBounds (selectorRow.removeFromRight (120));
        selectorRow.removeFromRight (6);
        stepPrevButton.setBounds (selectorRow.removeFromRight (120));

        setlistMenuButton.setBounds (selectorRow.removeFromRight (touch::minTapTarget));

        setlistPrevButton.setBounds (selectorRow.removeFromLeft (touch::minTapTarget));
        setlistNextButton.setBounds (selectorRow.removeFromLeft (touch::minTapTarget));
        selectorRow.removeFromLeft (4);
        setlistLabel.setBounds (selectorRow);
    }

    area.removeFromTop (4);
    hintLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (4);

    auto bottomRow = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (bottomRow.removeFromRight (90));
    bottomRow.removeFromRight (8);
    saveButton.setBounds (bottomRow.removeFromRight (120));
    bottomRow.removeFromRight (8);
    nameField.setBounds (bottomRow);

    area.removeFromBottom (8);

    if (mode == Mode::bank)
    {
        // The bank is the one place the grid is NOT free-flowing: exactly
        // bankSize tiles in a single row, one per foot-switch position.
        gridViewport.setVisible (false);
        const int n = juce::jmax (1, bankTiles.size());
        const int tileW = (area.getWidth() - tileGap * (n - 1)) / n;
        const int tileH = juce::jmin (180, area.getHeight() / 2);
        for (int i = 0; i < n; ++i)
            bankTiles[i]->setBounds (area.getX() + i * (tileW + tileGap), area.getY(), tileW, tileH);
    }
    else
    {
        for (auto* tile : bankTiles)
            tile->setVisible (false);

        gridViewport.setVisible (true);
        gridViewport.setBounds (area);
        emptyStateLabel.setBounds (area);

        const int contentWidth = juce::jmax (1, area.getWidth() - gridViewport.getScrollBarThickness());
        tileGrid.setSize (contentWidth, tileGrid.layoutForWidth (contentWidth));
    }

    emptyStateLabel.setVisible (emptyStateLabel.getText().isNotEmpty());
}

void PresetBrowserDialog::paint (juce::Graphics& g)
{
    g.fillAll (cardBackground);
}

} // namespace openguitarmultifx
