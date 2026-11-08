#include "Theme.h"

namespace openguitarmultifx::theme
{
namespace
{
    // Values are the prototype's tokens (docs/ui/prototype.html) -- keep the two in sync.
    const std::array<Tokens, count> tokens { {
        { Id::stageGlass, "stage-glass", "Stage Glass",
          juce::Colour (0xff0a0b0e), juce::Colour (0xff121419), juce::Colour (0xff191c23), juce::Colour (0xff242833),
          juce::Colour (0x12ffffff), juce::Colour (0x21ffffff),
          juce::Colour (0xfff3f5f8), juce::Colour (0xff8b92a0), juce::Colour (0xff555c69),
          18.0f, 12.0f, false, BlockStyle::glow },
        { Id::console, "console", "Console",
          juce::Colour (0xff1a1b1e), juce::Colour (0xff212226), juce::Colour (0xff2a2b30), juce::Colour (0xff36373d),
          juce::Colour (0xff34353b), juce::Colour (0xff45464d),
          juce::Colour (0xffe6e6e8), juce::Colour (0xff9a9ba2), juce::Colour (0xff64656c),
          5.0f, 4.0f, true, BlockStyle::topStripe },
        { Id::boldBlocks, "bold-blocks", "Bold Blocks",
          juce::Colour (0xff0d0d0d), juce::Colour (0xff171717), juce::Colour (0xff202020), juce::Colour (0xff2c2c2c),
          juce::Colour (0xff262626), juce::Colour (0xff3a3a3a),
          juce::Colour (0xfff5f2ea), juce::Colour (0xff9b978d), juce::Colour (0xff5d5a54),
          14.0f, 10.0f, true, BlockStyle::filled },
    } };

    Id active = Id::stageGlass;
}

const Tokens& get (Id id) noexcept { return tokens[(size_t) juce::jlimit (0, count - 1, (int) id)]; }
const Tokens& current() noexcept { return get (active); }
Id currentId() noexcept { return active; }
void setCurrent (Id id) noexcept { active = get (id).id; }

bool parseKey (const juce::String& key, Id& out) noexcept
{
    const auto trimmed = key.trim();
    for (const auto& t : tokens)
        if (trimmed == t.key)
        {
            out = t.id;
            return true;
        }
    return false;
}

juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("theme.txt");
}

void load()
{
    Id id = Id::stageGlass;
    if (settingsFile().existsAsFile())
        parseKey (settingsFile().loadFileAsString(), id);
    setCurrent (id);
}

void save()
{
    settingsFile().getParentDirectory().createDirectory();
    settingsFile().replaceWithText (current().key);
}

} // namespace openguitarmultifx::theme
