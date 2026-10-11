#pragma once

#include "../EffectRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>

namespace openguitarmultifx
{

/**
    The Add-Effect browser: replaces the old nested juce::PopupMenu that
    opened from a "+" grid slot (unusable on touch with ~120 registered
    effects, and amps/effects all mixed together). An OverlayHost card like
    every other dialog here -- a scrollable category rail on the left
    (categories come from EffectRegistry's registration metadata, so a new
    effect lands in the right tab on its own), a scrollable tile grid on
    the right, and a search field that filters tiles across ALL categories
    live. Tap a tile = pick that effect; the owner then calls addEffect()
    and pops the card.

    Tiles reuse the same visual language as EffectBlockComponent (accent
    outline + drawIcon() glyph + name along the bottom) so what you pick
    looks exactly like what lands on the grid.
*/
class AddEffectBrowserDialog : public juce::Component
{
public:
    explicit AddEffectBrowserDialog (EffectRegistry& registryToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;

    // ---- callbacks wired by MainComponent ----

    /** Fires when an effect tile is tapped (add this registry key). */
    std::function<void (juce::String registryKey)> onEffectChosen;
    std::function<void()> onPopOverlay;

private:
    /** One grid cell: accent-outlined rounded tile with the effect's own
        drawIcon() glyph and its display name (same two-zone layout as the
        chain's EffectBlockComponent). Tap = onTap. */
    class EffectTile : public juce::Component
    {
    public:
        juce::String key;
        juce::String name;
        juce::String subline;   // shown while searching: which category this hit belongs to
        const EffectProcessor* probe = nullptr; // icon + accent; owned by the dialog's probe cache

        std::function<void()> onTap;

        void paint (juce::Graphics& g) override;
        void mouseUp (const juce::MouseEvent& event) override;
    };

    /** Free-flowing rows of EffectTiles inside a Viewport. */
    class TileGrid : public juce::Component
    {
    public:
        juce::OwnedArray<EffectTile> tiles;
        std::function<void (EffectTile&)> onTileTap;

        void clearTiles();
        EffectTile& addTile();
        /** Lays the tiles out across `width` and returns the height they
            occupy -- the caller then sizes this component to it. */
        int layoutForWidth (int width);
    };

    /** The tile's icon/accent source: a lazily created (and cached)
        instance from registry.create(). */
    const EffectProcessor* probeForKey (const juce::String& key);
    void selectCategory (EffectRegistry::Category category);
    void rebuildTiles();

    EffectRegistry& registry;
    EffectRegistry::Category selectedCategory = EffectRegistry::Category::guitarAmps;
    std::map<juce::String, std::unique_ptr<EffectProcessor>> probes;
    std::map<EffectRegistry::Category, int> counts;

    juce::Label titleLabel;
    juce::TextEditor searchField;

    juce::Viewport railViewport;
    juce::Component rail;
    juce::OwnedArray<juce::TextButton> categoryButtons;

    juce::Viewport gridViewport;
    TileGrid tileGrid;
    juce::Label emptyStateLabel;
    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
