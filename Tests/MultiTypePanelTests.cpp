#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

/*  The Multi-Type blocks run a whole engine per category, but the panel should read like a pedal: only the controls
    that shape the chosen type are shown (isParameterActive), the rest is the type's voicing. This keeps every one of
    them at two panel pages or less, and checks the core knobs never disappear. */
class MultiTypePanelTests : public juce::UnitTest
{
public:
    MultiTypePanelTests() : juce::UnitTest ("MultiTypePanel", "Effects") {}

    void runTest() override
    {
        EffectRegistry registry;
        registerBuiltInEffects (registry);

        beginTest ("every Multi-Type block shows a pedal-sized set of controls");
        int checked = 0;
        for (const auto& key : registry.getRegisteredNames())
        {
            if (! key.startsWith ("Mt") && ! key.startsWith ("MultiType"))
                continue;
            auto fx = registry.create (key);
            if (fx == nullptr || fx->getParameters() == nullptr)
                continue;

            int total = 0, shown = 0;
            juce::StringArray names;
            bool hasLevel = false, typeShown = false;
            for (auto* prm : fx->getParameters()->getParameters (true))
                if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (prm))
                {
                    ++total;
                    if (! fx->isParameterActive (*f))
                        continue;
                    ++shown;
                    names.add (f->getName (32));
                    hasLevel = hasLevel || f->paramID == "level";
                    typeShown = typeShown || f->paramID == "type";
                }
            logMessage (key.paddedRight (' ', 22) + juce::String (shown).paddedLeft (' ', 3) + " of "
                        + juce::String (total).paddedLeft (' ', 3) + ": " + names.joinIntoString (", "));
            expectLessOrEqual (shown, 14, key);
            expect (hasLevel, key + " lost its Level knob");
            // a fixed-type block has no Type selector; the generic block keeps it
            expect (typeShown == key.startsWith ("MultiType"), key + " Type selector");
            ++checked;
        }
        expectGreaterThan (checked, 50);
    }
};

static MultiTypePanelTests multiTypePanelTests;

} // namespace openguitarmultifx
