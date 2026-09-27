#pragma once

#include "OAuthLoginDialog.h"
#include "../Tone3000/LoopbackServer.h"
#include "../EffectRegistry.h"
#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace openguitarmultifx
{

/**
    The app's one Settings screen (per AGENT.md's UI/UX Design Philosophy:
    a pedalboard has exactly one settings surface, reached through the "..."
    button, not a scatter of separate dialogs). Right now it only holds the
    TONE3000 account section -- paste your publishable client_id, log in --
    plus the rendering quality (Eco / Normal / High: how much the distortion
    pedals oversample) -- the reserved home for whatever else needs a settings
    surface later. Searching/downloading models is NOT here: that stays contextual
    to whichever block is selected (see ParameterPanel and
    GearRouting.h::gearFilterForProcessorName).

    Shown through OverlayHost, not a juce::DialogWindow -- there's no
    window manager to hand a second window to on the final touchscreen
    target, so every "screen" is a card drawn on top of the single app
    window instead.
*/
class Tone3000Panel : public juce::Component
{
public:
    explicit Tone3000Panel (Tone3000Manager& managerToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Wired by whoever hosts this panel to OverlayHost::pushOverlay/popOverlay. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** The user picked a rendering quality: the host applies it (rebuilding the affected pedals) and saves it. */
    std::function<void (EffectRegistry::OversamplingQuality)> onQualityChanged;

    /** The rates the audio device offers and the one it runs at; the host calls this right after building the panel. */
    void showSampleRates (const juce::Array<double>& rates, double current);

    /** The user picked a sample rate: the host restarts the device at it and returns an error message (empty on success). */
    std::function<juce::String (double)> onSampleRateChanged;

private:
    void refreshLoginState();
    void doLogin();
    void doLoginEmbedded (const juce::String& authorizeUrl);
    void finishLogin (const juce::StringPairArray& redirectParams);

    Tone3000Manager& manager;
    LoopbackServer loopback; // system-browser login path; idle unless a login is pending

    juce::Label titleLabel { {}, "Settings" };

    juce::Label qualitySectionLabel { {}, "Rendering quality" };
    juce::TextButton ecoButton { "Eco" }, normalButton { "Normal" }, highButton { "High" };
    juce::Label qualityHintLabel;
    void refreshQualityHint();
    juce::Label rateSectionLabel { {}, "Sample rate" };
    juce::OwnedArray<juce::TextButton> rateButtons;
    juce::Array<double> rateValues;
    double currentRate = 0.0;
    juce::Label rateHintLabel;
    juce::Label tone3000SectionLabel { {}, "TONE3000 account" };

    juce::Label clientIdLabel { {}, "client_id" };
    juce::TextEditor clientIdField;
    juce::TextButton saveClientIdButton { "Save key" };

    juce::TextButton loginButton { "Log in" };
    juce::TextButton logoutButton { "Log out" };
    juce::Label statusLabel;
    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
