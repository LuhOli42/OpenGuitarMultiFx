#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx
{

/** Every control on a modelled amp's real front panel lives on page 1; page 2 and later hold only the synthetic ones
    (Power Drive, Bias, Tube Feel, Speaker, Output, Cab, Sag, Smooth). A real switch hidden on page 2 -- the Dumble's
    Voicing / Bright / Deep / Filter switches were -- is a bug (user, 2026-10-27). */
class AmpPanelPageTests : public juce::UnitTest
{
public:
    AmpPanelPageTests() : juce::UnitTest ("AmpPanelPage", "Effects") {}

    static bool isSynthetic (const juce::String& name)
    {
        for (auto* s : { "Power Drive", "Bias", "Tube Feel", "Speaker", "Output", "Cab", "Sag", "Smooth" })
            if (name.startsWithIgnoreCase (s))
                return true;
        return false;
    }

    void runTest() override
    {
        beginTest ("amps keep every real panel control on page 1");
        EffectRegistry registry;
        registerBuiltInEffects (registry);
        for (auto& key : registry.getRegisteredNames())
        {
            if (registry.categoryForKey (key) != EffectRegistry::Category::amps)
                continue;
            auto fx = registry.create (key);
            if (fx == nullptr)
                continue;
            const auto pages = fx->getParameterPages();
            juce::StringArray misplaced;
            for (size_t p = 1; p < pages.size(); ++p)
                for (auto* param : pages[p])
                    if (! isSynthetic (param->getName (64)))
                        misplaced.add (param->getName (64));
            if (! misplaced.isEmpty())
                logMessage (key + ": page 2 holds " + misplaced.joinIntoString (", "));
            expect (misplaced.isEmpty(), key + " keeps real controls past page 1");
        }
    }
};

static AmpPanelPageTests ampPanelPageTests;

} // namespace openguitarmultifx
