#include "AddEffectBrowserDialog.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    const juce::Colour rowBackground { 0xff242424 };
    constexpr int rowGap = 6;
    constexpr int railWidth = 210;
}

//==============================================================================
// EffectRow
//==============================================================================
void AddEffectBrowserDialog::EffectRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = probe != nullptr ? probe->getAccentColour() : juce::Colours::grey;

    g.setColour (rowBackground);
    g.fillRoundedRectangle (bounds, 8.0f);

    // Thin accent strip on the left edge + the effect's own glyph as a
    // small ornament -- the name carries the row now, not the tile icon.
    g.setColour (accent);
    g.fillRect (bounds.getX(), bounds.getY() + 4.0f, 3.0f, bounds.getHeight() - 8.0f);

    auto area = bounds.reduced (14.0f, 6.0f);
    auto iconArea = area.removeFromLeft (area.getHeight());
    area.removeFromLeft (6.0f);
    if (probe != nullptr)
    {
        const float glyph = iconArea.getWidth() * 0.92f;
        probe->drawIcon (g, iconArea.withSizeKeepingCentre (glyph, glyph));
    }

    // The context line (category while searching, else the subcategory)
    // gets a fixed right-hand column so it never crowds the name.
    const float sublineWidth = subline.isNotEmpty() ? 150.0f : 0.0f;
    auto nameArea = area;
    auto subArea = nameArea.removeFromRight (sublineWidth);

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1);

    if (subline.isNotEmpty())
    {
        g.setColour (juce::Colours::grey);
        g.setFont (12.5f);
        g.drawFittedText (subline, subArea.toNearestInt(), juce::Justification::centredRight, 1);
    }
}

void AddEffectBrowserDialog::EffectRow::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasDraggedSinceMouseDown() && onTap)
        onTap();
}

//==============================================================================
// RowList
//==============================================================================
void AddEffectBrowserDialog::RowList::clearRows()
{
    rows.clear (true);
}

AddEffectBrowserDialog::EffectRow& AddEffectBrowserDialog::RowList::addRow()
{
    auto* row = rows.add (new EffectRow());
    addAndMakeVisible (*row);
    row->onTap = [this, row]
    {
        if (onRowTap)
            onRowTap (*row);
    };
    return *row;
}

int AddEffectBrowserDialog::RowList::layoutForWidth (int width)
{
    const int rowH = touch::minTapTarget;
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * (rowH + rowGap), width, rowH);

    return juce::jmax (rows.size(), 1) * (rowH + rowGap);
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
    searchField.onTextChange = [this] { rebuildRows(); };

    // One rail button per category, in the registry's display order, each
    // labelled with its effect count. Radio-grouped so exactly one is lit.
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

    addChildComponent (chipsViewport);
    // No scrollbar: on the touch target the chip row scrolls by drag, and a
    // horizontal bar would steal height from the 48px chips when scrolling
    // ever becomes necessary. The clipped chip edge is the overflow cue.
    chipsViewport.setScrollBarsShown (false, false);
    chipsViewport.setViewedComponent (&chipsBar, false);

    rowList.onRowTap = [this] (EffectRow& row)
    {
        if (row.key.isNotEmpty() && onEffectChosen)
            onEffectChosen (row.key);
    };

    addAndMakeVisible (gridViewport);
    gridViewport.setScrollBarsShown (true, false);
    gridViewport.setViewedComponent (&rowList, false);

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    emptyStateLabel.setFont (16.0f);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    rebuildChips();
    rebuildRows();
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
    selectedSubcategory = {};
    rebuildChips();
    rebuildRows();
}

void AddEffectBrowserDialog::rebuildChips()
{
    chipButtons.clear (true);

    const auto subcategories = registry.subcategoriesForCategory (selectedCategory);
    if (subcategories.size() <= 1)
    {
        chipsViewport.setVisible (false);
        return;
    }

    chipsViewport.setVisible (true);

    // "All" plus one chip per subcategory, radio-grouped like the rail.
    juce::StringArray labels;
    labels.add ("All");
    for (const auto& s : subcategories)
        labels.add (s);

    constexpr int chipRadioId = 2;
    for (const auto& label : labels)
    {
        auto* chip = chipButtons.add (new juce::TextButton (label));
        chipsBar.addAndMakeVisible (*chip);
        chip->setRadioGroupId (chipRadioId, juce::dontSendNotification);
        chip->setClickingTogglesState (true);
        chip->setToggleState ((label == "All" && selectedSubcategory.isEmpty())
                              || label == selectedSubcategory,
                              juce::dontSendNotification);
        chip->onClick = [this, label]
        {
            selectedSubcategory = label == "All" ? juce::String() : label;
            rebuildRows();
        };
    }
}

void AddEffectBrowserDialog::rebuildRows()
{
    rowList.clearRows();

    const auto query = searchField.getText().trim();
    const bool searching = query.isNotEmpty();

    // Chips are a within-category filter -- meaningless across a global
    // search, so they hide until the field is cleared again.
    chipsViewport.setVisible (! searching && registry.subcategoriesForCategory (selectedCategory).size() > 1);

    // Sorted by display name (what the rows actually show), not by key.
    juce::StringArray keys = registry.getRegisteredNames();
    std::sort (keys.strings.begin(), keys.strings.end(),
               [this] (const juce::String& a, const juce::String& b)
               { return registry.displayNameForKey (a).compareNatural (registry.displayNameForKey (b)) < 0; });

    for (auto& key : keys)
    {
        const auto category = registry.categoryForKey (key);
        const auto subcategory = registry.subcategoryForKey (key);
        const auto name = registry.displayNameForKey (key);

        // The query matches the row name OR its category's name: a user
        // typing "reverb" expects Hall/Plate/Spring too -- their display
        // names never contain the word "reverb" themselves.
        const bool matches = ! searching
            || name.containsIgnoreCase (query)
            || EffectRegistry::categoryDisplayName (category).containsIgnoreCase (query)
            || subcategory.containsIgnoreCase (query);
        if (! matches || (! searching && category != selectedCategory))
            continue;
        if (! searching && selectedSubcategory.isNotEmpty() && subcategory != selectedSubcategory)
            continue;

        auto& row = rowList.addRow();
        row.key = key;
        row.name = name;
        row.subline = searching
            ? EffectRegistry::categoryDisplayName (category)
                + (subcategory.isNotEmpty() ? " · " + subcategory : juce::String())
            : subcategory;
        row.probe = probeForKey (key);
    }

    emptyStateLabel.setText (rowList.rows.isEmpty()
                                 ? (searching ? "No effects match \"" + query + "\"."
                                              : (selectedSubcategory.isNotEmpty()
                                                     ? "Nothing in this subcategory yet."
                                                     : "Nothing in this category yet."))
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
    searchField.setBounds (header.removeFromRight (320).withSizeKeepingCentre (320, touch::minTapTarget));

    area.removeFromTop (10);
    auto footer = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (footer.removeFromRight (120).withSizeKeepingCentre (120, touch::minTapTarget));
    area.removeFromBottom (8);

    railViewport.setBounds (area.removeFromLeft (railWidth));
    area.removeFromLeft (10);

    if (chipsViewport.isVisible())
    {
        chipsViewport.setBounds (area.removeFromTop (touch::minTapTarget));
        area.removeFromTop (8);

        // Chips size to their labels (48px tall, like every tap target).
        // The bar's width is the full chip run -- the viewport scrolls
        // sideways when that run outgrows the card's width.
        const juce::Font chipFont (15.0f);
        int x = 0;
        for (auto* chip : chipButtons)
        {
            const int w = juce::jmax (64, chipFont.getStringWidth (chip->getButtonText()) + 32);
            chip->setBounds (x, 0, w, touch::minTapTarget);
            x += w + 8;
        }
        chipsBar.setSize (x, chipsViewport.getMaximumVisibleHeight());
    }

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
    rowList.setSize (gridContentWidth, rowList.layoutForWidth (gridContentWidth));
}

void AddEffectBrowserDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

} // namespace openguitarmultifx
