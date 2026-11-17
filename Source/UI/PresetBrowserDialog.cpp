#include "PresetBrowserDialog.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"

#include "TouchSizing.h"
#include "SnapshotPresetXml.h"

namespace openguitarmultifx
{

namespace
{
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
    const auto ink = boldCurrent ? juce::Colours::black : t.text;
    const auto inkMuted = boldCurrent ? juce::Colours::black.withAlpha (0.62f) : t.textMuted;

    g.setColour (boldCurrent ? accent : (isEmpty ? t.surface : t.raised));
    g.fillRoundedRectangle (bounds, t.radius);

    // .pt.cur -- accent border + solid 2px ring outside it (mockup box-shadow spread)
    if (isCurrent && ! boldCurrent && ! isEmpty)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (bounds.expanded (2.0f).reduced (1.0f), t.radius + 2.0f, 2.0f);
    }
    g.setColour (isCurrent && ! isEmpty ? accent : (isEmpty ? t.lineStrong : t.line));
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, isCurrent && ! isEmpty ? 2.0f : 1.0f);

    auto area = bounds.reduced (14.0f, 12.0f);
    const auto headingArea = area.removeFromTop (18.0f);

    if (heading.isNotEmpty())
    {
        g.setColour (inkMuted);
        g.setFont (overlaychrome::font (12.0f, true));
        g.drawFittedText (heading, headingArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // .tg -- first tag as a pill top-right
    if (tag.isNotEmpty() && ! bankLayout)
    {
        const auto tagFont = overlaychrome::font (10.0f);
        const auto tagW = juce::GlyphArrangement::getStringWidth (tagFont, tag) + 14.0f;
        auto headingCopy = headingArea;
        const auto tagArea = headingCopy.removeFromRight (tagW).withSizeKeepingCentre (tagW, 16.0f);
        g.setColour (boldCurrent ? juce::Colours::black.withAlpha (0.25f) : t.raisedHigh);
        g.fillRoundedRectangle (tagArea, 6.0f);
        g.setColour (inkMuted);
        g.setFont (tagFont);
        g.drawFittedText (tag, tagArea.toNearestInt(), juce::Justification::centred, 1);
    }

    if (isEmpty)
    {
        g.setColour (t.textDim);
        g.setFont (overlaychrome::font (bankLayout ? 22.0f : 28.0f, bankLayout));
        g.drawText (bankLayout ? "+ Assign" : "+", bounds.toNearestInt(), juce::Justification::centred);
        return;
    }

    if (bankLayout)
    {
        // .bk -- name 22px, footswitch circle bottom-centre
        g.setColour (ink);
        g.setFont (overlaychrome::font (22.0f, true));
        g.drawFittedText (name, area.toNearestInt().withTrimmedBottom (52), juce::Justification::centredLeft, 1);

        const auto circle = juce::Rectangle<float> (40.0f, 40.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() - 32.0f });
        g.setColour (t.lineStrong);
        g.drawEllipse (circle, 3.0f);
        return;
    }

    // .pt -- name under the heading, scene dots pinned to the bottom
    area.removeFromTop (6.0f);
    g.setColour (ink);
    g.setFont (overlaychrome::font (17.0f, true));
    g.drawFittedText (name, area.toNearestInt().withTrimmedBottom (18.0f), juce::Justification::centredLeft, 2);

    if (subline.isNotEmpty())
    {
        g.setColour (inkMuted);
        g.setFont (overlaychrome::font (11.0f));
        g.drawFittedText (subline, area.toNearestInt().removeFromBottom (14.0f), juce::Justification::centredLeft, 1);
    }

    if (! dots.isEmpty())
    {
        const auto dotRow = juce::Rectangle<float> (bounds.getX() + 14.0f, bounds.getBottom() - 18.0f,
                                                    bounds.getWidth() - 28.0f, 5.0f);
        for (int i = 0; i < dots.size() && i < 8; ++i)
        {
            g.setColour (dots[i]);
            g.fillRoundedRectangle (dotRow.getX() + i * 18.0f, dotRow.getY(), 14.0f, 5.0f, 3.0f);
        }
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

    if (! wasLongPress && touch::wasListTap (event) && onTap)
        onTap();
}

void PresetBrowserDialog::PresetTile::timerCallback()
{
    stopTimer();
    if (onMenu)
        onMenu();
}

//==============================================================================
// TabStrip / SetlistField / SetlistStage
//==============================================================================
void PresetBrowserDialog::TabStrip::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.lineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.smallRadius, 1.0f);
}

void PresetBrowserDialog::SetlistField::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (t.raised);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.lineStrong);
    g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);
    auto area = bounds.reduced (14.0f, 0.0f).toNearestInt();
    g.setFont (overlaychrome::font (14.0f));
    g.setColour (t.textMuted);
    g.drawFittedText ("Setlist:", area, juce::Justification::centredLeft, 1);
    area.removeFromLeft (juce::GlyphArrangement::getStringWidth (overlaychrome::font (14.0f), "Setlist:") + 8);
    g.setFont (overlaychrome::font (14.0f, true));
    g.setColour (t.text);
    g.drawFittedText (setlistName, area, juce::Justification::centredLeft, 1);
}

void PresetBrowserDialog::SetlistStage::mouseDown (const juce::MouseEvent&)
{
    startTimer (longPressMs);
}

void PresetBrowserDialog::SetlistStage::mouseUp (const juce::MouseEvent& event)
{
    const bool wasLongPress = ! isTimerRunning();
    stopTimer();

    if (event.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }

    if (! wasLongPress && touch::wasTap (event) && onTap)
        onTap();
}

void PresetBrowserDialog::SetlistStage::timerCallback()
{
    stopTimer();
    if (onMenu)
        onMenu();
}

void PresetBrowserDialog::SetlistStage::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    const auto bounds = getLocalBounds().toFloat();
    const bool boldCurrent = isCurrent && t.id == theme::Id::boldBlocks;
    const auto ink = boldCurrent ? juce::Colours::black : t.text;
    const auto inkMuted = boldCurrent ? juce::Colours::black.withAlpha (0.62f) : t.textMuted;

    g.setColour (boldCurrent ? accent : t.raised);
    g.fillRoundedRectangle (bounds, t.radius);
    if (isCurrent && ! boldCurrent)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (bounds.expanded (2.0f).reduced (1.0f), t.radius + 2.0f, 2.0f);
    }
    g.setColour (isCurrent ? accent : t.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, isCurrent ? 2.0f : 1.0f);

    // centred group: position above, giant name, "Next:" below
    const auto groupH = 22.0f + 68.0f + 26.0f;
    auto group = bounds.reduced (0.0f, (bounds.getHeight() - groupH) * 0.5f);
    g.setColour (inkMuted);
    g.setFont (overlaychrome::font (12.0f, true));
    g.drawFittedText (positionText, group.removeFromTop (22.0f).toNearestInt(), juce::Justification::centred, 1);

    g.setColour (ink);
    g.setFont (overlaychrome::font (56.0f, true));
    g.drawFittedText (presetName, group.removeFromTop (68.0f).toNearestInt(), juce::Justification::centred, 1);

    g.setColour (inkMuted);
    g.setFont (overlaychrome::font (14.0f));
    g.drawFittedText (nextText, group.removeFromTop (26.0f).toNearestInt(), juce::Justification::centred, 1);
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
    tile->setBufferedToImage (true); // painted once, scrolled as a texture
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

    addAndMakeVisible (tabStrip);
    auto setupModeButton = [this] (overlaychrome::ActionButton& button, Mode buttonMode)
    {
        tabStrip.addAndMakeVisible (button);
        button.selectedStyle = overlaychrome::SelectedStyle::tab;
        button.mutedIdle = true;
        button.flatTab = true;
        button.selected = mode == buttonMode;
        button.onClick = [this, buttonMode] { setMode (buttonMode); };
    };
    // "+ New" leads the strip like the mockup's `.tabs` row
    newPresetButton.selectedStyle = overlaychrome::SelectedStyle::none;
    newPresetButton.mutedIdle = true;
    newPresetButton.flatTab = true;
    newPresetButton.kind = overlaychrome::ButtonKind::normal;
    tabStrip.addAndMakeVisible (newPresetButton);
    newPresetButton.onClick = [this] { if (onNewRequested) onNewRequested(); };
    setupModeButton (allModeButton, Mode::all);
    setupModeButton (bankModeButton, Mode::bank);
    setupModeButton (setlistModeButton, Mode::setlist);

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search presets...", theme::current().textMuted);
    overlaychrome::drawSearchFieldChrome (searchField);
    searchField.onTextChange = [this] { rebuildTiles(); };

    chipsViewport.setScrollBarsShown (false, false);
    chipsViewport.setViewedComponent (&chipsContent, false);
    touch::enableDragScroll (chipsViewport);
    addAndMakeVisible (chipsViewport);

    // The stage card behaves like the removed member tiles: tap loads the
    // shown member, long-press/right-click opens its reorder/remove menu.
    setlistStage.onTap = [this]
    {
        if (stageMemberName.isNotEmpty() && onPresetChosen)
            onPresetChosen (stageMemberName);
    };
    setlistStage.onMenu = [this]
    {
        for (auto& e : library.entries())
            if (e.name == stageMemberName)
            {
                showTileMenu (e);
                return;
            }
    };
    addAndMakeVisible (setlistField);
    addAndMakeVisible (setlistStage);

    addAndMakeVisible (bankPrevButton);
    bankPrevButton.onClick = [this] { currentBank = juce::jmax (0, currentBank - 1); rebuildTiles(); };
    addAndMakeVisible (bankNextButton);
    bankNextButton.onClick = [this] { ++currentBank; rebuildTiles(); };
    addAndMakeVisible (bankLabel);
    bankLabel.setJustificationType (juce::Justification::centred);
    bankLabel.setFont (overlaychrome::font (22.0f, true));

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
    setlistField.setlistName = {};

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
    touch::enableDragScroll (gridViewport);

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

void PresetBrowserDialog::previewScreen (const juce::String& which)
{
    if (which == "presets-bank")
    {
        currentBank = 2; // the seeded/browsed bank in docs comparisons
        setMode (Mode::bank);
    }
    else if (which == "presets-setlist")
        setMode (Mode::setlist);
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
    tagChips.clear (true);
    auto addChip = [this] (const juce::String& tag)
    {
        auto* chip = tagChips.add (new overlaychrome::ActionButton (tag.isEmpty() ? "All tags" : tag));
        chip->selectedStyle = overlaychrome::SelectedStyle::chip;
        chip->listTap = true; // chips live in a drag-scroll viewport
        chip->selected = (tag == selectedTag);
        // Flip selection in place -- rebuilding this array inside a chip's own
        // onClick would delete the button mid-callback.
        chip->onClick = [this, tag]
        {
            selectedTag = tag;
            for (auto* c : tagChips)
            {
                c->selected = (c->text == (tag.isEmpty() ? "All tags" : tag));
                c->repaint();
            }
            rebuildTiles();
        };
        chipsContent.addAndMakeVisible (*chip);
    };
    addChip ({});
    for (auto& tag : library.allTags())
        addChip (tag);
    layoutTagChips();
}

void PresetBrowserDialog::layoutTagChips()
{
    if (chipsViewport.getWidth() <= 0)
        return;
    const auto font = overlaychrome::font (13.0f);
    int x = 0;
    for (auto* chip : tagChips)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (font, chip->text) + 32;
        chip->setBounds (x, 0, w, 40);
        x += w + 8;
    }
    chipsContent.setSize (juce::jmax (x, chipsViewport.getWidth()), 40);
}

void PresetBrowserDialog::configureTile (PresetTile& tile, const PresetLibrary::Entry& entry, const juce::String& heading)
{
    tile.name = entry.name;
    tile.heading = heading;
    tile.tag = entry.tags.isEmpty() ? juce::String {} : entry.tags[0];
    tile.subline = {};
    tile.isCurrent = (entry.name == currentPresetName());
    tile.isEmpty = false;

    // .dots -- one accent bar per saved scene, defaulting like the top bar
    tile.dots.clear();
    for (int i = 0; i < entry.snapshotColours.size(); ++i)
    {
        const auto hex = entry.snapshotColours[i];
        tile.dots.add (hex.isNotEmpty() ? juce::Colour::fromString ("ff" + hex)
                                        : snapshotxml::defaultColourFor (i));
    }
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
    chipsViewport.setVisible (allVisible);
    bankPrevButton.setVisible (bankVisible);
    bankNextButton.setVisible (bankVisible);
    bankLabel.setVisible (bankVisible);
    bankSize4Button.setVisible (bankVisible);
    bankSize8Button.setVisible (bankVisible);
    setlistPrevButton.setVisible (setlistVisible);
    setlistNextButton.setVisible (setlistVisible);
    setlistField.setVisible (setlistVisible);
    setlistMenuButton.setVisible (setlistVisible);
    stepPrevButton.setVisible (setlistVisible);
    stepNextButton.setVisible (setlistVisible);
    setlistStage.setVisible (setlistVisible);

    bankTiles.clear (true);
    tileGrid.clearTiles();

    const auto allEntries = library.entries(); // one directory scan per refresh

    if (allVisible)
    {
        const auto names = library.search (searchField.getText(), selectedTag);
        for (auto& e : allEntries)
            if (names.contains (e.name))
                configureTile (tileGrid.addTile(), e,
                               juce::String (e.number).paddedLeft ('0', 2)
                                   + snapshotxml::letterFor (e.snapshotCurrent));

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

        tile->bankLayout = true;
        if (occupant == nullptr)
        {
            tile->isEmpty = true;
            tile->name = {};
            tile->heading = "SLOT " + juce::String (slot + 1);
            tile->onTap = [this, stored] { showSlotPicker (stored.bank, stored.slot); };
        }
        else
        {
            configureTile (*tile, *occupant, "SLOT " + juce::String (slot + 1));
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
        setlistField.setlistName = "--";
        setlistField.repaint();
        emptyStateLabel.setText ("No setlists yet -- add a preset to one from its tile menu (long-press).",
                                 juce::dontSendNotification);
        return;
    }

    if (! names.contains (currentSetlist))
        currentSetlist = names[0];

    setlistField.setlistName = currentSetlist;
    setlistField.repaint();

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

    // .stg -- stage card: "n of N" + giant name + "Next: X" (prototype layout)
    const auto current = currentPresetName();
    int liveIndex = -1;
    for (int i = 0; i < (int) members.size(); ++i)
        if (members[(size_t) i]->name == current)
        {
            liveIndex = i;
            break;
        }
    if (members.empty())
    {
        setlistStage.positionText = {};
        setlistStage.presetName = "--";
        setlistStage.nextText = {};
        setlistStage.isCurrent = false;
        stageMemberName = {};
    }
    else if (liveIndex < 0)
    {
        // The loaded preset isn't a member: don't dress member[0] up as
        // "current", and keep Next consistent with stepSetlist (which loads
        // members[0] when current isn't in the list).
        setlistStage.positionText = "-- of " + juce::String ((int) members.size());
        setlistStage.presetName = current.isEmpty() ? "--" : current;
        setlistStage.nextText = "Next: " + members[0]->name;
        setlistStage.isCurrent = false;
        stageMemberName = members[0]->name;
    }
    else
    {
        auto* m = members[(size_t) liveIndex];
        auto* next = members[(size_t) ((liveIndex + 1) % (int) members.size())];
        setlistStage.positionText = juce::String (liveIndex + 1) + " of " + juce::String ((int) members.size());
        setlistStage.presetName = m->name;
        setlistStage.nextText = "Next: " + next->name;
        setlistStage.isCurrent = true;
        stageMemberName = m->name;
    }
    setlistStage.repaint();

    // tapping the big card loads the shown preset; long-press still opens the menu
    emptyStateLabel.setText (members.empty() ? "This setlist is empty." : juce::String {},
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
    // .card{padding:20px} -- everything insets 20px from the card edges
    auto area = getLocalBounds().reduced (20);
    auto header = area.removeFromTop (76);

    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    titleLabel.setBounds (header.removeFromLeft (150));

    // .tabs -- the four buttons live inside one bordered strip (4px padding, 4px gaps)
    auto stripArea = header.removeFromLeft (88 + 84 + 102 + 118 + 3 * 4 + 8);
    tabStrip.setBounds (stripArea.withSizeKeepingCentre (stripArea.getWidth(), 56));
    auto strip = tabStrip.getLocalBounds().reduced (4);
    for (auto* b : { &newPresetButton, &allModeButton, &bankModeButton, &setlistModeButton })
    {
        const int w = b == &newPresetButton ? 88 : b == &allModeButton ? 84 : b == &bankModeButton ? 102 : 118;
        b->setBounds (strip.removeFromLeft (w).withSizeKeepingCentre (w, 40));
        strip.removeFromLeft (4);
    }

    if (searchField.isVisible())
    {
        header.removeFromLeft (juce::jmax (0, header.getWidth() - 300));
        searchField.setBounds (header.removeFromRight (300).withSizeKeepingCentre (300, 52));
    }

    auto body = area;
    auto selectorRow = body.removeFromTop (56);

    if (mode == Mode::all)
    {
        chipsViewport.setBounds (selectorRow);
        layoutTagChips();
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
        // .field "Setlist: name" + ... menu pill at the right
        setlistMenuButton.setBounds (selectorRow.removeFromRight (48).withSizeKeepingCentre (48, 40));
        selectorRow.removeFromRight (8);
        // keep cycling setlists available but compact, inside the field row
        setlistPrevButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 40));
        selectorRow.removeFromLeft (4);
        setlistNextButton.setBounds (selectorRow.removeFromLeft (48).withSizeKeepingCentre (48, 40));
        selectorRow.removeFromLeft (8);
        setlistField.setBounds (selectorRow.withTrimmedTop (6).withTrimmedBottom (6));
    }

    body.removeFromTop (8);

    auto bottomRow = body.removeFromBottom (76);
    saveButton.setBounds (bottomRow.removeFromRight (150).withSizeKeepingCentre (150, 52));
    bottomRow.removeFromRight (10);
    hintLabel.setBounds (bottomRow.removeFromRight (juce::jmin (360, bottomRow.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (360, bottomRow.getWidth() / 3), 24));
    nameField.setBounds (bottomRow.withSizeKeepingCentre (bottomRow.getWidth(), 52));
    overlaychrome::drawSearchFieldChrome (nameField);
    body.removeFromBottom (10);

    if (mode == Mode::setlist)
    {
        gridViewport.setVisible (false);
        for (auto* tile : bankTiles)
            tile->setVisible (false);

        // .stg -- 180px buttons centred against the card, giant card in the middle
        stepPrevButton.setBounds (body.removeFromLeft (180).withSizeKeepingCentre (180, 48));
        stepNextButton.setBounds (body.removeFromRight (180).withSizeKeepingCentre (180, 48));
        setlistStage.setBounds (body.reduced (16, 0));
        emptyStateLabel.setBounds (body);
    }
    else if (mode == Mode::bank)
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
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 96.0f));
    // .foot{border-top}
    g.setColour (theme::current().line);
    g.drawLine (20.0f, (float) getHeight() - 96.0f, (float) getWidth() - 20.0f, (float) getHeight() - 96.0f, 1.0f);
}

} // namespace openguitarmultifx
