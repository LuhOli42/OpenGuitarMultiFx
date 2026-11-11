#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace openguitarmultifx::theme
{

/**
    The three user-selectable visual themes (Settings -> Appearance). A theme
    only changes how the UI is painted -- presets, routing and sound never
    depend on it. Every surface colour/radius a component paints with comes
    from current() instead of a literal, so switching is "apply tokens to
    the LookAndFeel, send a LookAndFeel change, repaint". Design reference
    and the per-theme rules: docs/ui/REDESIGN.md.
*/
enum class Id
{
    stageGlass = 0,
    console,
    boldBlocks
};

constexpr int count = 3;

/** How an effect block is drawn in the chain (EffectBlockComponent). */
enum class BlockStyle
{
    glow,      // Stage Glass: dark glass card, category-coloured icon + LED glow
    topStripe, // Console: flat graphite card, category colour as a top stripe
    filled     // Bold Blocks: whole block filled with the category colour, bypass = outline only
};

struct Tokens
{
    Id id;
    const char* key;         // stable id persisted in theme.txt -- never rename
    const char* displayName; // ASCII-only: it is drawn with the embedded font

    juce::Colour background; // app / full-screen card background
    juce::Colour surface;    // panels, overlay cards, footer
    juce::Colour raised;     // buttons, fields, idle cells, list rows
    juce::Colour raisedHigh; // tracks, pressed/bypassed fills
    juce::Colour line;       // hairlines, separators
    juce::Colour lineStrong; // resting widget borders
    juce::Colour text;
    juce::Colour textMuted;
    juce::Colour textDim;

    float radius;      // cards, blocks, tiles
    float smallRadius; // buttons, fields, cells
    bool uppercaseLabels;
    BlockStyle blockStyle;
};

const Tokens& get (Id id) noexcept;
const Tokens& current() noexcept;

/** Text/glyph colour for something drawn ON `fill`: `dark` on a light fill, `light` on a dark one. The prototype puts
    #000 on every accent/snapshot fill, so the cut-off is low. (Not `fill.contrasting (a, b)`: that two-argument form is
    a STATIC JUCE function that ignores `fill` and returns a grey contrasting with both a and b.) */
inline juce::Colour inkOn (juce::Colour fill, juce::Colour dark = juce::Colours::black,
                          juce::Colour light = juce::Colours::white) noexcept
{
    return fill.getPerceivedBrightness() > 0.4f ? dark : light;
}
Id currentId() noexcept;

/** UI thread only. Does not repaint or save -- see applyTheme() on the LookAndFeel and save(). */
void setCurrent (Id id) noexcept;

/** Unknown/empty keys return false and leave `out` untouched. */
bool parseKey (const juce::String& key, Id& out) noexcept;

/** <userApplicationDataDirectory>/OpenGuitarMultiFx/theme.txt, holding the key. */
juce::File settingsFile();

/** Reads settingsFile() into the current theme; a missing or unknown file keeps the default (Stage Glass). */
void load();
void save();

} // namespace openguitarmultifx::theme
