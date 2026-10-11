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

        beginTest ("subcategories are consistent with the categories they group");
        for (auto& key : keys)
        {
            const auto sub = registry.subcategoryForKey (key);
            if (sub.isNotEmpty())
                expect (registry.subcategoriesForCategory (registry.categoryForKey (key)).contains (sub),
                        key + " -> subcategory '" + sub + "' missing from its own category's chip list");
        }

        // The amp split into voicing classes: every guitar amp has to land
        // in exactly one of Clean / Crunch / High-Gain, or the browser's
        // chip row silently leaves it under All only.
        for (const char* key : { "TwinReverbStyleAmplifier", "DeluxeReverbStyleAmplifier", "JC120StyleAmplifier" })
            expect (registry.subcategoryForKey (key) == "Clean",
                    juce::String (key) + " should be a Clean amp");
        for (const char* key : { "AC15StyleAmplifier", "AC30StyleAmplifier", "JTM45StyleAmplifier",
                                 "BassmanStyleAmplifier", "MatchlessHC30StyleAmplifier" })
            expect (registry.subcategoryForKey (key) == "Crunch",
                    juce::String (key) + " should be a Crunch amp");
        for (const char* key : { "JCM800StyleAmplifier", "SLO100StyleAmplifier", "MarkIICPlusStyleAmplifier",
                                 "DualRectifierStyleAmplifier", "EVH5150StyleAmplifier" })
            expect (registry.subcategoryForKey (key) == "High-Gain",
                    juce::String (key) + " should be a High-Gain amp");

        for (const char* key : { "TS9BStyleOverdrive", "ODB3StyleOverdrive", "DarkglassB7KStyleOverdrive",
                                 "SansAmpBDDIStyleOverdrive" })
            expect (registry.subcategoryForKey (key) == "Bass",
                    juce::String (key) + " should be a Bass drive");

        beginTest ("a category only earns a chip row when more than one subcategory is populated");
        expect (registry.subcategoriesForCategory (EffectRegistry::Category::guitarAmps).size() == 3);
        expect (registry.subcategoriesForCategory (EffectRegistry::Category::bassAmps).isEmpty(),
                "bass amps are one group -- no chip row");
        expect (registry.subcategoriesForCategory (EffectRegistry::Category::distortion).size() <= 1);
        expect (registry.subcategoriesForCategory (EffectRegistry::Category::neural).size() <= 1);

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
            {
                if (registry.categoryForKey (key) != category)
                    continue;
                const auto sub = registry.subcategoryForKey (key);
                members.add (registry.displayNameForKey (key)
                             + (sub.isNotEmpty() ? " [" + sub + "]" : juce::String()));
            }
            lines.add (EffectRegistry::categoryDisplayName (category) + " ("
                       + juce::String (members.size()) + "): " + members.joinIntoString (", "));
        }
        juce::Logger::writeToLog ("\n" + lines.joinIntoString ("\n"));
    }
};

static EffectRegistryCategoryTests effectRegistryCategoryTests;

} // namespace openguitarmultifx
