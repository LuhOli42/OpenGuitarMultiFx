#include "AddEffectBrowserDialog.h"

#include "TouchSizing.h"
#include "../Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    const juce::Colour rowBackground { 0xff242424 };
    constexpr int rowGap = 6;
    constexpr int railWidth = 210;

    /** True for the modelled amps that carry a page-2 Cab selector -- detected by
        the `*_cab` parameter they all register now, so a new amp gets its two
        browser rows ('name' cab-off + 'name + Cab') for free. */
    bool cabCapable (const EffectProcessor* p)
    {
        // getParameters() is non-const; this is a read-only scan of the probe.
        auto* params = p != nullptr ? const_cast<EffectProcessor*> (p)->getParameters() : nullptr;
        if (params == nullptr)
            return false;
        for (auto* param : params->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (param);
                f != nullptr && juce::String (f->paramID).endsWith ("_cab"))
                return true;
        return false;
    }
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
    if (headOnlyIcon)
    {
        // 'Amp' (cab off) rows get the head-only glyph; '+ Cab' rows keep the
        // probe's own icon, which for the modelled amps is already head+cabinet.
        static const std::unique_ptr<juce::Drawable> headIcon =
            icon::loadSvg (IconData::amp_head_svg, IconData::amp_head_svgSize);
        const float glyph = iconArea.getWidth() * 0.92f;
        icon::drawSvg (g, iconArea.withSizeKeepingCentre (glyph, glyph), headIcon.get());
    }
    else if (probe != nullptr)
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

    // The `amps` category gets two rail entries -- 'Amps' lists every amp
    // head-only, 'Amps + Cab' the same amps with their page-2 cab on. All
    // other categories get one entry each, in the registry's display order.
    railTabs.push_back ({ "Amps", EffectRegistry::Category::amps, RailTab::Rows::headOnly, 0 });
    railTabs.push_back ({ "Amps + Cab", EffectRegistry::Category::amps, RailTab::Rows::cabOnly, 0 });
    for (auto category : EffectRegistry::categoriesInDisplayOrder())
        if (category != EffectRegistry::Category::amps)
            railTabs.push_back ({ EffectRegistry::categoryDisplayName (category), category, RailTab::Rows::both, 0 });

    for (auto& tab : railTabs)
    {
        if (tab.rows != RailTab::Rows::cabOnly)
        {
            tab.count = counts[tab.category];
            continue;
        }
        // Counting the cab-capable amps creates their probes a little
        // earlier than the lazy path would -- the same probes the default
        // 'Amps' tab builds on open anyway.
        for (auto& key : registry.getRegisteredNames())
            if (registry.categoryForKey (key) == tab.category && cabCapable (probeForKey (key)))
                ++tab.count;
    }

    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Add Effect", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search effects...", juce::Colours::grey);
    searchField.onTextChange = [this] { rebuildRows(); };

    // One rail button per tab, each labelled with its row count.
    // Radio-grouped so exactly one is lit.
    int radioId = 1;
    for (int i = 0; i < (int) railTabs.size(); ++i)
    {
        auto& tab = railTabs[(size_t) i];
        auto* button = categoryButtons.add (new juce::TextButton (
            tab.label + "  (" + juce::String (tab.count) + ")"));
        rail.addAndMakeVisible (*button);
        button->setRadioGroupId (radioId, juce::dontSendNotification);
        button->setClickingTogglesState (true);
        button->setToggleState (i == selectedTab, juce::dontSendNotification);
        button->onClick = [this, i] { selectTab (i); };
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
            onEffectChosen (row.key, row.cabOn);
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

void AddEffectBrowserDialog::selectTab (int tabIndex)
{
    selectedTab = tabIndex;
    selectedSubcategory = {};
    rebuildChips();
    rebuildRows();
}

void AddEffectBrowserDialog::rebuildChips()
{
    chipButtons.clear (true);

    const auto subcategories = registry.subcategoriesForCategory (railTabs[(size_t) selectedTab].category);
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
    const auto& tab = railTabs[(size_t) selectedTab];
    chipsViewport.setVisible (! searching && registry.subcategoriesForCategory (tab.category).size() > 1);

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
        // names never contain the word "reverb" themselves. Typing "cab"
        // also matches the 'Amps + Cab' rail label, surfacing every
        // with-cab amp row.
        const bool matches = ! searching
            || name.containsIgnoreCase (query)
            || EffectRegistry::categoryDisplayName (category).containsIgnoreCase (query)
            || (category == EffectRegistry::Category::amps
                && juce::String ("Amps + Cab").containsIgnoreCase (query))
            || subcategory.containsIgnoreCase (query);
        if (! matches || (! searching && category != tab.category))
            continue;
        if (! searching && selectedSubcategory.isNotEmpty() && subcategory != selectedSubcategory)
            continue;

        // cabCapable() keys off the `*_cab` parameter, so new amps get
        // their cab-variant rows for free. The two amp tabs show one
        // insertion variant each; other tabs list both rows together, and
        // a global search always shows both.
        auto* probe = probeForKey (key);
        const bool cab = cabCapable (probe);
        const bool plainRow = searching || tab.rows != RailTab::Rows::cabOnly;
        const bool cabRow   = cab && (searching || tab.rows != RailTab::Rows::headOnly);
        if (! plainRow && ! cabRow)
            continue; // a cab-less amp in the 'Amps + Cab' tab

        const auto searchSubline = [&] (bool forCabRow)
        {
            auto groupLabel = EffectRegistry::categoryDisplayName (category);
            if (forCabRow)
                groupLabel += " + Cab";
            return groupLabel + (subcategory.isNotEmpty() ? " · " + subcategory : juce::String());
        };

        if (plainRow)
        {
            auto& row = rowList.addRow();
            row.key = key;
            row.name = name;
            row.subline = searching ? searchSubline (false) : subcategory;
            row.probe = probe;
            row.headOnlyIcon = cab; // the cab-off row draws the head-only glyph
        }

        if (cabRow)
        {
            auto& row = rowList.addRow();
            row.key = key;
            // Inside the 'Amps + Cab' tab the group label already says it --
            // repeating '+ Cab' on every row name is noise there.
            row.name = searching || tab.rows == RailTab::Rows::both ? name + " + Cab" : name;
            row.subline = searching ? searchSubline (true) : subcategory;
            row.probe = probe;
            row.cabOn = true;
        }
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
