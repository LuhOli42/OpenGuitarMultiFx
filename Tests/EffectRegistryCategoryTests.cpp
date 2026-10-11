#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

/**
    Every registered effect key must land in a real Add-Effect category.
    Before categories lived in the registry, classification was a hardcoded
    display-name table in MainComponent's add-effect menu: a new effect that
    nobody hand-added there silently fell into an "Other" submenu (the
    DS-1/OD-1 shipped unreachable through the UI for a full session that
    way), and bass amps couldn't be told from guitar amps by name at all.
    Category is now registration metadata (registerType's second argument),
    and this test is the tripwire that keeps every key mapped -- run it
    after adding any registerType() call.
*/
class EffectRegistryCategoryTests : public juce::UnitTest
{
public:
    EffectRegistryCategoryTests() : juce::UnitTest ("EffectRegistryCategory", "Registry") {}

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        const auto keys = registry.getRegisteredNames();
        beginTest ("registry contains the full built-in set");
        expect (keys.size() > 100, "expected the full built-in set, got " + juce::String (keys.size()));

        beginTest ("every registered key maps to a real category");
        for (auto& key : keys)
        {
            const auto category = registry.categoryForKey (key);
            const auto label = EffectRegistry::categoryDisplayName (category);
            expect (label.isNotEmpty(), key + " -> unnamed category");
        }

        beginTest ("bass amps are split from guitar amps");
        for (auto& key : keys)
        {
            const auto category = registry.categoryForKey (key);
            if (key.contains ("Amplifier"))
                expect (category == EffectRegistry::Category::guitarAmps
                        || category == EffectRegistry::Category::bassAmps,
                        key + " is an amp outside the amp categories");
        }

        // The bass heads and bass preamp must land in Bass Amps -- this was
        // the exact complaint behind the split ("muito amp misturado").
        for (const char* key : { "SVTStyleAmplifier", "AmpegB15StyleAmplifier", "AmpegV4BStyleAmplifier",
                                 "MesaBass400PlusStyleAmplifier", "Acoustic360StyleAmplifier",
                                 "TraceElliotGP12StyleAmplifier" })
        {
            expect (registry.categoryForKey (key) == EffectRegistry::Category::bassAmps,
                    juce::String (key) + " should be a Bass Amp");
        }

        // Every tab except Utility (empty today, kept for future helpers)
        // must have at least one member -- an empty tab in the browser is a
        // broken category, not a UI choice.
        beginTest ("every populated-category tab has members");
        for (auto category : EffectRegistry::categoriesInDisplayOrder())
        {
            if (category == EffectRegistry::Category::utility)
                continue;

            int count = 0;
            for (auto& key : keys)
                if (registry.categoryForKey (key) == category)
                    ++count;
            expect (count > 0, EffectRegistry::categoryDisplayName (category) + " is empty");
        }

        // Print the full category table -- the same table that goes in the
        // PR description, kept honest by the assertions above.
        juce::StringArray lines;
        for (auto category : EffectRegistry::categoriesInDisplayOrder())
        {
            juce::StringArray members;
            for (auto& key : keys)
                if (registry.categoryForKey (key) == category)
                    members.add (registry.displayNameForKey (key));
            lines.add (EffectRegistry::categoryDisplayName (category) + " ("
                       + juce::String (members.size()) + "): " + members.joinIntoString (", "));
        }
        juce::Logger::writeToLog ("\n" + lines.joinIntoString ("\n"));
    }
};

static EffectRegistryCategoryTests effectRegistryCategoryTests;

} // namespace openguitarmultifx
