#include "EffectRegistry.h"
#include "../Source/UI/ParameterPanelPagePlanner.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace openguitarmultifx
{

/** Every control on a modelled amp's real front panel lives on page 1; page 2 and later hold only the synthetic ones
    (Power Drive, Bias, Tube Feel, Speaker, Output, Cab, Sag, Smooth) and internal trims ("... Trim", like the Steel
    String's Presence Trim, a chassis pot with no knob on the panel). A real switch hidden on page 2 -- the Dumble's
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
        return name.endsWithIgnoreCase (" Trim");
    }

    static juce::AudioParameterFloat* findParameter (EffectProcessor& fx, const char* id)
    {
        for (auto* parameter : fx.getParameters()->getParameters (true))
            if (auto* floatParameter = dynamic_cast<juce::AudioParameterFloat*> (parameter))
                if (floatParameter->paramID == id)
                    return floatParameter;
        return nullptr;
    }

    static std::vector<std::vector<juce::AudioParameterFloat*>> plannedPages (EffectProcessor& fx)
    {
        std::vector<std::vector<juce::AudioParameterFloat*>> result;
        for (const auto& sourcePage : fx.getParameterPages())
        {
            auto split = parameterPanelLayout::splitSourcePage (fx, sourcePage);
            result.insert (result.end(), split.begin(), split.end());
        }
        return result;
    }

    static juce::StringArray activeNames (EffectProcessor& fx,
                                          const std::vector<juce::AudioParameterFloat*>& page)
    {
        juce::StringArray names;
        for (auto* parameter : page)
            if (parameter != nullptr && fx.isParameterActive (*parameter))
                names.add (parameter->getName (64));
        return names;
    }

    static juce::StringArray activeKnobNames (EffectProcessor& fx,
                                              const std::vector<juce::AudioParameterFloat*>& page)
    {
        juce::StringArray names;
        for (auto* parameter : page)
            if (parameter != nullptr && fx.isParameterActive (*parameter)
                && ! parameterPanelLayout::isSelector (fx, *parameter)
                && ! fx.prefersFader (*parameter))
                names.add (parameter->getName (64));
        return names;
    }

    static bool splitPagesRespectDeclaredOrder (EffectProcessor& fx)
    {
        for (const auto& sourcePage : fx.getParameterPages())
        {
            const auto activeSourceCount = std::count_if (sourcePage.begin(), sourcePage.end(),
                [&] (auto* parameter) { return parameter != nullptr && fx.isParameterActive (*parameter); });
            if (activeSourceCount <= 8)
                continue;

            for (const auto& page : parameterPanelLayout::splitSourcePage (fx, sourcePage))
            {
                std::vector<juce::AudioParameterFloat*> expected;
                for (auto* parameter : sourcePage)
                    if (parameter != nullptr && fx.isParameterActive (*parameter)
                        && std::find (page.begin(), page.end(), parameter) != page.end())
                        expected.push_back (parameter);
                const auto presence = std::find_if (expected.begin(), expected.end(), [] (auto* parameter)
                {
                    return parameter->getName (64).equalsIgnoreCase ("Presence");
                });
                const auto master = std::find_if (expected.begin(), expected.end(), [] (auto* parameter)
                {
                    return parameter->getName (64).equalsIgnoreCase ("Master");
                });
                if (presence != expected.end() && master != expected.end() && presence > master)
                {
                    auto* presenceParameter = *presence;
                    expected.erase (presence);
                    const auto reorderedMaster = std::find_if (expected.begin(), expected.end(), [] (auto* parameter)
                    {
                        return parameter->getName (64).equalsIgnoreCase ("Master");
                    });
                    expected.insert (reorderedMaster, presenceParameter);
                }
                if (page != expected)
                    return false;
            }
        }
        return true;
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

        beginTest ("controls that do nothing on the selected channel are hidden, through the registry's wrappers");
        {
            const struct { const char* key; const char* channelId; const char* controlId; float hiddenAt, shownAt; } cases[] = {
                { "JC120StyleAmplifier", "jc_channel", "jc_distortion", 0.0f, 1.0f },
                { "TwinReverbStyleAmplifier", "tr_input", "tr_speed", 0.0f, 1.0f },
                { "DeluxeReverbStyleAmplifier", "dr_input", "dr_intensity", 0.0f, 1.0f },
                { "SuperSonicStyleAmplifier", "ssc_channel", "ssc_gain1", 0.0f, 1.0f },
                { "SuperSonicStyleAmplifier", "ssc_channel", "ssc_v_volume", 1.0f, 0.0f },
                { "DumbleODSStyleAmplifier", "ods_channel", "ods_ratio", 0.0f, 1.0f },
                { "BassmanStyleAmplifier", "bm_input", "bm_vol_bright", 0.0f, 1.0f },
                { "SuperLeadStyleAmplifier", "sl_input", "sl_loud2", 2.0f, 0.0f },
                { "DividedBy13FTR37StyleAmplifier", "f37_input", "f37_boost", 0.0f, 2.0f },
                { "MarkIICPlusStyleAmplifier", "mk2c_channel", "mk2c_lead_master", 0.0f, 1.0f },
                { "RockerverbStyleAmplifier", "rv_channel", "rv_c_volume", 1.0f, 0.0f },
                { "SLO100StyleAmplifier", "slo_channel", "slo_n_master", 1.0f, 0.0f },
                { "MatchlessHC30StyleAmplifier", "hc30_channel", "hc30_tone2", 0.0f, 1.0f },
                { "HotCat30StyleAmplifier", "hotcat30_channel", "hotcat30_edge", 1.0f, 0.0f },
            };
            for (const auto& c : cases)
            {
                auto fx = registry.create (c.key);
                auto* channel = findParameter (*fx, c.channelId);
                auto* control = findParameter (*fx, c.controlId);
                expect (channel != nullptr && control != nullptr, c.controlId);
                if (channel == nullptr || control == nullptr)
                    continue;
                *channel = c.hiddenAt;
                expect (! fx->isParameterActive (*control), juce::String (c.controlId) + " should be hidden");
                *channel = c.shownAt;
                expect (fx->isParameterActive (*control), juce::String (c.controlId) + " should be shown");
            }
        }

        beginTest ("width-based pages never contain only selector controls");
        for (auto& key : registry.getRegisteredNames())
        {
            auto fx = registry.create (key);
            if (fx == nullptr)
                continue;
            for (const auto& sourcePage : fx->getParameterPages())
            {
                const auto activeSourceCount = std::count_if (sourcePage.begin(), sourcePage.end(),
                    [&] (auto* parameter) { return parameter != nullptr && fx->isParameterActive (*parameter); });
                if (activeSourceCount <= 8)
                    continue;
                for (const auto& page : parameterPanelLayout::splitSourcePage (*fx, sourcePage))
                {
                    std::vector<juce::AudioParameterFloat*> active;
                    for (auto* parameter : page)
                        if (parameter != nullptr && fx->isParameterActive (*parameter))
                            active.push_back (parameter);
                    expect (! parameterPanelLayout::isSelectorOnlyPage (active, *fx),
                            key + " has a selector-only page");
                }
            }
        }

        beginTest ("Mark IIC+ keeps core controls and graphic EQ on width-based pages");
        {
            auto fx = registry.create ("MarkIICPlusStyleAmplifier");
            expect (fx != nullptr, "Mark IIC+ is registered");
            if (fx == nullptr)
                return;
            auto* channel = findParameter (*fx, "mk2c_channel");
            expect (channel != nullptr, "Mark IIC+ channel selector exists");
            if (channel != nullptr)
                *channel = 1.0f;

            const auto pages = plannedPages (*fx);
            expectEquals ((int) pages.size(), 3, "Mark IIC+ keeps two amp pages and its Power/Cab source page");
            if (pages.size() >= 2)
            {
                const auto pageOne = activeNames (*fx, pages[0]);
                for (auto* controlName : { "Channel", "Pull Bright", "Pull Shift", "Pull Deep",
                                            "Volume 1", "Treble", "Bass", "Middle", "Presence", "Master" })
                    expect (pageOne.contains (controlName), juce::String ("page 1 contains ") + controlName);
                expectEquals ((int) pageOne.size(), 10, "page 1 has the requested core and switch controls");

                const auto pageTwo = activeNames (*fx, pages[1]);
                for (auto* controlName : { "Lead Drive", "Lead Master", "Graphic EQ", "80 Hz",
                                            "240 Hz", "750 Hz", "2200 Hz", "6600 Hz" })
                    expect (pageTwo.contains (controlName), juce::String ("page 2 contains ") + controlName);
                expectEquals ((int) pageTwo.size(), 8, "page 2 keeps Lead controls with all EQ bands");
            }
        }

        beginTest ("Mark IIC+ and Dual Rectifier display controls in declared order");
        {
            auto mark = registry.create ("MarkIICPlusStyleAmplifier");
            expect (mark != nullptr, "Mark IIC+ is registered");
            if (mark != nullptr)
            {
                auto* channel = findParameter (*mark, "mk2c_channel");
                if (channel != nullptr)
                    *channel = 1.0f;
                expect (splitPagesRespectDeclaredOrder (*mark), "Mark IIC+ split pages preserve declared control order");
                const auto pages = plannedPages (*mark);
                if (! pages.empty())
                    expectEquals (activeKnobNames (*mark, pages[0]).joinIntoString (", "),
                                  juce::String ("Volume 1, Treble, Bass, Middle, Presence, Master"),
                                  "Mark IIC+ page 1 knobs follow parameter order");
            }

            auto dualRectifier = registry.create ("DualRectifierStyleAmplifier");
            expect (dualRectifier != nullptr, "Dual Rectifier is registered");
            if (dualRectifier != nullptr)
            {
                expect (splitPagesRespectDeclaredOrder (*dualRectifier),
                        "Dual Rectifier split pages preserve declared control order");
                const auto pages = plannedPages (*dualRectifier);
                if (! pages.empty())
                    expectEquals (activeKnobNames (*dualRectifier, pages[0]).joinIntoString (", "),
                                  juce::String ("Gain, Treble, Mid, Bass, Presence"),
                                  "Dual Rectifier page 1 knobs follow parameter order");
            }
        }

        beginTest ("GE-7 keeps every fader on one page");
        {
            auto fx = registry.create ("GE7StyleEqualizer");
            expect (fx != nullptr, "GE-7 is registered");
            if (fx == nullptr)
                return;
            const auto pages = plannedPages (*fx);
            int faderPages = 0;
            for (const auto& page : pages)
                if (std::any_of (page.begin(), page.end(), [&] (auto* parameter)
                    { return parameter != nullptr && fx->prefersFader (*parameter); }))
                    ++faderPages;
            expectEquals (faderPages, 1, "GE-7 graphic faders share one page");
        }
    }
};

static AmpPanelPageTests ampPanelPageTests;

} // namespace openguitarmultifx
