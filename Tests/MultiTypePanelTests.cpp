#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

/*  The Multi-Type blocks run a whole engine per category, but the panel should read like a pedal
    (mt::PanelLayout): a front page with the real pedal's knobs ending in Mix/Level, a "More" page of secondary
    controls, the rest hidden as the type's voicing. Checks the shape of every block's layout. */
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

            const auto layout = fx->getPanelLayout();
            expect (layout.size() == 2, key + " has a front and a More page");
            if (layout.size() != 2)
                continue;
            const auto& front = layout[0];
            juce::StringArray names;
            bool typeShown = false;
            for (auto* f : front)
            {
                names.add (f->getName (32));
                typeShown = typeShown || f->paramID == "type";
            }
            juce::StringArray moreNames;
            for (auto* f : layout[1])
                moreNames.add (f->getName (32));
            logMessage (key.paddedRight (' ', 22) + names.joinIntoString (", ") + "  |  More: " + moreNames.joinIntoString (", "));
            expectLessOrEqual ((int) front.size(), 8, key + " front fits one row");
            expectLessOrEqual ((int) layout[1].size(), 10, key + " More page");
            expect (! front.empty() && front.back()->paramID == "level", key + " front ends with Level");
            // a fixed-type block has no Type selector; the generic block keeps it, first
            expect (typeShown == key.startsWith ("MultiType"), key + " Type selector");
            if (typeShown)
                expect (front.front()->paramID == "type", key + " Type first");
            for (const auto& page : layout)
                for (auto* f : page)
                    expect (f != nullptr && fx->isParameterActive (*f), key + " layout control active");
            ++checked;
        }
        expectGreaterThan (checked, 50);
    }
};

static MultiTypePanelTests multiTypePanelTests;

} // namespace openguitarmultifx
