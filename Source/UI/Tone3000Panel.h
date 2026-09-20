#pragma once

#include "OAuthLoginDialog.h"
#include "../Tone3000/LoopbackServer.h"
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
    but it's the reserved home for whatever else needs a settings surface
    later. Searching/downloading models is NOT here: that stays contextual
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

private:
    void refreshLoginState();
    void doLogin();
    void doLoginEmbedded (const juce::String& authorizeUrl);
    void finishLogin (const juce::StringPairArray& redirectParams);

    Tone3000Manager& manager;
    LoopbackServer loopback; // system-browser login path; idle unless a login is pending

    juce::Label titleLabel { {}, "Settings" };
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
