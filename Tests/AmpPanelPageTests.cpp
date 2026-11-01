#include "EffectRegistry.h"

#include <juce_core/juce_core.h>

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
            const auto find = [] (EffectProcessor& fx, const char* id) -> juce::AudioParameterFloat*
            {
                for (auto* p : fx.getParameters()->getParameters (true))
                    if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p))
                        if (f->paramID == id)
                            return f;
                return nullptr;
            };
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
                auto* channel = find (*fx, c.channelId);
                auto* control = find (*fx, c.controlId);
                expect (channel != nullptr && control != nullptr, c.controlId);
                if (channel == nullptr || control == nullptr)
                    continue;
                *channel = c.hiddenAt;
                expect (! fx->isParameterActive (*control), juce::String (c.controlId) + " should be hidden");
                *channel = c.shownAt;
                expect (fx->isParameterActive (*control), juce::String (c.controlId) + " should be shown");
            }
        }
    }
};

static AmpPanelPageTests ampPanelPageTests;

} // namespace openguitarmultifx
