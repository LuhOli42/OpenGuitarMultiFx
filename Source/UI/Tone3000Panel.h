#pragma once

#include "MidiSettingsPages.h"
#include "OverlayChrome.h"
#include "OverlayHost.h"
#include "QrCodeView.h"
#include "Theme.h"
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
    button, not a scatter of separate dialogs). A side navigation splits it
    into Appearance (theme), Audio (interface/sample rate/buffer), Rendering
    quality, TONE3000 account, MIDI (the table of learned assignments), Expression
    pedals, Software update and About.
    Searching/downloading models is NOT here: that stays contextual to
    whichever block is selected (see ParameterPanel and
    GearRouting.h::gearFilterForProcessorName).

    Shown through OverlayHost, not a juce::DialogWindow -- there's no
    window manager to hand a second window to on the final touchscreen
    target, so every "screen" is a card drawn on top of the single app
    window instead.
*/
class Tone3000Panel : public juce::Component,
                      public PersistentCard,
                      private juce::Timer
{
public:
    Tone3000Panel (Tone3000Manager& managerToUse, AppUpdater& updaterToUse);
    ~Tone3000Panel() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void timerCallback() override;

    /** Built once and shown again (OverlayHost::pushPersistent): back to how a fresh panel looks -- first section, the
        current theme/quality/login/update state, scrolled to the top. The host also re-pushes the device state (rates...). */
    void resetForOpen();
    /** Taken off the screen: what the destructor used to do on closing. */
    void cardHidden() override;

    /** Wired by whoever hosts this panel to OverlayHost::pushOverlay/popOverlay. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** The user picked a rendering quality: the host applies it (rebuilding the affected pedals) and saves it. */
    std::function<void (EffectRegistry::OversamplingQuality)> onQualityChanged;

    /** The user picked a visual theme: the host applies it to the whole UI and saves it. */
    std::function<void (theme::Id)> onThemeChanged;

    /** The rates the audio device offers and the one it runs at; the host calls this right after building the panel. */
    void showSampleRates (const juce::Array<double>& rates, double current);

    /** The user picked a sample rate: the host restarts the device at it and returns an error message (empty on success). */
    std::function<juce::String (double)> onSampleRateChanged;

    /** The buffer sizes the device offers and the one it runs at, plus the rate (for the ms readout); the host calls this with showSampleRates. */
    void showBufferSizes (const juce::Array<int>& sizes, int current, double rate);

    /** The interface line at the top of the audio section (AudioEngine::describeInterface). */
    void showInterface (const juce::String& description) { interfaceLabel.setText (description, juce::dontSendNotification); }

    /** The user picked a buffer size: the host restarts the device with it and returns an error message (empty on success). */
    std::function<juce::String (int)> onBufferSizeChanged;
    /** Performance mode toggle (Audio page): MainComponent applies it to the engine, saves it and rebuilds the graph. */
    std::function<void (bool)> onPerformanceModeChanged;
    std::function<void (bool)> onSendModeChanged;
    /** Settings > Connections: open the Wi-Fi / Bluetooth cards (the top bar only shows their connected state). */
    std::function<void()> onOpenWifi, onOpenBluetooth;
    /** Settings > Audio > System monitor: show / hide the live panel (core clocks and busy-%, GPU busy-%, temperature). */
    std::function<void (bool)> onSystemMonitorChanged;
    void setSystemMonitor (bool shown);
    /** Settings > Audio > CPU: cpuAuto, cpuFull, cpuMax, or 416 / 780 (slow cores locked at that many MHz). */
    static constexpr int cpuAuto = 0, cpuFull = 1, cpuMax = 2;
    std::function<void (int)> onSlowClockChanged;
    void setSlowClock (int mhz);
    void setSendMode (bool enabled);
    void setPerformanceMode (bool enabled);

    /** The user tapped "Retry audio device" while no device is open (the NO
        AUDIO DEVICE state's recovery action -- audit #93 item 2): the host
        re-runs the device open and returns its error (empty on success,
        which also re-shows this section and hides the button again). */
    std::function<juce::String()> onRetryDevice;

    /** MIDI page: every learned assignment (polled while the page is open) and removing one of them. */
    std::function<std::vector<MidiMappingRow>()> getMidiMappings;
    std::function<void (const MidiMappingRow&)> onRemoveMidiMapping;

    /** Expression page: the host owns the pedals (exppedal::Settings) and the incoming CCs; the page polls the status
        and sends edits. Learn and Calibrate toggle (a second tap cancels / finishes). */
    std::function<ExpressionPedalStatus (int)> getExpressionPedalStatus;
    std::function<void (int)> onExpressionLearn, onExpressionClear, onExpressionCalibrate;
    /** "Link knobs": the host closes Settings and lets you tap the knobs on screen. */
    std::function<void (int)> onExpressionLinkKnobs;
    std::function<void (int, const exppedal::Pedal&)> onExpressionEdited;
    /** Settings is closing: the host drops a pending pedal learn / calibration nobody can see any more. */
    std::function<void()> onClosing;

private:
    void refreshLoginState();

    Tone3000Manager& manager;
    AppUpdater& updater;

    // Scrollable surface: the sections below no longer fit the card at its
    // OverlayHost-clamped height (the Software update controls pushed it
    // over), so the title/close row stays fixed on the panel and every
    // section lives in `content`, viewed through a Viewport.
    juce::Viewport viewport;
    juce::Component content;

    enum class Section { appearance, audio, connections, quality, tone3000, midi, expression, updates, about };
    Section currentSection = Section::appearance;
    juce::Component appearancePage, audioPage, connectionsPage, qualityPage, tone3000Page, midiPage, expressionPage, updatesPage, aboutPage;
    juce::OwnedArray<juce::Component> navButtons;
    juce::OwnedArray<juce::Component> themeCards;
    juce::Rectangle<int> navRailBounds;

    void layoutContent();
    int layoutActivePage (juce::Rectangle<int> bounds);
    void selectSection (int index);
    void refreshNavButtons();
    void refreshThemeCards();
    void applyLabelColours();

    juce::Label titleLabel { {}, "Settings" };

    juce::Label appearanceSectionLabel { {}, "Appearance" };
    juce::Label themeHintLabel;
    void refreshThemeHint();

    juce::Label qualitySectionLabel { {}, "Rendering quality" };
    juce::TextButton ecoButton { "Eco" }, normalButton { "Normal" }, highButton { "High" };
    juce::Label qualityHintLabel;
    void refreshQualityHint();
    juce::Label interfaceSectionLabel { {}, "Audio interface" };
    juce::Label interfaceLabel;
    juce::Label rateSectionLabel { {}, "Sample rate" };
    juce::OwnedArray<juce::TextButton> rateButtons;
    juce::Array<double> rateValues;
    double currentRate = 0.0;
    juce::Label rateHintLabel;
    /** Only visible while the device reports no usable rate -- the state a
        failed device open leaves this section in. */
    juce::TextButton retryDeviceButton { "Retry audio device" };
    juce::Label bufferSectionLabel { {}, "Buffer (latency)" };
    juce::Label perfSectionLabel { {}, "Performance mode" };
    juce::Label perfHintLabel;
    juce::TextButton perfButton { "Off" };
    juce::Label sendSectionLabel { {}, "Parallel delay/reverb send" };
    juce::Label sendHintLabel;
    juce::TextButton sendButton { "Off" };
    juce::Label connectionsSectionLabel { {}, "Wi-Fi and Bluetooth" };
    juce::Label connectionsHintLabel;
    juce::TextButton wifiOpenButton { "Wi-Fi" }, bluetoothOpenButton { "Bluetooth" };
    juce::Label monitorSectionLabel { {}, "System monitor" };
    juce::Label monitorHintLabel;
    juce::TextButton monitorButton { "Off" };
    juce::Label slowClockSectionLabel { {}, "CPU" };
    juce::Label slowClockHintLabel;
    juce::TextButton slowAutoButton { "Auto" }, slowFullButton { "Full" }, slowMaxButton { "Max" },
                     slow416Button { "416 MHz" }, slow780Button { "780 MHz" };
    juce::OwnedArray<juce::TextButton> bufferButtons;
    juce::Array<int> bufferValues;
    int currentBuffer = 0;
    double bufferRate = 48000.0;
    juce::Label bufferHintLabel;
    juce::Label tone3000SectionLabel { {}, "TONE3000 account" };

    juce::Label clientIdLabel { {}, "client_id" };
    juce::TextEditor clientIdField;
    juce::TextButton saveClientIdButton { "Save key" };

    juce::TextButton loginButton { "Log in" };
    juce::TextButton logoutButton { "Log out" };
    juce::Label statusLabel;

    // The only login (no web page inside the app): the panel shows a QR code of the activation link plus the short code,
    // the user signs in on a phone, and the panel polls until TONE3000 hands over the tokens (Tone3000Manager::beginDeviceLogin).
    // On a desktop "Open in this computer's browser" launches the same link in the system browser instead.
    void doDeviceLogin();
    void pollDeviceLogin();
    void stopDeviceLogin();
    juce::Label deviceCodeLabel;
    QrCodeView deviceQr;
    juce::TextButton openHereButton { "Open in this computer's browser" };
    Tone3000Manager::DeviceLogin deviceLogin;
    double deviceLoginStartMs = 0.0;
    int deviceLoginIntervalSec = 5;
    bool devicePollInFlight = false;
    struct DeviceLoginTimer : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } deviceLoginPoll;

    juce::Label updateSectionLabel { {}, "Software update" };
    juce::Label currentVersionLabel;
    juce::TextButton checkUpdateButton { "Check for update" };
    juce::TextButton installUpdateButton;
    juce::TextButton restartAppButton { "Restart now" };
    juce::Label updateStatusLabel;
    double updateProgressValue = 0.0;
    juce::ProgressBar updateProgressBar { updateProgressValue };

    juce::Label midiTableLabel;
    MidiMappingTable midiTable;
    juce::Label expressionIntroLabel;
    juce::Label expressionHintLabel;
    juce::OwnedArray<ExpressionPedalEditor> expressionEditors;

    // Separate from the panel's own Timer, which AppUpdater's UI starts and stops: this one runs only while the MIDI or
    // Expression page is on screen, refreshing the table and the live pedal position.
    struct PollTimer : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } midiPoll;
    void pollMidiPages();
    juce::Label aboutIntroLabel;
    juce::Label aboutDetailsLabel;

    void doCheckForUpdate();
    void doInstallUpdate();
    // Redraws the whole update section from AppUpdater::UiSnapshot, which
    // outlives this card -- the section survives Settings being closed and
    // reopened mid-update (status text, pending candidate for retry,
    // restart prompt), and the Timer keeps a live card polling while an
    // operation it didn't start is still in flight.
    void applyUpdateUiState();

    overlaychrome::CloseButton closeButton;
};

} // namespace openguitarmultifx
