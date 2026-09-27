#include "Tone3000Panel.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

Tone3000Panel::Tone3000Panel (Tone3000Manager& managerToUse)
    : manager (managerToUse)
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (23.0f, juce::Font::bold));

    addAndMakeVisible (qualitySectionLabel);
    qualitySectionLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    qualitySectionLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    using Q = EffectRegistry::OversamplingQuality;
    const struct { juce::TextButton* button; Q quality; } choices[] = { { &ecoButton, Q::eco }, { &normalButton, Q::balanced }, { &highButton, Q::high } };
    for (const auto& c : choices)
    {
        addAndMakeVisible (*c.button);
        c.button->setClickingTogglesState (true);
        c.button->setRadioGroupId (4711);
        c.button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2d5c56));
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

    addAndMakeVisible (qualityHintLabel);
    qualityHintLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    qualityHintLabel.setFont (juce::Font (14.0f));
    refreshQualityHint();

    addAndMakeVisible (rateSectionLabel);
    rateSectionLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    rateSectionLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (rateHintLabel);
    rateHintLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    rateHintLabel.setFont (juce::Font (14.0f));

    addAndMakeVisible (tone3000SectionLabel);
    tone3000SectionLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    tone3000SectionLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible (clientIdLabel);

    addAndMakeVisible (clientIdField);
    clientIdField.setTextToShowWhenEmpty ("t3k_pub_...", juce::Colours::grey);
    clientIdField.setText (manager.getClientId(), juce::dontSendNotification);

    addAndMakeVisible (saveClientIdButton);
    saveClientIdButton.onClick = [this]
    {
        manager.setClientId (clientIdField.getText());
        refreshLoginState();
    };

    addAndMakeVisible (loginButton);
    loginButton.onClick = [this] { doLogin(); };

    addAndMakeVisible (logoutButton);
    logoutButton.onClick = [this] { manager.logOut(); refreshLoginState(); };

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    refreshLoginState();
    setSize (720, 640); // clamped by OverlayHost to fit the app window -- this is the "whole screen" settings surface
}

void Tone3000Panel::showSampleRates (const juce::Array<double>& rates, double current)
{
    rateButtons.clear();
    rateValues.clear();
    currentRate = current;

    // The device may list many rates; a guitar processor only wants the usual ones.
    for (double r : rates)
        if (r == 44100.0 || r == 48000.0 || r == 88200.0 || r == 96000.0)
            rateValues.add (r);

    for (double r : rateValues)
    {
        auto* b = rateButtons.add (new juce::TextButton (juce::String (r / 1000.0, r == 44100.0 || r == 88200.0 ? 1 : 0) + " kHz"));
        addAndMakeVisible (b);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4712);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2d5c56));
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

    rateHintLabel.setText (rateValues.isEmpty() ? "No audio device is open." : "The rate the audio device runs at (48 kHz is the default). Changing it restarts audio.",
                           juce::dontSendNotification);
    resized();
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

    // Preferred path: the system browser, where the user is usually already
    // signed in to TONE3000, with a one-shot loopback listener catching the
    // redirect. Needs no embedded WebKit at all, which is what made the
    // in-app login pane render blank on this dev machine. If either half is
    // unavailable (port taken, or no browser on the device -- the final
    // touchscreen target) fall through to the embedded browser instead.
    const auto redirectPort = juce::URL (manager.getRedirectUri()).getPort();

    const bool listening = loopback.start (redirectPort, [safeThis = juce::Component::SafePointer<Tone3000Panel> (this)]
                                                          (const juce::StringPairArray& params)
    {
        // Background thread -- hop to the message thread before touching UI/manager.
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
    // Embedded in-app browser, not the system one -- see OAuthLoginDialog
    // and Tone3000Manager's class comment for why: this is the only login
    // flow that also works on a touchscreen device with no browser. Shown as
    // a further overlay layer on top of this panel, not a second window.
    auto dialog = std::make_unique<OAuthLoginDialog> (authorizeUrl, manager.getRedirectUri());
    auto* dialogPtr = dialog.get();

    dialogPtr->onRedirectReached = [this] (const juce::StringPairArray& params)
    {
        if (onPopOverlay)
            onPopOverlay(); // back to this panel

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

    // Close sits on the title row: the parameter drawer of a selected block is drawn OVER the bottom of this card,
    // which used to hide a bottom-right Close button.
    auto titleRow = area.removeFromTop (touch::minTapTarget);
    closeButton.setBounds (titleRow.removeFromRight (100));
    titleLabel.setBounds (titleRow);
    area.removeFromTop (10);

    qualitySectionLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    auto qualityRow = area.removeFromTop (touch::minTapTarget);
    ecoButton.setBounds (qualityRow.removeFromLeft (120));
    qualityRow.removeFromLeft (8);
    normalButton.setBounds (qualityRow.removeFromLeft (120));
    qualityRow.removeFromLeft (8);
    highButton.setBounds (qualityRow.removeFromLeft (120));
    area.removeFromTop (6);
    qualityHintLabel.setBounds (area.removeFromTop (50));
    area.removeFromTop (14);

    rateSectionLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);
    auto rateRow = area.removeFromTop (touch::minTapTarget);
    for (auto* b : rateButtons)
    {
        b->setBounds (rateRow.removeFromLeft (120));
        rateRow.removeFromLeft (8);
    }
    area.removeFromTop (6);
    rateHintLabel.setBounds (area.removeFromTop (34));
    area.removeFromTop (14);

    tone3000SectionLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    auto keyRow = area.removeFromTop (touch::minTapTarget);
    clientIdLabel.setBounds (keyRow.removeFromLeft (70));
    saveClientIdButton.setBounds (keyRow.removeFromRight (90));
    clientIdField.setBounds (keyRow.reduced (4, 0));

    area.removeFromTop (8);

    auto loginRow = area.removeFromTop (touch::minTapTarget);
    loginButton.setBounds (loginRow.removeFromLeft (110));
    logoutButton.setBounds (loginRow.getX(), loginRow.getY(), 110, loginRow.getHeight());

    area.removeFromTop (10);
    statusLabel.setBounds (area.removeFromTop (44));

}

void Tone3000Panel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));
}

} // namespace openguitarmultifx
