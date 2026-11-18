#include "Tone3000Panel.h"
#include "OverlayChrome.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    class ThemeCard : public juce::Component
    {
    public:
        ThemeCard (theme::Id themeId, juce::String descriptionText)
            : id (themeId), description (std::move (descriptionText))
        {
        }

        void paint (juce::Graphics& g) override
        {
            const auto& current = theme::current();
            const auto& target = theme::get (id);
            auto bounds = getLocalBounds().toFloat();

            g.setColour (current.surface);
            g.fillRoundedRectangle (bounds, current.radius);
            g.setColour (selected ? OpenGuitarMultiFxLookAndFeel::getAppAccentColour()
                                  : current.lineStrong);
            g.drawRoundedRectangle (bounds.reduced (0.5f), current.radius, selected ? 2.0f : 1.0f);

            auto preview = bounds.reduced (12.0f, 12.0f);
            preview.removeFromBottom (preview.getHeight() * 0.52f);
            g.setColour (target.background);
            g.fillRoundedRectangle (preview, target.smallRadius);

            const juce::Colour colours[] = { EffectRegistry::categoryColour (EffectRegistry::Category::amps),
                                             EffectRegistry::categoryColour (EffectRegistry::Category::overdriveBoost),
                                             EffectRegistry::categoryColour (EffectRegistry::Category::delay) };
            auto blocks = preview.reduced (14.0f, 18.0f);
            const int blockW = (int) ((blocks.getWidth() - 20.0f) / 3.0f);
            for (int i = 0; i < 3; ++i)
            {
                auto block = juce::Rectangle<float> ((float) i * (blockW + 10), blocks.getY(), (float) blockW, blocks.getHeight());
                switch (target.blockStyle)
                {
                    case theme::BlockStyle::glow:
                        g.setColour (target.raised);
                        g.fillRoundedRectangle (block, target.smallRadius);
                        g.setColour (colours[i]);
                        g.fillEllipse (block.getCentreX() - 12.0f, block.getCentreY() - 12.0f, 24.0f, 24.0f);
                        break;
                    case theme::BlockStyle::topStripe:
                        g.setColour (target.raised);
                        g.fillRoundedRectangle (block, target.smallRadius);
                        g.setColour (colours[i]);
                        g.fillRoundedRectangle (block.removeFromTop (6.0f), target.smallRadius);
                        break;
                    case theme::BlockStyle::filled:
                        g.setColour (colours[i]);
                        g.fillRoundedRectangle (block, target.smallRadius);
                        break;
                }
            }

            auto textArea = bounds.reduced (14.0f, 12.0f);
            textArea.removeFromTop (preview.getBottom() - bounds.getY() + 10.0f);
            g.setColour (current.text);
            g.setFont (juce::FontOptions (17.0f).withStyle ("Bold"));
            g.drawText (theme::get (id).displayName, textArea.removeFromTop (24.0f), juce::Justification::centredLeft);
            g.setColour (current.textMuted);
            g.setFont (juce::FontOptions (12.5f));
            g.drawFittedText (description, textArea.reduced (0.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, 3);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (onTap)
                onTap();
        }

        void lookAndFeelChanged() override { repaint(); }

        theme::Id id;
        bool selected = false;
        std::function<void()> onTap;
        juce::String description;
    };

}

Tone3000Panel::Tone3000Panel (Tone3000Manager& managerToUse, AppUpdater& updaterToUse)
    : manager (managerToUse),
      updater (updaterToUse)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    // The page in view is cached as one texture: a scroll slides it instead of re-painting every label, button and toggle on each
    // frame (the lists of effects/presets already do this per row; this page was the slow scroll). Hidden pages are not drawn.
    content.setBufferedToImage (true);
    touch::enableDragScroll (viewport);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);

    const char* navTitles[] = { "Appearance", "Audio", "Connections", "Quality", "TONE3000", "MIDI", "Expression", "Updates", "About" };
    for (int i = 0; i < 9; ++i)
    {
        auto* b = new juce::TextButton (navTitles[i]);
        navButtons.add (b);
        addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4701);
        b->setToggleState (i == 0, juce::dontSendNotification);
        b->onClick = [this, i] { selectSection (i); };
    }

    juce::Component* pages[] = { &appearancePage, &audioPage, &connectionsPage, &qualityPage, &tone3000Page, &midiPage, &expressionPage, &updatesPage, &aboutPage };
    for (auto* page : pages)
    {
        content.addAndMakeVisible (*page);
        page->setVisible (page == &appearancePage);
    }

    appearancePage.addAndMakeVisible (appearanceSectionLabel);
    appearanceSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    appearanceSectionLabel.setText ("Theme", juce::dontSendNotification);

    const struct { theme::Id id; const char* description; } cards[] = {
        { theme::Id::stageGlass, "Blue-black glass cards with category colours glowing. The default live look." },
        { theme::Id::console, "Flat graphite surfaces, category colour as a stripe, tight corners." },
        { theme::Id::boldBlocks, "Blocks filled with the category colour for maximum distance readability." }
    };
    for (const auto& card : cards)
    {
        auto* c = new ThemeCard (card.id, card.description);
        themeCards.add (c);
        appearancePage.addAndMakeVisible (c);
        c->selected = theme::currentId() == card.id;
        c->onTap = [this, id = card.id]
        {
            if (theme::currentId() == id)
                return;
            if (onThemeChanged)
                onThemeChanged (id);
            refreshThemeHint();
            refreshThemeCards();
        };
    }

    appearancePage.addAndMakeVisible (themeHintLabel);
    themeHintLabel.setFont (juce::Font (14.0f));
    refreshThemeHint();

    qualityPage.addAndMakeVisible (qualitySectionLabel);
    qualitySectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    qualitySectionLabel.setText ("Rendering quality", juce::dontSendNotification);

    using Q = EffectRegistry::OversamplingQuality;
    const struct { juce::TextButton* button; Q quality; } choices[] = { { &ecoButton, Q::eco }, { &normalButton, Q::balanced }, { &highButton, Q::high } };
    for (const auto& c : choices)
    {
        qualityPage.addAndMakeVisible (*c.button);
        c.button->setClickingTogglesState (true);
        c.button->setRadioGroupId (4711);
        c.button->setToggleState (EffectRegistry::getOversamplingQuality() == c.quality, juce::dontSendNotification);
        c.button->onClick = [this, q = c.quality]
        {
            if (EffectRegistry::getOversamplingQuality() == q)
                return;
            if (onQualityChanged)
                onQualityChanged (q);
            refreshQualityHint();
        };
    }

    qualityPage.addAndMakeVisible (qualityHintLabel);
    qualityHintLabel.setFont (juce::Font (14.0f));
    refreshQualityHint();

    audioPage.addAndMakeVisible (interfaceSectionLabel);
    interfaceSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    interfaceSectionLabel.setText ("Audio interface", juce::dontSendNotification);
    audioPage.addAndMakeVisible (interfaceLabel);
    interfaceLabel.setFont (juce::Font (16.0f));

    audioPage.addAndMakeVisible (rateSectionLabel);
    rateSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    rateSectionLabel.setText ("Sample rate", juce::dontSendNotification);
    audioPage.addChildComponent (retryDeviceButton);
    retryDeviceButton.onClick = [this]
    {
        if (! onRetryDevice)
            return;
        const auto err = onRetryDevice();
        if (err.isNotEmpty())
            rateHintLabel.setText ("Could not open the audio device: " + err, juce::dontSendNotification);
    };

    audioPage.addAndMakeVisible (rateHintLabel);
    rateHintLabel.setFont (juce::Font (14.0f));

    audioPage.addAndMakeVisible (bufferSectionLabel);
    bufferSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    bufferSectionLabel.setText ("Buffer (latency)", juce::dontSendNotification);
    audioPage.addAndMakeVisible (bufferHintLabel);
    bufferHintLabel.setFont (juce::Font (14.0f));

    audioPage.addAndMakeVisible (perfSectionLabel);
    perfSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    audioPage.addAndMakeVisible (perfButton);
    perfButton.setClickingTogglesState (true);
    perfButton.onClick = [this]
    {
        perfButton.setButtonText (perfButton.getToggleState() ? "On" : "Off");
        if (onPerformanceModeChanged)
            onPerformanceModeChanged (perfButton.getToggleState());
    };
    audioPage.addAndMakeVisible (perfHintLabel);
    perfHintLabel.setFont (juce::Font (14.0f));
    perfHintLabel.setText ("Splits a single-row chain across the two fast cores so heavy amps stop dropping out. "
                           "Adds two buffers of latency (about 5 ms at 128 samples). Takes effect right away.",
                           juce::dontSendNotification);

    audioPage.addAndMakeVisible (sendSectionLabel);
    sendSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    audioPage.addAndMakeVisible (sendButton);
    sendButton.setClickingTogglesState (true);
    sendButton.onClick = [this]
    {
        sendButton.setButtonText (sendButton.getToggleState() ? "On" : "Off");
        if (onSendModeChanged)
            onSendModeChanged (sendButton.getToggleState());
    };
    audioPage.addAndMakeVisible (sendHintLabel);
    sendHintLabel.setFont (juce::Font (14.0f));
    sendHintLabel.setText ("Runs the delays and reverbs at the end of a single-row chain on the second fast core, as a send: "
                           "the dry signal stays instant and only the echoes/tail arrive about 5 ms late. Ignored while Performance mode is on.",
                           juce::dontSendNotification);

    audioPage.addAndMakeVisible (slowClockSectionLabel);
    slowClockSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    {
        const struct { juce::TextButton* button; int mhz; } slowChoices[] = { { &slowAutoButton, 0 }, { &slow416Button, 416 }, { &slow780Button, 780 } };
        for (const auto& c : slowChoices)
        {
            audioPage.addAndMakeVisible (*c.button);
            c.button->setClickingTogglesState (true);
            c.button->setRadioGroupId (4702);
            c.button->onClick = [this, mhz = c.mhz] { if (onSlowClockChanged) onSlowClockChanged (mhz); };
        }
    }
    audioPage.addAndMakeVisible (slowClockHintLabel);
    slowClockHintLabel.setFont (juce::Font (14.0f));
    slowClockHintLabel.setText ("Locks the six slow cores (the interface and the drawing) at one clock. The processor only has fixed "
                                "steps: 416 is the lowest, 780 the next. Lower = cooler, but the interface needs about 600 MHz of one core "
                                "while it animates, so 416 can stutter. Auto = follows the load (ceiling 1 GHz).",
                                juce::dontSendNotification);

    audioPage.addAndMakeVisible (monitorSectionLabel);
    monitorSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    audioPage.addAndMakeVisible (monitorButton);
    monitorButton.setClickingTogglesState (true);
    monitorButton.onClick = [this]
    {
        monitorButton.setButtonText (monitorButton.getToggleState() ? "On" : "Off");
        if (onSystemMonitorChanged)
            onSystemMonitorChanged (monitorButton.getToggleState());
    };
    audioPage.addAndMakeVisible (monitorHintLabel);
    monitorHintLabel.setFont (juce::Font (14.0f));
    monitorHintLabel.setText ("A live panel over the screen: clock and busy-% of the slow cores (interface) and the fast cores "
                              "(audio), the GPU's busy-%, and the temperature. Tap the TEMP pill in the top bar to hide it too.",
                              juce::dontSendNotification);

    connectionsPage.addAndMakeVisible (connectionsSectionLabel);
    connectionsSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    connectionsPage.addAndMakeVisible (wifiOpenButton);
    wifiOpenButton.onClick = [this] { if (onOpenWifi) onOpenWifi(); };
    connectionsPage.addAndMakeVisible (bluetoothOpenButton);
    bluetoothOpenButton.onClick = [this] { if (onOpenBluetooth) onOpenBluetooth(); };
    connectionsPage.addAndMakeVisible (connectionsHintLabel);
    connectionsHintLabel.setFont (juce::Font (14.0f));
    connectionsHintLabel.setText ("Pick a network or pair a device (a MIDI controller, headphones). The top bar only lights up the "
                                  "Wi-Fi and Bluetooth icons while something is connected.",
                                  juce::dontSendNotification);

    tone3000Page.addAndMakeVisible (tone3000SectionLabel);
    tone3000SectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    tone3000SectionLabel.setText ("TONE3000 account", juce::dontSendNotification);

    tone3000Page.addAndMakeVisible (clientIdLabel);

    tone3000Page.addAndMakeVisible (clientIdField);
    clientIdField.setTextToShowWhenEmpty ("t3k_pub_...", theme::current().textDim);
    clientIdField.setText (manager.getClientId(), juce::dontSendNotification);

    tone3000Page.addAndMakeVisible (saveClientIdButton);
    saveClientIdButton.onClick = [this]
    {
        manager.setClientId (clientIdField.getText());
        refreshLoginState();
    };

    tone3000Page.addAndMakeVisible (loginButton);
    loginButton.onClick = [this] { doDeviceLogin(); };

    tone3000Page.addAndMakeVisible (logoutButton);
    logoutButton.onClick = [this] { manager.logOut(); refreshLoginState(); };

    tone3000Page.addAndMakeVisible (statusLabel);
    tone3000Page.addChildComponent (deviceCodeLabel);
    deviceCodeLabel.setFont (juce::Font (juce::FontOptions (38.0f, juce::Font::bold)));
    deviceCodeLabel.setJustificationType (juce::Justification::centredLeft);
    deviceCodeLabel.setColour (juce::Label::textColourId, theme::current().text);
    tone3000Page.addChildComponent (deviceQr);
    tone3000Page.addChildComponent (openHereButton);
    openHereButton.onClick = [this] { juce::URL (deviceLogin.verificationUriComplete).launchInDefaultBrowser(); };
    deviceLoginPoll.tick = [this] { pollDeviceLogin(); };

    midiPage.addAndMakeVisible (midiTableLabel);
    midiTableLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    midiTableLabel.setText ("MIDI assignments", juce::dontSendNotification);
    midiPage.addAndMakeVisible (midiTable);
    midiTable.onRemove = [this] (const MidiMappingRow& row)
    {
        if (onRemoveMidiMapping)
            onRemoveMidiMapping (row);
        // The tapped Remove button is one of the table's own rows: rebuild after its click handler has returned.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<Tone3000Panel> (this)]
        {
            if (safeThis != nullptr)
                safeThis->pollMidiPages();
        });
    };

    expressionPage.addAndMakeVisible (expressionIntroLabel);
    expressionIntroLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    expressionIntroLabel.setText ("Expression pedals", juce::dontSendNotification);
    expressionPage.addAndMakeVisible (expressionHintLabel);
    expressionHintLabel.setFont (juce::Font (14.0f));
    expressionHintLabel.setText ("A pedal on your MIDI controller's EXP input. Learn its CC, calibrate heel and toe so it reaches "
                                 "both ends, pick the curve. Auto on/off switches the blocks it drives on when you move it "
                                 "and off after it rests at the heel (wah style). Applies to every preset.",
                                 juce::dontSendNotification);
    for (int i = 0; i < exppedal::numPedals; ++i)
    {
        auto* editor = expressionEditors.add (new ExpressionPedalEditor (i));
        expressionPage.addAndMakeVisible (editor);
        editor->onLearn = [this, i] { if (onExpressionLearn) onExpressionLearn (i); pollMidiPages(); };
        editor->onClear = [this, i] { if (onExpressionClear) onExpressionClear (i); pollMidiPages(); };
        editor->onCalibrate = [this, i] { if (onExpressionCalibrate) onExpressionCalibrate (i); pollMidiPages(); };
        editor->onLinkKnobs = [this, i] { if (onExpressionLinkKnobs) onExpressionLinkKnobs (i); };
        editor->onEdited = [this, i] (const exppedal::Pedal& p) { if (onExpressionEdited) onExpressionEdited (i, p); pollMidiPages(); };
    }
    midiPoll.tick = [this] { pollMidiPages(); };

    updatesPage.addAndMakeVisible (updateSectionLabel);
    updateSectionLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    updateSectionLabel.setText ("Software update", juce::dontSendNotification);

    updatesPage.addAndMakeVisible (currentVersionLabel);
    currentVersionLabel.setText ("installed: " + updater.getCurrentVersion(), juce::dontSendNotification);

    updatesPage.addAndMakeVisible (checkUpdateButton);
    checkUpdateButton.onClick = [this] { doCheckForUpdate(); };

    updatesPage.addChildComponent (installUpdateButton);
    installUpdateButton.onClick = [this] { doInstallUpdate(); };

    updatesPage.addChildComponent (restartAppButton);
    restartAppButton.onClick = [this] { updater.restartNow(); applyUpdateUiState(); };

    updatesPage.addAndMakeVisible (updateStatusLabel);
    updateStatusLabel.setFont (juce::Font (14.0f));
    updateStatusLabel.setText (updater.isPackagedInstall()
        ? "Updates install through the system updater; the app restarts on the new version."
        : "Updates are downloaded, checksum-verified and staged next to the binary; they apply on restart.",
        juce::dontSendNotification);

    updatesPage.addChildComponent (updateProgressBar);
    updateProgressBar.setPercentageDisplay (false);

    aboutPage.addAndMakeVisible (aboutIntroLabel);
    aboutIntroLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    aboutIntroLabel.setText ("About", juce::dontSendNotification);
    aboutPage.addAndMakeVisible (aboutDetailsLabel);
    aboutDetailsLabel.setFont (juce::Font (15.0f));
    aboutDetailsLabel.setText ("OpenGuitarMultiFx - one settings surface for theme, audio, quality, TONE3000, MIDI and updates.\n"
                               "The selected theme only changes the look; sound and presets never depend on it.",
                               juce::dontSendNotification);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    applyLabelColours();
    refreshLoginState();
    applyUpdateUiState();
    setSize (1180, 780);
}

void Tone3000Panel::showSampleRates (const juce::Array<double>& rates, double current)
{
    rateButtons.clear();
    rateValues.clear();
    currentRate = current;

    for (double r : rates)
        if (r == 44100.0 || r == 48000.0 || r == 88200.0 || r == 96000.0)
            rateValues.add (r);

    for (double r : rateValues)
    {
        auto* b = rateButtons.add (new juce::TextButton (juce::String (r / 1000.0, r == 44100.0 || r == 88200.0 ? 1 : 0) + " kHz"));
        audioPage.addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4712);
        b->setToggleState (std::abs (r - current) < 0.5, juce::dontSendNotification);
        b->onClick = [this, r, b]
        {
            if (! onSampleRateChanged)
                return;
            const auto err = onSampleRateChanged (r);
            rateHintLabel.setText (err.isEmpty() ? "Audio restarted at " + juce::String (r / 1000.0, 1) + " kHz. The pedals are re-prepared for it; "
                                                       "higher rates cost proportionally more CPU."
                                                 : "Could not switch: " + err, juce::dontSendNotification);
            if (err.isEmpty())
                currentRate = r;
            else
                for (int i = 0; i < rateButtons.size(); ++i)
                    rateButtons[i]->setToggleState (std::abs (rateValues[i] - currentRate) < 0.5, juce::dontSendNotification);
        };
    }

    rateHintLabel.setText (rateValues.isEmpty() ? "No usable sample rate reported by the audio device." : "The rate the audio device runs at (48 kHz is the default). Changing it restarts audio.",
                           juce::dontSendNotification);
    retryDeviceButton.setVisible (rateValues.isEmpty());
    resized();
}

void Tone3000Panel::setSlowClock (int mhz)
{
    slowAutoButton.setToggleState (mhz != 416 && mhz != 780, juce::dontSendNotification);
    slow416Button.setToggleState (mhz == 416, juce::dontSendNotification);
    slow780Button.setToggleState (mhz == 780, juce::dontSendNotification);
}

void Tone3000Panel::setSystemMonitor (bool shown)
{
    monitorButton.setToggleState (shown, juce::dontSendNotification);
    monitorButton.setButtonText (shown ? "On" : "Off");
}

void Tone3000Panel::setSendMode (bool enabled)
{
    sendButton.setToggleState (enabled, juce::dontSendNotification);
    sendButton.setButtonText (enabled ? "On" : "Off");
}

void Tone3000Panel::setPerformanceMode (bool enabled)
{
    perfButton.setToggleState (enabled, juce::dontSendNotification);
    perfButton.setButtonText (enabled ? "On" : "Off");
}

void Tone3000Panel::showBufferSizes (const juce::Array<int>& sizes, int current, double rate)
{
    bufferButtons.clear();
    bufferValues.clear();
    currentBuffer = current;
    bufferRate = rate > 0.0 ? rate : 48000.0;

    for (int n : { 32, 64, 128, 256, 512 })
        if (sizes.contains (n))
            bufferValues.add (n);

    const auto ms = [this] (int n) { return juce::String (1000.0 * n / bufferRate, 1) + " ms"; };

    for (int n : bufferValues)
    {
        auto* b = bufferButtons.add (new juce::TextButton (juce::String (n)));
        audioPage.addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4713);
        b->setToggleState (n == current, juce::dontSendNotification);
        b->onClick = [this, n, ms]
        {
            if (! onBufferSizeChanged)
                return;
            const auto err = onBufferSizeChanged (n);
            if (err.isEmpty())
                currentBuffer = n;
            else
                for (int i = 0; i < bufferButtons.size(); ++i)
                    bufferButtons[i]->setToggleState (bufferValues[i] == currentBuffer, juce::dontSendNotification);
            bufferHintLabel.setText (err.isEmpty() ? "Audio restarted with " + juce::String (n) + " samples (" + ms (n) + " per block). "
                                                       "If it crackles, go one step up."
                                                 : "Could not switch: " + err, juce::dontSendNotification);
        };
    }

    bufferHintLabel.setText (bufferValues.isEmpty() ? "No buffer size reported by the audio device."
                                                    : "Samples per block, now " + juce::String (current) + " (" + ms (current) + "). Smaller = "
                                                      "tighter attack, more CPU per block; go up if it crackles.",
                             juce::dontSendNotification);
    resized();
}

void Tone3000Panel::refreshThemeHint()
{
    switch (theme::currentId())
    {
        case theme::Id::stageGlass:
            themeHintLabel.setText ("Stage Glass: blue-black glass cards, category colours glow. Only the look changes, never the sound.", juce::dontSendNotification);
            break;
        case theme::Id::console:
            themeHintLabel.setText ("Console: flat graphite, category colour as a stripe, small corners. Only the look changes, never the sound.", juce::dontSendNotification);
            break;
        case theme::Id::boldBlocks:
            themeHintLabel.setText ("Bold Blocks: blocks filled with the category colour, readable from a distance. Only the look changes, never the sound.", juce::dontSendNotification);
            break;
    }
}

void Tone3000Panel::refreshThemeCards()
{
    for (auto* component : themeCards)
        if (auto* card = dynamic_cast<ThemeCard*> (component))
        {
            card->selected = theme::currentId() == card->id;
            card->repaint();
        }
}

void Tone3000Panel::refreshNavButtons()
{
    int index = 0;
    for (auto* component : navButtons)
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (component))
        {
            const bool selected = index == (int) currentSection;
            button->setToggleState (selected, juce::dontSendNotification);
        }
        ++index;
    }
}

void Tone3000Panel::selectSection (int index)
{
    Section next;
    switch (index)
    {
        case 0: next = Section::appearance; break;
        case 1: next = Section::audio;       break;
        case 2: next = Section::connections; break;
        case 3: next = Section::quality;     break;
        case 4: next = Section::tone3000;    break;
        case 5: next = Section::midi;        break;
        case 6: next = Section::expression;  break;
        case 7: next = Section::updates;     break;
        case 8: next = Section::about;       break;
        default: return;
    }
    if (currentSection == next)
        return;

    currentSection = next;
    refreshNavButtons();
    pollMidiPages();
    layoutContent();
    viewport.setViewPosition (0, 0);

    if (currentSection == Section::midi || currentSection == Section::expression)
        midiPoll.startTimer (100);
    else
        midiPoll.stopTimer();
}

void Tone3000Panel::resetForOpen()
{
    selectSection (0);
    refreshThemeCards();
    refreshThemeHint();
    using Q = EffectRegistry::OversamplingQuality;
    const auto quality = EffectRegistry::getOversamplingQuality();
    ecoButton.setToggleState (quality == Q::eco, juce::dontSendNotification);
    normalButton.setToggleState (quality == Q::balanced, juce::dontSendNotification);
    highButton.setToggleState (quality == Q::high, juce::dontSendNotification);
    refreshQualityHint();
    refreshLoginState();
    applyUpdateUiState();
    viewport.setViewPosition (0, 0);
}

void Tone3000Panel::cardHidden()
{
    midiPoll.stopTimer();
    stopDeviceLogin();
    if (onClosing)
        onClosing();
}

Tone3000Panel::~Tone3000Panel()
{
    midiPoll.stopTimer();
    if (onClosing)
        onClosing();
}

void Tone3000Panel::pollMidiPages()
{
    if (currentSection == Section::midi && getMidiMappings)
    {
        const int before = midiTable.getIdealHeight();
        midiTable.setRows (getMidiMappings());
        if (midiTable.getIdealHeight() != before)
            layoutContent();
    }
    if (currentSection == Section::expression && getExpressionPedalStatus)
        for (int i = 0; i < expressionEditors.size(); ++i)
            expressionEditors[i]->setStatus (getExpressionPedalStatus (i));
}

void Tone3000Panel::applyLabelColours()
{
    const auto& t = theme::current();
    const auto muted = [this, &t] (juce::Label& label) { label.setColour (juce::Label::textColourId, t.textMuted); };

    for (juce::Label* label : { &themeHintLabel, &qualityHintLabel, &rateHintLabel, &bufferHintLabel, &perfHintLabel, &sendHintLabel, &monitorHintLabel, &slowClockHintLabel, &connectionsHintLabel, &statusLabel,
                                &updateStatusLabel, &expressionHintLabel, &aboutDetailsLabel, &interfaceLabel,
                                &currentVersionLabel, &clientIdLabel })
        muted (*label);
}

void Tone3000Panel::lookAndFeelChanged()
{
    applyLabelColours();
    refreshThemeCards();
    repaint();
}

void Tone3000Panel::refreshQualityHint()
{
    switch (EffectRegistry::getOversamplingQuality())
    {
        case EffectRegistry::OversamplingQuality::eco:
            qualityHintLabel.setText ("Eco: lightest on the CPU. The distortion pedals do not oversample, so the hardest "
                                      "clippers (DS-1, HM-2, BD-2) can sound a little fizzy.", juce::dontSendNotification);
            break;
        case EffectRegistry::OversamplingQuality::balanced:
            qualityHintLabel.setText ("Normal: the DS-1, BD-2 and HM-2 run at 2x oversampling (about twice the CPU of Eco "
                                      "on those pedals) for a cleaner top end.", juce::dontSendNotification);
            break;
        case EffectRegistry::OversamplingQuality::high:
            qualityHintLabel.setText ("High: the most oversampling the measurements say is worth it (4x on the DS-1, "
                                      "BD-2, HM-2; 2x on the Tube Screamers and OD-1). Heaviest on the CPU.", juce::dontSendNotification);
            break;
    }
}

void Tone3000Panel::refreshLoginState()
{
    const bool loggedIn = manager.isLoggedIn();

    loginButton.setVisible (! loggedIn);
    logoutButton.setVisible (loggedIn);

    if (! manager.hasClientId())
        statusLabel.setText ("Paste your publishable key from tone3000.com -> Settings -> API Keys",
                              juce::dontSendNotification);
    else
        statusLabel.setText (loggedIn ? "Logged in. Search for gear from inside any block that takes a model."
                                       : "Key saved. Log in to search and download.",
                              juce::dontSendNotification);
}

void Tone3000Panel::stopDeviceLogin()
{
    deviceLoginPoll.stopTimer();
    devicePollInFlight = false;
    if (deviceCodeLabel.isVisible())
    {
        deviceCodeLabel.setVisible (false);
        deviceQr.setVisible (false);
        openHereButton.setVisible (false);
        loginButton.setButtonText ("Log in");
        layoutContent();
    }
}

void Tone3000Panel::doDeviceLogin()
{
    stopDeviceLogin();
    if (! manager.hasClientId())
    {
        statusLabel.setText ("Set your client_id first.", juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Asking TONE3000 for a login code...", juce::dontSendNotification);
    manager.beginDeviceLogin ([safe = juce::Component::SafePointer<Tone3000Panel> (this)]
                              (bool ok, Tone3000Manager::DeviceLogin login, juce::String error)
    {
        if (safe == nullptr)
            return;
        if (! ok)
        {
            safe->statusLabel.setText (error, juce::dontSendNotification);
            return;
        }

        safe->deviceLogin = login;
        safe->deviceLoginStartMs = juce::Time::getMillisecondCounterHiRes();
        safe->deviceLoginIntervalSec = login.intervalSec;
        safe->deviceCodeLabel.setText (login.userCode, juce::dontSendNotification);
        safe->deviceCodeLabel.setVisible (true);
        safe->deviceQr.setText (login.verificationUriComplete);
        safe->deviceQr.setVisible (true);
        // A desktop has a browser of its own; the panel (kiosk) has none, so there the phone is the only way.
        safe->openHereButton.setVisible (! touch::kioskMode());
        safe->loginButton.setButtonText ("New code");
        const auto where = login.verificationUri.replace ("https://www.", {}).replace ("https://", {});
        safe->statusLabel.setText ("Scan the code with your phone and sign in to TONE3000 (or open " + where
                                   + " and type the code). This screen finishes by itself.",
                                   juce::dontSendNotification);
        safe->layoutContent();
        safe->deviceLoginPoll.startTimer (juce::jmax (1, login.intervalSec) * 1000);
    });
}

void Tone3000Panel::pollDeviceLogin()
{
    if (devicePollInFlight)
        return;

    if ((juce::Time::getMillisecondCounterHiRes() - deviceLoginStartMs) / 1000.0 > (double) deviceLogin.expiresInSec)
    {
        stopDeviceLogin();
        statusLabel.setText ("The code expired. Tap Log in for a new one.", juce::dontSendNotification);
        return;
    }

    devicePollInFlight = true;
    manager.pollDeviceLogin (deviceLogin, [safe = juce::Component::SafePointer<Tone3000Panel> (this)]
                                          (Tone3000Manager::DevicePoll result, juce::String error)
    {
        if (safe == nullptr)
            return;
        safe->devicePollInFlight = false;

        using P = Tone3000Manager::DevicePoll;
        switch (result)
        {
            case P::pending:
                break;
            case P::slowDown:
                safe->deviceLoginIntervalSec += 5; // RFC 8628: back off by 5 s and keep going
                safe->deviceLoginPoll.startTimer (safe->deviceLoginIntervalSec * 1000);
                break;
            case P::done:
                safe->stopDeviceLogin();
                safe->statusLabel.setText ("Logged in.", juce::dontSendNotification);
                safe->refreshLoginState();
                break;
            case P::denied:
                safe->stopDeviceLogin();
                safe->statusLabel.setText ("The login was declined on the phone.", juce::dontSendNotification);
                break;
            case P::expired:
                safe->stopDeviceLogin();
                safe->statusLabel.setText ("The code expired. Tap Log in for a new one.", juce::dontSendNotification);
                break;
            case P::failed:
                safe->stopDeviceLogin();
                safe->statusLabel.setText (error.isNotEmpty() ? error : juce::String ("Login failed."), juce::dontSendNotification);
                break;
        }
    });
}

void Tone3000Panel::resized()
{
    auto area = getLocalBounds().reduced (22);

    auto header = area.removeFromTop (76);
    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    titleLabel.setBounds (header.removeFromLeft (280));

    auto body = area;
    navRailBounds = body.removeFromLeft (240);
    viewport.setBounds (body.reduced (12, 0));

    auto rail = navRailBounds.reduced (12, 10);
    for (auto* component : navButtons)
    {
        component->setBounds (rail.removeFromTop (touch::minTapTarget));
        rail.removeFromTop (8);
    }

    layoutContent();
}

void Tone3000Panel::layoutContent()
{
    const int w = juce::jmax (120, viewport.getMaximumVisibleWidth());
    const int h = layoutActivePage ({ 0, 0, w, 10000 });
    content.setSize (w, h);

    juce::Component* pages[] = { &appearancePage, &audioPage, &connectionsPage, &qualityPage, &tone3000Page, &midiPage, &expressionPage, &updatesPage, &aboutPage };
    const int activeIndex = juce::jlimit (0, 8, (int) currentSection);
    for (auto* page : pages)
    {
        page->setBounds (content.getLocalBounds());
        page->setVisible (page == pages[activeIndex]);
    }

    layoutActivePage (content.getLocalBounds());
}

int Tone3000Panel::layoutActivePage (juce::Rectangle<int> bounds)
{
    auto area = bounds.reduced (16, 12);
    const int startY = area.getY();

    switch (currentSection)
    {
        case Section::appearance:
        {
            appearanceSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            auto cards = area.removeFromTop (208);
            const int gap = 12;
            const int cardW = (cards.getWidth() - gap * 2) / 3;
            for (auto* component : themeCards)
            {
                component->setBounds (cards.removeFromLeft (cardW));
                cards.removeFromLeft (gap);
            }
            area.removeFromTop (12);
            themeHintLabel.setBounds (area.removeFromTop (48));
            break;
        }
        case Section::quality:
        {
            qualitySectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            auto qualityRow = area.removeFromTop (touch::minTapTarget);
            ecoButton.setBounds (qualityRow.removeFromLeft (140));
            qualityRow.removeFromLeft (10);
            normalButton.setBounds (qualityRow.removeFromLeft (140));
            qualityRow.removeFromLeft (10);
            highButton.setBounds (qualityRow.removeFromLeft (140));
            area.removeFromTop (10);
            qualityHintLabel.setBounds (area.removeFromTop (64));
            break;
        }
        case Section::audio:
        {
            interfaceSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            interfaceLabel.setBounds (area.removeFromTop (28));
            area.removeFromTop (14);

            rateSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            auto rateRow = area.removeFromTop (touch::minTapTarget);
            for (auto* b : rateButtons)
            {
                b->setBounds (rateRow.removeFromLeft (120));
                rateRow.removeFromLeft (8);
            }
            if (retryDeviceButton.isVisible())
                retryDeviceButton.setBounds (rateRow.removeFromLeft (200));
            area.removeFromTop (6);
            rateHintLabel.setBounds (area.removeFromTop (40));
            area.removeFromTop (14);

            bufferSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            auto bufferRow = area.removeFromTop (touch::minTapTarget);
            for (auto* b : bufferButtons)
            {
                b->setBounds (bufferRow.removeFromLeft (110));
                bufferRow.removeFromLeft (8);
            }
            area.removeFromTop (6);
            bufferHintLabel.setBounds (area.removeFromTop (48));
            area.removeFromTop (14);

            perfSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            perfButton.setBounds (area.removeFromTop (touch::minTapTarget).removeFromLeft (110));
            area.removeFromTop (6);
            perfHintLabel.setBounds (area.removeFromTop (60));
            area.removeFromTop (14);
            sendSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            sendButton.setBounds (area.removeFromTop (touch::minTapTarget).removeFromLeft (110));
            area.removeFromTop (6);
            sendHintLabel.setBounds (area.removeFromTop (60));
            area.removeFromTop (14);
            slowClockSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            {
                auto row = area.removeFromTop (touch::minTapTarget);
                slowAutoButton.setBounds (row.removeFromLeft (110));
                row.removeFromLeft (8);
                slow416Button.setBounds (row.removeFromLeft (130));
                row.removeFromLeft (8);
                slow780Button.setBounds (row.removeFromLeft (130));
            }
            area.removeFromTop (6);
            slowClockHintLabel.setBounds (area.removeFromTop (92));
            area.removeFromTop (14);
            monitorSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            monitorButton.setBounds (area.removeFromTop (touch::minTapTarget).removeFromLeft (110));
            area.removeFromTop (6);
            monitorHintLabel.setBounds (area.removeFromTop (60));
            break;
        }
        case Section::connections:
        {
            connectionsSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (10);
            auto row = area.removeFromTop (touch::minTapTarget + 8);
            wifiOpenButton.setBounds (row.removeFromLeft (200));
            row.removeFromLeft (12);
            bluetoothOpenButton.setBounds (row.removeFromLeft (200));
            area.removeFromTop (8);
            connectionsHintLabel.setBounds (area.removeFromTop (70));
            break;
        }
        case Section::tone3000:
        {
            tone3000SectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);

            auto keyRow = area.removeFromTop (touch::minTapTarget);
            clientIdLabel.setBounds (keyRow.removeFromLeft (90));
            saveClientIdButton.setBounds (keyRow.removeFromRight (110));
            clientIdField.setBounds (keyRow.reduced (4, 0));
            area.removeFromTop (8);

            auto loginRow = area.removeFromTop (touch::minTapTarget);
            loginButton.setBounds (loginRow.removeFromLeft (130));
            logoutButton.setBounds (loginRow.getX(), loginRow.getY(), 130, loginRow.getHeight());
            area.removeFromTop (10);
            if (deviceCodeLabel.isVisible())
            {
                // QR on the left, the code to type and the desktop-only button beside it.
                auto row = area.removeFromTop (220);
                deviceQr.setBounds (row.removeFromLeft (220));
                row.removeFromLeft (16);
                deviceCodeLabel.setBounds (row.removeFromTop (52));
                if (openHereButton.isVisible())
                {
                    row.removeFromTop (8);
                    openHereButton.setBounds (row.removeFromTop (touch::minTapTarget).withWidth (juce::jmin (row.getWidth(), 300)));
                }
                area.removeFromTop (10);
            }
            statusLabel.setBounds (area.removeFromTop (64));
            break;
        }
        case Section::midi:
        {
            midiTableLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            midiTable.setBounds (area.removeFromTop (midiTable.getIdealHeight()));
            break;
        }
        case Section::expression:
        {
            expressionIntroLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (6);
            expressionHintLabel.setBounds (area.removeFromTop (60));
            for (auto* editor : expressionEditors)
            {
                area.removeFromTop (18);
                editor->setBounds (area.removeFromTop (ExpressionPedalEditor::idealHeight));
            }
            break;
        }
        case Section::updates:
        {
            updateSectionLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            auto updateRow = area.removeFromTop (touch::minTapTarget);
            checkUpdateButton.setBounds (updateRow.removeFromLeft (170));
            updateRow.removeFromLeft (8);
            installUpdateButton.setBounds (updateRow.removeFromLeft (190));
            if (updateRow.getWidth() > 150 + 8 + 70)
            {
                updateRow.removeFromLeft (8);
                restartAppButton.setBounds (updateRow.removeFromLeft (150));
                updateRow.removeFromLeft (8);
                currentVersionLabel.setBounds (updateRow);
            }
            else
            {
                area.removeFromTop (4);
                auto row2 = area.removeFromTop (touch::minTapTarget);
                restartAppButton.setBounds (row2.removeFromLeft (150));
                row2.removeFromLeft (8);
                currentVersionLabel.setBounds (row2);
            }
            area.removeFromTop (6);
            updateStatusLabel.setBounds (area.removeFromTop (44));
            updateProgressBar.setBounds (area.removeFromTop (12));
            break;
        }
        case Section::about:
        {
            aboutIntroLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            aboutDetailsLabel.setBounds (area.removeFromTop (120));
            break;
        }
    }

    // The page is laid out again inside exactly this height, reduced by the same 12 px top AND bottom: count both,
    // or the last row comes out 12 px short (clipped Remove buttons on the MIDI table).
    return area.getY() - startY + 24;
}

void Tone3000Panel::applyUpdateUiState()
{
    const auto snap = updater.getUiSnapshot();

    checkUpdateButton.setEnabled (! snap.checkInFlight && ! snap.installInFlight);
    installUpdateButton.setEnabled (! snap.checkInFlight && ! snap.installInFlight);

    installUpdateButton.setVisible (snap.candidate.has_value() && ! snap.restartRequired);
    if (snap.candidate.has_value())
        installUpdateButton.setButtonText ("Update to " + snap.candidate->version);

    restartAppButton.setVisible (snap.restartRequired);

    if (snap.statusText.isNotEmpty())
        updateStatusLabel.setText (snap.statusText, juce::dontSendNotification);

    const bool busyNow = snap.checkInFlight || snap.installInFlight;
    if (snap.checkInFlight || (snap.installInFlight && updater.isPackagedInstall()))
        updateProgressValue = -1.0;
    else if (snap.installInFlight)
        updateProgressValue = snap.downloadProgress;
    updateProgressBar.setVisible (busyNow);

    if (busyNow)
        startTimer (300);
    else
        stopTimer();
}

void Tone3000Panel::timerCallback()
{
    applyUpdateUiState();
}

void Tone3000Panel::doCheckForUpdate()
{
    updater.checkForUpdate ([safeThis = juce::Component::SafePointer<Tone3000Panel> (this)]
                            (const AppUpdater::CheckResult&)
    {
        if (safeThis != nullptr)
            safeThis->applyUpdateUiState();
    });
    applyUpdateUiState();
}

void Tone3000Panel::doInstallUpdate()
{
    const auto candidate = updater.getUiSnapshot().candidate;
    if (! candidate.has_value())
        return;

    updater.downloadAndInstall (*candidate,
        [safeThis = juce::Component::SafePointer<Tone3000Panel> (this)] (const juce::String&)
        {
            if (safeThis != nullptr)
                safeThis->applyUpdateUiState();
        },
        [safeThis = juce::Component::SafePointer<Tone3000Panel> (this)] (double)
        {
            if (safeThis != nullptr)
                safeThis->applyUpdateUiState();
        },
        [safeThis = juce::Component::SafePointer<Tone3000Panel> (this)]
            (bool, juce::String, bool)
        {
            if (safeThis != nullptr)
                safeThis->applyUpdateUiState();
        });
    applyUpdateUiState();
}

void Tone3000Panel::paint (juce::Graphics& g)
{
    overlaychrome::paintCardBackground (g, getLocalBounds().toFloat());
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 76.0f));

    if (navRailBounds.getWidth() > 0)
    {
        g.setColour (theme::current().line);
        g.drawVerticalLine (navRailBounds.getRight() + 6, 76.0f, (float) getHeight() - 22.0f);
    }
}

} // namespace openguitarmultifx
