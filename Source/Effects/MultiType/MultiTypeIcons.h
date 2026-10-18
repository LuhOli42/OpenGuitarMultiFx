#pragma once

#include "../IconKit.h"
#include <IconData.h>

#include <map>

namespace openguitarmultifx::mt
{

/** Picks the approved reference-sheet glyph for a block from its type name (docs/icons/AGENT-icon-notes.md), so every
    type of a multi-type engine shows the glyph of what it actually is instead of one icon per class. */
inline void drawIconByKey (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& key)
{
    static const std::map<juce::String, std::unique_ptr<juce::Drawable>> icons = [] {
        std::map<juce::String, std::unique_ptr<juce::Drawable>> m;
#define OGMFX_ICON(n) m.emplace (#n, icon::loadSvg (IconData::n##_svg, IconData::n##_svgSize))
        OGMFX_ICON (digital_delay); OGMFX_ICON (analog_delay); OGMFX_ICON (tape_delay); OGMFX_ICON (ping_pong);
        OGMFX_ICON (dual_delay); OGMFX_ICON (reverse_delay); OGMFX_ICON (multi_tap);
        OGMFX_ICON (room); OGMFX_ICON (hall); OGMFX_ICON (plate); OGMFX_ICON (spring); OGMFX_ICON (shimmer); OGMFX_ICON (ambient);
        OGMFX_ICON (pitch_mod); OGMFX_ICON (pitch_shift); OGMFX_ICON (harmonizer); OGMFX_ICON (octaver);
        OGMFX_ICON (pulse); OGMFX_ICON (ring_mod);
#undef OGMFX_ICON
        return m;
    }();
    const auto it = icons.find (key);
    if (it != icons.end())
        icon::drawSvg (g, b, it->second.get());
}

inline juce::String delayIconKey (const juce::String& n)
{
    if (n.containsIgnoreCase ("tape")) return "tape_delay";
    if (n.containsIgnoreCase ("ping")) return "ping_pong";
    if (n.containsIgnoreCase ("reverse")) return "reverse_delay";
    if (n.containsIgnoreCase ("dual")) return "dual_delay";
    if (n.containsIgnoreCase ("analog") || n.containsIgnoreCase ("bbd") || n.containsIgnoreCase ("mind guy")
        || n.containsIgnoreCase ("graphite")) return "analog_delay";
    return "digital_delay";
}

inline juce::String reverbIconKey (const juce::String& n, bool spring)
{
    if (spring) return "spring";
    if (n.containsIgnoreCase ("plate")) return "plate";
    if (n.containsIgnoreCase ("shimmer")) return "shimmer";
    if (n.containsIgnoreCase ("ambien")) return "ambient";
    if (n.containsIgnoreCase ("room") || n.containsIgnoreCase ("studio") || n.containsIgnoreCase ("chamber")) return "room";
    return "hall";
}

inline juce::String pitchIconKey (const juce::String& n)
{
    if (n.containsIgnoreCase ("detune")) return "pitch_mod";
    if (n.containsIgnoreCase ("octave")) return "octaver";
    if (n.containsIgnoreCase ("interval") || n.containsIgnoreCase ("harmony")) return "harmonizer";
    return "pitch_shift"; // whammy, capo, free shifter
}

} // namespace openguitarmultifx::mt
