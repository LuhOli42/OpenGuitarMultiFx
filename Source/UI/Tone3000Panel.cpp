#include "Tone3000Panel.h"
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
    titleLabel.setFont (juce::Font (23.0f, juce::Font::bold));

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);

    const char* navTitles[] = { "Appearance", "Audio", "Quality", "TONE3000", "MIDI", "Updates", "About" };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = new juce::TextButton (navTitles[i]);
        navButtons.add (b);
        addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4701);
        b->setToggleState (i == 0, juce::dontSendNotification);
        b->onClick = [this, i] { selectSection (i); };
    }

    juce::Component* pages[] = { &appearancePage, &audioPage, &qualityPage, &tone3000Page, &midiPage, &updatesPage, &aboutPage };
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
    loginButton.onClick = [this] { doLogin(); };

    tone3000Page.addAndMakeVisible (logoutButton);
    logoutButton.onClick = [this] { manager.logOut(); refreshLoginState(); };

    tone3000Page.addAndMakeVisible (statusLabel);

    midiPage.addAndMakeVisible (midiIntroLabel);
    midiIntroLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    midiIntroLabel.setText ("MIDI", juce::dontSendNotification);
    midiPage.addAndMakeVisible (midiHintLabel);
    midiHintLabel.setFont (juce::Font (15.0f));
    midiHintLabel.setText ("Learn a knob: press and hold its name in the parameter drawer, then move a MIDI control.\n"
                           "Learn a scene footswitch: press and hold the + on the snapshot strip.\n"
                           "Program Change 1-128 recalls presets 1-128. Every available MIDI input is enabled automatically.",
                           juce::dontSendNotification);

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
    setSize (980, 720);
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
        case 1: next = Section::audio;      break;
        case 2: next = Section::quality;    break;
        case 3: next = Section::tone3000;   break;
        case 4: next = Section::midi;       break;
        case 5: next = Section::updates;    break;
        case 6: next = Section::about;      break;
        default: return;
    }
    if (currentSection == next)
        return;

    currentSection = next;
    refreshNavButtons();
    layoutContent();
    viewport.setViewPosition (0, 0);
}

void Tone3000Panel::applyLabelColours()
{
    const auto& t = theme::current();
    const auto muted = [this, &t] (juce::Label& label) { label.setColour (juce::Label::textColourId, t.textMuted); };

    for (juce::Label* label : { &themeHintLabel, &qualityHintLabel, &rateHintLabel, &bufferHintLabel, &statusLabel,
                                &updateStatusLabel, &midiHintLabel, &aboutDetailsLabel, &interfaceLabel,
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

void Tone3000Panel::doLogin()
{
    const auto authorizeUrl = manager.beginLogin();

    if (authorizeUrl.isEmpty())
    {
        statusLabel.setText ("Set your client_id first.", juce::dontSendNotification);
        return;
    }

    const auto redirectPort = juce::URL (manager.getRedirectUri()).getPort();

    const bool listening = loopback.start (redirectPort, [safeThis = juce::Component::SafePointer<Tone3000Panel> (this)]
                                                          (const juce::StringPairArray& params)
    {
        juce::MessageManager::callAsync ([safeThis, params]
        {
            if (safeThis != nullptr)
                safeThis->finishLogin (params);
        });
    });

    if (listening && juce::URL (authorizeUrl).launchInDefaultBrowser())
    {
        statusLabel.setText ("Finish logging in in your browser, then come back here.",
                             juce::dontSendNotification);
        return;
    }

    loopback.stop();
    doLoginEmbedded (authorizeUrl);
}

void Tone3000Panel::finishLogin (const juce::StringPairArray& params)
{
    const auto code = params["code"];
    const auto state = params["state"];
    const auto oauthError = params["error"];

    if (oauthError.isNotEmpty())
    {
        statusLabel.setText ("Authorization denied: " + oauthError, juce::dontSendNotification);
        return;
    }

    statusLabel.setText ("Completing login...", juce::dontSendNotification);

    manager.completeLogin (code, state, [this] (bool success, juce::String error)
    {
        statusLabel.setText (success ? "Logged in." : error, juce::dontSendNotification);
        refreshLoginState();
    });
}

void Tone3000Panel::doLoginEmbedded (const juce::String& authorizeUrl)
{
    auto dialog = std::make_unique<OAuthLoginDialog> (authorizeUrl, manager.getRedirectUri());
    auto* dialogPtr = dialog.get();

    dialogPtr->onRedirectReached = [this] (const juce::StringPairArray& params)
    {
        if (onPopOverlay)
            onPopOverlay();

        finishLogin (params);
    };

    dialogPtr->onCancelled = [this]
    {
        if (onPopOverlay)
            onPopOverlay();
        statusLabel.setText ("Login cancelled.", juce::dontSendNotification);
    };

    if (onPushOverlay)
        onPushOverlay (std::move (dialog));
}

void Tone3000Panel::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto titleRow = area.removeFromTop (touch::minTapTarget + 8);
    closeButton.setBounds (titleRow.removeFromRight (110).withSizeKeepingCentre (110, touch::minTapTarget));
    titleLabel.setBounds (titleRow);
    area.removeFromTop (12);

    auto body = area;
    navRailBounds = body.removeFromLeft (220);
    body.removeFromLeft (12);
    viewport.setBounds (body);

    auto rail = navRailBounds.reduced (10, 8);
    for (auto* component : navButtons)
    {
        component->setBounds (rail.removeFromTop (touch::minTapTarget));
        rail.removeFromTop (6);
    }

    layoutContent();
}

void Tone3000Panel::layoutContent()
{
    const int w = juce::jmax (120, viewport.getMaximumVisibleWidth());
    const int h = layoutActivePage ({ 0, 0, w, 10000 });
    content.setSize (w, h);

    juce::Component* pages[] = { &appearancePage, &audioPage, &qualityPage, &tone3000Page, &midiPage, &updatesPage, &aboutPage };
    const int activeIndex = juce::jlimit (0, 6, (int) currentSection);
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
            statusLabel.setBounds (area.removeFromTop (64));
            break;
        }
        case Section::midi:
        {
            midiIntroLabel.setBounds (area.removeFromTop (26));
            area.removeFromTop (8);
            midiHintLabel.setBounds (area.removeFromTop (150));
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

    return area.getY() - startY + 12;
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
    g.fillAll (theme::current().background);
    if (navRailBounds.getWidth() > 0)
    {
        g.setColour (theme::current().line);
        const float x = (float) navRailBounds.getRight() + 6.0f;
        g.drawVerticalLine (x, (float) getLocalBounds().getY() + 16.0f, (float) getLocalBounds().getBottom() - 16.0f);
    }
}

} // namespace openguitarmultifx
