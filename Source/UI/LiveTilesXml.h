#pragma once

#include "../Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace openguitarmultifx::livetiles
{

/**
    What each of the stage screen's eight big buttons does (LiveOverlay): recall a scene, toggle one effect on/off, or
    nothing. The default is "button i = scene i" (A-H), which is what the screen did before the buttons were
    configurable, and a preset that never customised them is saved without any <LiveTiles> element.

    Per preset: a pedal tile points at a block of THIS preset, so it is stored as "block index" inside the preset (same
    trick as BypassPedalXml.h) and resolved against the freshly loaded blocks.
*/
enum class Kind { scene, pedal, none };

struct Tile
{
    Kind kind = Kind::scene;
    int scene = 0;                    // Kind::scene: 0-7
    EffectProcessor* pedal = nullptr; // Kind::pedal

    bool operator== (const Tile& other) const
    {
        return kind == other.kind && scene == other.scene && pedal == other.pedal;
    }
};

constexpr int slots = 8;
using Layout = std::array<Tile, slots>;
using Blocks = std::vector<EffectProcessor*>; // one per <Block> element, in order (nullptr for a skipped one)

inline Layout defaultLayout()
{
    Layout layout;
    for (int i = 0; i < slots; ++i)
        layout[(size_t) i] = { Kind::scene, i, nullptr };
    return layout;
}

inline void write (juce::XmlElement& presetXml, const Layout& layout, const Blocks& blocks)
{
    const auto fallback = defaultLayout();
    juce::XmlElement* tilesXml = nullptr;

    for (int slot = 0; slot < slots; ++slot)
    {
        const auto& tile = layout[(size_t) slot];
        if (tile == fallback[(size_t) slot])
            continue;

        int block = -1;
        if (tile.kind == Kind::pedal)
        {
            for (int i = 0; i < (int) blocks.size(); ++i)
                if (blocks[(size_t) i] == tile.pedal)
                    block = i;
            if (block < 0)
                continue; // the block is not in the preset any more: leave the default
        }

        if (tilesXml == nullptr)
            tilesXml = presetXml.createNewChildElement ("LiveTiles");

        auto* tileXml = tilesXml->createNewChildElement ("Tile");
        tileXml->setAttribute ("slot", slot);
        tileXml->setAttribute ("kind", tile.kind == Kind::scene ? "scene" : tile.kind == Kind::pedal ? "pedal" : "none");
        if (tile.kind == Kind::scene)
            tileXml->setAttribute ("scene", tile.scene);
        else if (tile.kind == Kind::pedal)
            tileXml->setAttribute ("block", block);
    }
}

inline Layout read (const juce::XmlElement& presetXml, const Blocks& blocks)
{
    auto layout = defaultLayout();

    auto* tilesXml = presetXml.getChildByName ("LiveTiles");
    if (tilesXml == nullptr)
        return layout;

    for (auto* tileXml : tilesXml->getChildWithTagNameIterator ("Tile"))
    {
        const int slot = tileXml->getIntAttribute ("slot", -1);
        if (slot < 0 || slot >= slots)
            continue;

        const auto kind = tileXml->getStringAttribute ("kind");
        if (kind == "none")
            layout[(size_t) slot] = { Kind::none, 0, nullptr };
        else if (kind == "scene")
        {
            const int scene = tileXml->getIntAttribute ("scene", -1);
            if (scene >= 0 && scene < slots)
                layout[(size_t) slot] = { Kind::scene, scene, nullptr };
        }
        else if (kind == "pedal")
        {
            const int block = tileXml->getIntAttribute ("block", -1);
            if (block >= 0 && block < (int) blocks.size() && blocks[(size_t) block] != nullptr)
                layout[(size_t) slot] = { Kind::pedal, 0, blocks[(size_t) block] };
        }
    }

    return layout;
}

} // namespace openguitarmultifx::livetiles
