#pragma once

#include "OAuthLoginDialog.h"
#include "../Tone3000/LoopbackServer.h"
#include "../EffectRegistry.h"
#include "../Tone3000/Tone3000Manager.h"
#include "../Updater/AppUpdater.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace openguitarmultifx
{

/**
    The app's one Settings screen (per AGENT.md's UI/UX Design Philosophy:
    a pedalboard has exactly one settings surface, reached through the "..."
    button, not a scatter of separate dialogs). Right now it holds the
    TONE3000 account section -- paste your publishable client_id, log in --
    the rendering quality (Eco / Normal / High: how much the distortion
    pedals oversample), the built-in cab voicing toggle, the sample rate
    picker and the Software update section (check/download/apply a GitHub
    release -- see Source/Updater/AppUpdater.h). Searching/downloading
    models is NOT here: that stays contextual to whichever block is selected
    (see ParameterPanel and GearRouting.h::gearFilterForProcessorName).

    Shown through OverlayHost, not a juce::DialogWindow -- there's no
    window manager to hand a second window to on the final touchscreen
    target, so every "screen" is a card drawn on top of the single app
    window instead.
*/
class Tone3000Panel : public juce::Component
{
public:
    Tone3000Panel (Tone3000Manager& managerToUse, AppUpdater& updaterToUse);

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
    AppUpdater& updater;
    LoopbackServer loopback; // system-browser login path; idle unless a login is pending

    // Scrollable surface: the sections below no longer fit the card at its
    // OverlayHost-clamped height (the Software update controls pushed it
    // over), so the title/close row stays fixed on the panel and every
    // section lives in `content`, viewed through a Viewport.
    juce::Viewport viewport;
    juce::Component content;
    int layoutSections (juce::Rectangle<int> bounds);

    juce::Label titleLabel { {}, "Settings" };

    juce::Label qualitySectionLabel { {}, "Rendering quality" };
    juce::TextButton ecoButton { "Eco" }, normalButton { "Normal" }, highButton { "High" };
    juce::Label qualityHintLabel;
    void refreshQualityHint();
    juce::Label cabSectionLabel { {}, "Built-in cab voicing" };
    juce::TextButton cabVoicingButton { "On" };
    juce::Label cabHintLabel;
    void refreshCabHint();
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

    juce::Label updateSectionLabel { {}, "Software update" };
    juce::Label currentVersionLabel;
    juce::TextButton checkUpdateButton { "Check for update" };
    juce::TextButton installUpdateButton;
    juce::TextButton restartAppButton { "Restart now" };
    juce::Label updateStatusLabel;
    double updateProgressValue = 0.0;
    juce::ProgressBar updateProgressBar { updateProgressValue };
    std::optional<updatecore::UpdateCandidate> pendingUpdate;
    void doCheckForUpdate();
    void doInstallUpdate();

    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
