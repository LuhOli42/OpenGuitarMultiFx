#include "AddEffectBrowserDialog.h"
#include "EffectCpuCost.h"
#include "OverlayChrome.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"

#include "TouchSizing.h"
#include "../Effects/IconKit.h"

#include <IconData.h>

namespace openguitarmultifx
{

namespace
{
    constexpr int rowGap = 8;
    constexpr int railWidth = 220;

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
    const auto& t = theme::current();
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (t.raised);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.smallRadius, 1.0f);

    const auto accent = probe != nullptr ? probe->getAccentColour() : t.textMuted;
    if (t.id == theme::Id::console)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (bounds.withWidth (5.0f), 2.0f);
    }

    // Right-hand columns first: the "+" affordance, the CPU-cost bars, and
    // the search-only category tag. Everything left of them is icon + text.
    auto rightArea = bounds;
    const auto plusArea = rightArea.removeFromRight (58.0f).withSizeKeepingCentre (48.0f, 48.0f);
    const auto cpuArea = rightArea.removeFromRight (44.0f);
    const auto tagArea = rightArea.removeFromRight (tag.isNotEmpty() ? 112.0f : 0.0f);

    // Icon tile: the effect's own glyph on an accent-tinted square.
    auto area = rightArea.reduced (10.0f, 8.0f);
    auto iconArea = area.removeFromLeft (44.0f).withSizeKeepingCentre (44.0f, 44.0f);
    iconArea = juce::Rectangle<float> (iconArea.getX(), area.getCentreY() - 22.0f, 44.0f, 44.0f);
    area.removeFromLeft (12.0f);

    if (t.id == theme::Id::boldBlocks)
        g.setColour (accent);
    else
        g.setColour (accent.withAlpha (0.16f));
    g.fillRoundedRectangle (iconArea, t.smallRadius);

    if (headOnlyIcon)
    {
        // 'Amp' (cab off) rows get the head-only glyph; '+ Cab' rows keep the
        // probe's own icon, which for the modelled amps is already head+cabinet.
        static const std::unique_ptr<juce::Drawable> headIcon =
            icon::loadSvg (IconData::amp_head_svg, IconData::amp_head_svgSize);
        const float glyph = iconArea.getWidth() * 0.86f;
        icon::drawSvg (g, iconArea.withSizeKeepingCentre (glyph, glyph), headIcon.get());
    }
    else if (probe != nullptr)
    {
        const float glyph = iconArea.getWidth() * 0.86f;
        probe->drawIcon (g, iconArea.withSizeKeepingCentre (glyph, glyph));
    }

    // Name over its short description line.
    const auto nameArea = area.removeFromTop (area.getHeight() * 0.56f);
    const auto descArea = area;

    g.setColour (t.text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centredLeft, 1);

    if (subline.isNotEmpty())
    {
        g.setColour (t.textMuted);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (subline, descArea.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    // Category tag (search results only): which rail the row came from.
    if (tag.isNotEmpty())
    {
        const auto tagFont = juce::Font (juce::FontOptions (11.0f));
        const int tagTextWidth = tagFont.getStringWidth (tag);
        const auto chip = tagArea.withSizeKeepingCentre (
            juce::jmin (tagArea.getWidth(), (float) tagTextWidth + 18.0f), 24.0f);
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (chip, t.smallRadius);
        g.setColour (t.textMuted);
        g.setFont (tagFont);
        g.drawFittedText (tag, chip.toNearestInt(), juce::Justification::centred, 1);
    }

    // CPU-cost bars: four bars, filled up to cpuLevel (docs/CpuCostMap.md).
    {
        constexpr float barWidth = 5.0f, barGap = 2.0f, barHeight = 14.0f;
        const float barsWidth = 4.0f * barWidth + 3.0f * barGap;
        const auto barsArea = cpuArea.withSizeKeepingCentre (barsWidth, barHeight);
        for (int i = 0; i < 4; ++i)
        {
            const auto bar = juce::Rectangle<float> (barsArea.getX() + i * (barWidth + barGap),
                                                     barsArea.getY(), barWidth, barHeight);
            g.setColour (i < cpuLevel ? t.textMuted : t.raisedHigh);
            g.fillRoundedRectangle (bar, 2.0f);
        }
    }

    // "+" affordance -- the whole row is the tap target, this is the cue.
    g.setColour (t.raisedHigh);
    g.fillRoundedRectangle (plusArea, t.smallRadius);
    g.setColour (t.text);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("+", plusArea.toNearestInt(), juce::Justification::centred);
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
    // Card 4's list row: 64px tall (icon tile + name/description + tag +
    // CPU bars + "+"), well above the 48px touch floor.
    const int rowH = 64;
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
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("Search by name, type or original pedal...",
                                        theme::current().textMuted);
    overlaychrome::drawSearchFieldChrome (searchField);
    searchField.onTextChange = [this] { rebuildRows(); };

    for (int i = 0; i < (int) railTabs.size(); ++i)
    {
        auto& tab = railTabs[(size_t) i];
        auto* button = categoryButtons.add (new overlaychrome::ActionButton());
        rail.addAndMakeVisible (*button);
        button->text = tab.label;
        button->showDot = true;
        button->dotColour = EffectRegistry::categoryColour (tab.category);
        button->trailingText = juce::String (tab.count);
        button->selectedStyle = overlaychrome::SelectedStyle::tab;
        button->leftAccent = true;
        button->mutedIdle = true;
        button->selected = i == selectedTab;
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
        if (row.key.isEmpty())
            return;
        // Picked from the open tab: remember it with its chip. Picked from a search: remember the effect's own tab.
        const int tab = tabForRow (row);
        lastTab = tab;
        lastSubcategory = (searchField.isEmpty() && tab == selectedTab) ? selectedSubcategory : juce::String();
        if (onEffectChosen)
            onEffectChosen (row.key, row.cabOn);
    };

    addAndMakeVisible (gridViewport);
    gridViewport.setScrollBarsShown (true, false);
    gridViewport.setViewedComponent (&rowList, false);

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
    emptyStateLabel.setFont (16.0f);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    if (lastTab >= 0 && lastTab < (int) railTabs.size())
    {
        selectedTab = lastTab;
        if (registry.subcategoriesForCategory (railTabs[(size_t) selectedTab].category).contains (lastSubcategory))
            selectedSubcategory = lastSubcategory;
        for (int i = 0; i < categoryButtons.size(); ++i)
            categoryButtons[i]->selected = i == selectedTab;
        categoryButtons[selectedTab]->repaint();
    }

    rebuildChips();
    rebuildRows();
    setSize (1180, 780);
}

int AddEffectBrowserDialog::tabForRow (const EffectRow& row) const
{
    const auto category = registry.categoryForKey (row.key);
    for (int i = 0; i < (int) railTabs.size(); ++i)
    {
        const auto& tab = railTabs[(size_t) i];
        if (tab.category != category)
            continue;
        if (category == EffectRegistry::Category::amps && (tab.rows == RailTab::Rows::cabOnly) != row.cabOn)
            continue;
        return i;
    }
    return 0;
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
    for (int i = 0; i < categoryButtons.size(); ++i)
    {
        categoryButtons[i]->selected = i == tabIndex;
        categoryButtons[i]->repaint();
    }
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

    for (const auto& label : labels)
    {
        auto* chip = chipButtons.add (new overlaychrome::ActionButton());
        chipsBar.addAndMakeVisible (*chip);
        chip->text = label;
        chip->selectedStyle = overlaychrome::SelectedStyle::chip;
        chip->mutedIdle = true;
        chip->selected = (label == "All" && selectedSubcategory.isEmpty())
                         || label == selectedSubcategory;
        chip->onClick = [this, label]
        {
            // rebuildChips() deletes THIS chip, and with it the lambda's captured `this` -- keep our own copy on
            // the stack for the call after it (using the capture there was a use-after-free: clicking a
            // subcategory chip such as Bass crashed in rebuildRows()).
            auto* const self = this;
            self->selectedSubcategory = label == "All" ? juce::String() : label;
            self->rebuildChips();
            self->rebuildRows();
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

        // Each row matches on its own merits: name / category / subcategory
        // as before (a user typing "reverb" expects Hall/Plate/Spring too),
        // plus the 'Amps + Cab' rail label -- but that label only earns the
        // CAB row, and only for cab-capable amps. Otherwise a 'cab' search
        // would bury the results under every amp's head-only row plus
        // cab-less amps like the Trace Elliot.
        const bool ownMatch = name.containsIgnoreCase (query)
            || EffectRegistry::categoryDisplayName (category).containsIgnoreCase (query)
            || subcategory.containsIgnoreCase (query);
        const bool cabLabelMatch = category == EffectRegistry::Category::amps
            && juce::String ("Amps + Cab").containsIgnoreCase (query);
        if (searching && ! ownMatch && ! cabLabelMatch)
            continue;
        if (! searching && category != tab.category)
            continue;
        if (! searching && selectedSubcategory.isNotEmpty() && subcategory != selectedSubcategory)
            continue;

        // cabCapable() keys off the `*_cab` parameter, so new amps get
        // their cab-variant rows for free. The two amp tabs show one
        // insertion variant each; other tabs list both rows together, and
        // a global search lists whichever variants actually matched.
        auto* probe = probeForKey (key);
        const bool cab = cabCapable (probe);
        const bool plainRow = searching ? ownMatch : tab.rows != RailTab::Rows::cabOnly;
        const bool cabRow   = cab && (searching ? (ownMatch || cabLabelMatch)
                                               : tab.rows != RailTab::Rows::headOnly);
        if (! plainRow && ! cabRow)
            continue; // a cab-less amp in the 'Amps + Cab' tab, or an unmatched row in search

        const auto searchSubline = [&] (bool forCabRow)
        {
            auto groupLabel = EffectRegistry::categoryDisplayName (category);
            if (forCabRow)
                groupLabel += " + Cab";
            return groupLabel + (subcategory.isNotEmpty() ? " - " + subcategory : juce::String());
        };

        if (plainRow)
        {
            auto& row = rowList.addRow();
            row.key = key;
            row.name = name;
            row.subline = searching ? searchSubline (false)
                                    : (subcategory.isNotEmpty() ? subcategory
                                                                : EffectRegistry::categoryDisplayName (category));
            row.tag = searching ? EffectRegistry::categoryDisplayName (category) : juce::String();
            row.cpuLevel = effectcost::levelForKey (key);
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
            row.subline = searching ? searchSubline (true)
                                    : (subcategory.isNotEmpty() ? subcategory
                                                                : EffectRegistry::categoryDisplayName (category));
            row.tag = searching ? EffectRegistry::categoryDisplayName (category) : juce::String();
            row.cpuLevel = effectcost::levelForKey (key);
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
    auto area = getLocalBounds();

    auto header = area.removeFromTop (76).reduced (20, 0);
    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    titleLabel.setBounds (header.removeFromLeft (220));
    if (header.getWidth() > 24)
    {
        const int searchWidth = juce::jmin (420, header.getWidth() - 12);
        searchField.setBounds (header.removeFromLeft (searchWidth).withSizeKeepingCentre (searchWidth, 52));
    }
    else
    {
        searchField.setBounds (header.withSizeKeepingCentre (0, 52));
    }

    railViewport.setBounds (area.removeFromLeft (railWidth));
    auto content = area.reduced (20, 16);
    if (chipsViewport.isVisible())
    {
        chipsViewport.setBounds (content.removeFromTop (48));
        content.removeFromTop (12);

        const juce::Font chipFont (13.0f);
        int x = 0;
        for (auto* chip : chipButtons)
        {
            const int w = juce::jmax (64, chipFont.getStringWidth (chip->text) + 32);
            chip->setBounds (x, 0, w, 48);
            x += w + 8;
        }
        chipsBar.setSize (x, chipsViewport.getMaximumVisibleHeight());
    }

    gridViewport.setBounds (content);
    emptyStateLabel.setBounds (content);

    const int railContentWidth = railViewport.getMaximumVisibleWidth();
    rail.setSize (railContentWidth, juce::jmax (0, railViewport.getMaximumVisibleHeight()));
    const int buttonStep = 52;
    for (int i = 0; i < categoryButtons.size(); ++i)
    {
        categoryButtons[i]->setBounds (12, 12 + i * buttonStep, juce::jmax (48, railContentWidth - 24), 48);
    }

    const int gridContentWidth = gridViewport.getMaximumVisibleWidth();
    rowList.setSize (gridContentWidth, rowList.layoutForWidth (gridContentWidth));
}

void AddEffectBrowserDialog::paint (juce::Graphics& g)
{
    overlaychrome::paintCardBackground (g, getLocalBounds().toFloat());
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 76.0f));

    const auto& t = theme::current();
    g.setColour (t.line);
    g.drawVerticalLine (railWidth, 76.0f, (float) getHeight());
}

} // namespace openguitarmultifx
