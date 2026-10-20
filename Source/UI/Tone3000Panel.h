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
class Tone3000Panel : public juce::Component,
                      private juce::Timer
{
public:
    Tone3000Panel (Tone3000Manager& managerToUse, AppUpdater& updaterToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void timerCallback() override;

    /** Wired by whoever hosts this panel to OverlayHost::pushOverlay/popOverlay. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** The user picked a rendering quality: the host applies it (rebuilding the affected pedals) and saves it. */
    std::function<void (EffectRegistry::OversamplingQuality)> onQualityChanged;

    /** The rates the audio device offers and the one it runs at; the host calls this right after building the panel. */
    void showSampleRates (const juce::Array<double>& rates, double current);

    /** The user picked a sample rate: the host restarts the device at it and returns an error message (empty on success). */
    std::function<juce::String (double)> onSampleRateChanged;

    /** The user tapped "Retry audio device" while no device is open (the NO
        AUDIO DEVICE state's recovery action -- audit #93 item 2): the host
        re-runs the device open and returns its error (empty on success,
        which also re-shows this section and hides the button again). */
    std::function<juce::String()> onRetryDevice;

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
    juce::Label rateSectionLabel { {}, "Sample rate" };
    juce::OwnedArray<juce::TextButton> rateButtons;
    juce::Array<double> rateValues;
    double currentRate = 0.0;
    juce::Label rateHintLabel;
    /** Only visible while the device reports no usable rate -- the state a
        failed device open leaves this section in. */
    juce::TextButton retryDeviceButton { "Retry audio device" };
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
    void doCheckForUpdate();
    void doInstallUpdate();
    // Redraws the whole update section from AppUpdater::UiSnapshot, which
    // outlives this card -- the section survives Settings being closed and
    // reopened mid-update (status text, pending candidate for retry,
    // restart prompt), and the Timer keeps a live card polling while an
    // operation it didn't start is still in flight.
    void applyUpdateUiState();

    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
