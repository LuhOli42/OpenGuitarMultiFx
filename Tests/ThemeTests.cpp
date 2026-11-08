#include "UI/Theme.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

class ThemeTests : public juce::UnitTest
{
public:
    ThemeTests() : juce::UnitTest ("Theme", "UI") {}

    void runTest() override
    {
        beginTest ("every theme key round-trips through parseKey");
        for (int i = 0; i < theme::count; ++i)
        {
            const auto& t = theme::get ((theme::Id) i);
            expect ((int) t.id == i);
            theme::Id parsed = theme::Id::stageGlass;
            expect (theme::parseKey (juce::String (t.key) + "\n", parsed));
            expect (parsed == t.id);
        }

        beginTest ("unknown keys are rejected and leave the output alone");
        {
            theme::Id out = theme::Id::console;
            expect (! theme::parseKey ("", out));
            expect (! theme::parseKey ("neon", out));
            expect (out == theme::Id::console);
        }

        beginTest ("themes are visually distinct and readable");
        for (int i = 0; i < theme::count; ++i)
        {
            const auto& t = theme::get ((theme::Id) i);
            expect (t.text.getPerceivedBrightness() - t.background.getPerceivedBrightness() > 0.7f, t.key);
            expect (t.textMuted.getPerceivedBrightness() > t.textDim.getPerceivedBrightness(), t.key);
            expect (t.radius > 0.0f && t.smallRadius > 0.0f && t.smallRadius <= t.radius, t.key);
            for (auto* c = t.displayName; *c != 0; ++c)
                expect ((unsigned char) *c < 0x80, "display names are drawn with the embedded font -- ASCII only");
            for (int j = i + 1; j < theme::count; ++j)
                expect (t.background != theme::get ((theme::Id) j).background);
        }

        beginTest ("setCurrent switches current()");
        {
            const auto before = theme::currentId();
            theme::setCurrent (theme::Id::boldBlocks);
            expect (theme::current().id == theme::Id::boldBlocks);
            theme::setCurrent (before);
        }
    }
};

static ThemeTests themeTests;

} // namespace openguitarmultifx
