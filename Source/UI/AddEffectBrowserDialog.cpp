#include "AddEffectBrowserDialog.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    const juce::Colour tileBackground { 0xff242424 };
    constexpr int tileGap = 10;
    constexpr int railWidth = 210;
}

//==============================================================================
// EffectTile
//==============================================================================
void AddEffectBrowserDialog::EffectTile::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.5f);
    const auto accent = probe != nullptr ? probe->getAccentColour() : juce::Colours::grey;

    // Same look as EffectBlockComponent: dark fill, outline in the effect's
    // own category colour -- what you pick previews what lands on the grid.
    g.setColour (tileBackground);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (bounds, 8.0f, 2.0f);

    auto area = bounds.reduced (6.0f);
    auto nameArea = area.removeFromBottom (subline.isNotEmpty() ? 40.0f : 28.0f);
    area.removeFromBottom (2.0f);

    // drawIcon() implementations expect a square -- hand it a centred one,
    // same guard EffectBlockComponent uses.
    const float squareSize = juce::jmin (area.getWidth(), area.getHeight()) * 0.86f;
    if (probe != nullptr)
        probe->drawIcon (g, area.withSizeKeepingCentre (squareSize, squareSize));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText (name, nameArea.toNearestInt().removeFromTop (subline.isNotEmpty() ? 24 : 28),
                      juce::Justification::centred, 2);

    if (subline.isNotEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (12.0f);
        g.drawFittedText (subline, nameArea.toNearestInt(), juce::Justification::centred, 1);
    }
}

void AddEffectBrowserDialog::EffectTile::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasDraggedSinceMouseDown() && onTap)
        onTap();
}

//==============================================================================
// TileGrid
//==============================================================================
void AddEffectBrowserDialog::TileGrid::clearTiles()
{
    tiles.clear (true);
}

AddEffectBrowserDialog::EffectTile& AddEffectBrowserDialog::TileGrid::addTile()
{
    auto* tile = tiles.add (new EffectTile());
    addAndMakeVisible (*tile);
    tile->onTap = [this, tile]
    {
        if (onTileTap)
            onTileTap (*tile);
    };
    return *tile;
}

int AddEffectBrowserDialog::TileGrid::layoutForWidth (int width)
{
    // Taller than wide on purpose: the icon row needs room above a two-line
    // name, and finger-sized tiles beat dense ones (touch::minTapTarget is
    // the floor, not the target).
    constexpr int tileW = 148, tileH = 112;
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
// AddEffectBrowserDialog
//==============================================================================
AddEffectBrowserDialog::AddEffectBrowserDialog (EffectRegistry& registryToUse)
    : registry (registryToUse)
{
    for (auto& key : registry.getRegisteredNames())
        ++counts[registry.categoryForKey (key)];

    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Add Effect", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search effects...", juce::Colours::grey);
    searchField.onTextChange = [this] { rebuildTiles(); };

    // One rail button per category, in the registry's display order, each
    // labelled with its tile count. Radio-grouped so exactly one is lit.
    int radioId = 1;
    for (auto category : EffectRegistry::categoriesInDisplayOrder())
    {
        auto* button = categoryButtons.add (new juce::TextButton (
            EffectRegistry::categoryDisplayName (category) + "  (" + juce::String (counts[category]) + ")"));
        rail.addAndMakeVisible (*button);
        button->setRadioGroupId (radioId, juce::dontSendNotification);
        button->setClickingTogglesState (true);
        button->setToggleState (category == selectedCategory, juce::dontSendNotification);
        button->onClick = [this, category] { selectCategory (category); };
    }

    addAndMakeVisible (railViewport);
    railViewport.setScrollBarsShown (true, false);
    railViewport.setViewedComponent (&rail, false);

    tileGrid.onTileTap = [this] (EffectTile& tile)
    {
        if (tile.key.isNotEmpty() && onEffectChosen)
            onEffectChosen (tile.key);
    };

    addAndMakeVisible (gridViewport);
    gridViewport.setScrollBarsShown (true, false);
    gridViewport.setViewedComponent (&tileGrid, false);

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    emptyStateLabel.setFont (16.0f);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    rebuildTiles();
    setSize (1040, 700);
}

const EffectProcessor* AddEffectBrowserDialog::probeForKey (const juce::String& key)
{
    auto& probe = probes[key];
    if (probe == nullptr)
        probe = registry.create (key);
    return probe.get();
}

void AddEffectBrowserDialog::selectCategory (EffectRegistry::Category category)
{
    selectedCategory = category;
    rebuildTiles();
}

void AddEffectBrowserDialog::rebuildTiles()
{
    tileGrid.clearTiles();

    const auto query = searchField.getText().trim();
    const bool searching = query.isNotEmpty();

    // Sorted by display name (what the tiles actually show), not by key.
    juce::StringArray keys = registry.getRegisteredNames();
    std::sort (keys.strings.begin(), keys.strings.end(),
               [this] (const juce::String& a, const juce::String& b)
               { return registry.displayNameForKey (a).compareNatural (registry.displayNameForKey (b)) < 0; });

    for (auto& key : keys)
    {
        const auto category = registry.categoryForKey (key);
        const auto name = registry.displayNameForKey (key);

        // The query matches the tile name OR its category's name: a user
        // typing "reverb" expects Hall/Plate/Spring too -- their display
        // names never contain the word "reverb" themselves.
        const bool matches = ! searching
            || name.containsIgnoreCase (query)
            || EffectRegistry::categoryDisplayName (category).containsIgnoreCase (query);
        if (! matches || (! searching && category != selectedCategory))
            continue;

        auto& tile = tileGrid.addTile();
        tile.key = key;
        tile.name = name;
        tile.subline = searching ? EffectRegistry::categoryDisplayName (category) : juce::String {};
        tile.probe = probeForKey (key);
    }

    emptyStateLabel.setText (tileGrid.tiles.isEmpty()
                                 ? (searching ? "No effects match \"" + query + "\"."
                                              : "Nothing in this category yet.")
                                 : juce::String {},
                             juce::dontSendNotification);
    emptyStateLabel.setVisible (emptyStateLabel.getText().isNotEmpty());

    resized();

    // A new result set always starts at the top -- keeping the old offset
    // would show the first matches above the viewport whenever the
    // replacement list is also long enough to scroll.
    gridViewport.setViewPosition (0, 0);
}

void AddEffectBrowserDialog::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (touch::minTapTarget);
    titleLabel.setBounds (header.removeFromLeft (240));
    searchField.setBounds (header.removeFromRight (320).withSizeKeepingCentre (320, 40));

    area.removeFromTop (10);
    auto footer = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (footer.removeFromRight (120).withSizeKeepingCentre (120, 44));
    area.removeFromBottom (8);

    railViewport.setBounds (area.removeFromLeft (railWidth));
    area.removeFromLeft (10);
    gridViewport.setBounds (area);
    emptyStateLabel.setBounds (area);

    // Rail buttons: full-width rows at the tap-target floor, in a viewport
    // in case 13 categories ever outgrow the card.
    const int railContentWidth = railViewport.getMaximumVisibleWidth();
    const int buttonStep = touch::minTapTarget + 6;
    rail.setSize (railContentWidth, categoryButtons.size() * buttonStep);
    for (int i = 0; i < categoryButtons.size(); ++i)
        categoryButtons[i]->setBounds (0, i * buttonStep, railContentWidth, touch::minTapTarget);

    const int gridContentWidth = gridViewport.getMaximumVisibleWidth();
    tileGrid.setSize (gridContentWidth, tileGrid.layoutForWidth (gridContentWidth));
}

void AddEffectBrowserDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

} // namespace openguitarmultifx
