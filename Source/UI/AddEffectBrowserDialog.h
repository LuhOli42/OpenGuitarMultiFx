#pragma once

#include "../EffectRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    The Add-Effect browser: replaces the old nested juce::PopupMenu that
    opened from a "+" grid slot (unusable on touch with ~120 registered
    effects, and amps/effects all mixed together). An OverlayHost card like
    every other dialog here -- a scrollable category rail on the left
    (categories come from EffectRegistry's registration metadata, so a new
    effect lands in the right tab on its own), a scrollable list of effect
    rows on the right, and a search field that filters rows across ALL
    categories live. Tap a row = pick that effect; the owner then calls
    addEffect() and pops the card.

    Rows are name-first list rows at the 48px touch floor, not icon tiles:
    the glyph survives only as a small ornament next to the name. Inside a
    category with more than one subcategory (Amps -> Clean / Crunch /
    High-Gain / Bass, Fuzz -> Big Muff / Fuzz Face / Tone Bender, ...) a
    second row of chips filters the list further -- same
    registration-metadata mechanism as the category itself, no name-string
    matching.

    The single `amps` registry category gets TWO rail entries: 'Amps'
    (every amp, head-only insert) and 'Amps + Cab' (the same amps with
    their page-2 cab on) -- amps used to sit split across Guitar/Bass Amps
    tabs with '+ Cab' rows interleaved in the same list.
*/
class AddEffectBrowserDialog : public juce::Component
{
public:
    explicit AddEffectBrowserDialog (EffectRegistry& registryToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;

    // ---- callbacks wired by MainComponent ----

    /** Fires when an effect row is tapped (add this registry key). `cabOn`
        is the insertion variant for amps that carry a page-2 Cab selector:
        the 'Amps' rail entry lists them cab off (head-only glyph), the
        'Amps + Cab' entry lists them with the amp's historic cab voicing
        on (head+cabinet glyph). Always false for rows without a cab
        variant. */
    std::function<void (juce::String registryKey, bool cabOn)> onEffectChosen;
    std::function<void()> onPopOverlay;

private:
    /** One list row: a small accent-tinted icon ornament, then the effect's
        display name, then an optional context line (its category while
        searching, else its subcategory). Tap = onTap. */
    class EffectRow : public juce::Component
    {
    public:
        juce::String key;
        juce::String name;
        juce::String subline;
        bool cabOn = false;        // '+ Cab' insertion variant -- see onEffectChosen
        bool headOnlyIcon = false; // the cab-off row draws the head-only glyph, not the probe's
        const EffectProcessor* probe = nullptr; // icon + accent; owned by the dialog's probe cache

        std::function<void()> onTap;

        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& event) override;
    };

    /** Stacked rows of EffectRow inside a Viewport. */
    class RowList : public juce::Component
    {
    public:
        juce::OwnedArray<EffectRow> rows;
        std::function<void (EffectRow&)> onRowTap;

        void clearRows();
        EffectRow& addRow();
        /** Lays the rows out down `width` and returns the height they
            occupy -- the caller then sizes this component to it. */
        int layoutForWidth (int width);
    };

    /** One rail entry. `both` shows a plain row plus a '+ Cab' row for
        cab-capable keys (the non-amp tabs, where nothing is cab-capable
        today); `headOnly` and `cabOnly` are the two amp tabs, one
        insertion variant each. */
    struct RailTab
    {
        juce::String label;
        EffectRegistry::Category category;
        enum class Rows { both, headOnly, cabOnly };
        Rows rows = Rows::both;
        int count = 0;
    };

    /** The row's icon/accent source: a lazily created (and cached)
        instance from registry.create(). */
    const EffectProcessor* probeForKey (const juce::String& key);
    void selectTab (int tabIndex);
    void rebuildChips();
    void rebuildRows();

    EffectRegistry& registry;
    std::vector<RailTab> railTabs;
    int selectedTab = 0;
    juce::String selectedSubcategory; // empty = All
    std::map<juce::String, std::unique_ptr<EffectProcessor>> probes;
    std::map<EffectRegistry::Category, int> counts;

    juce::Label titleLabel;
    juce::TextEditor searchField;

    juce::Viewport railViewport;
    juce::Component rail;
    juce::OwnedArray<juce::TextButton> categoryButtons;

    // Chips live in a horizontal Viewport: wide chip sets (Reverb has six)
    // would otherwise clip the rightmost filters on a narrow window.
    juce::Viewport chipsViewport;
    juce::Component chipsBar;
    juce::OwnedArray<juce::TextButton> chipButtons;

    juce::Viewport gridViewport;
    RowList rowList;
    juce::Label emptyStateLabel;
    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
