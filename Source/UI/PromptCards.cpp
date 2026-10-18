#include "PromptCards.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    const juce::Colour cardBackground { 0xff1a1a1a };

    void styleTitle (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::Font (19.0f, juce::Font::bold));
    }
}

ChoiceCard::ChoiceCard (const juce::String& title, const juce::String& message, juce::StringArray buttonLabels,
                        std::function<void (int)> onChoice, int cancelButtonIndex)
    : cancelIndex (cancelButtonIndex)
{
    styleTitle (titleLabel, title);
    addAndMakeVisible (titleLabel);

    messageLabel.setText (message, juce::dontSendNotification);
    messageLabel.setFont (16.0f);
    messageLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (messageLabel);

    for (int i = 0; i < buttonLabels.size(); ++i)
    {
        auto* button = buttons.add (new juce::TextButton (buttonLabels[i]));
        // Copied to the stack first: the handler usually pops this card, destroying the button and this lambda.
        button->onClick = [onChoice, i] { if (auto callback = onChoice) callback (i); };
        addAndMakeVisible (button);
    }

    setSize (440, 190);
}

void ChoiceCard::resized()
{
    auto area = getLocalBounds().reduced (14);
    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    auto row = area.removeFromBottom (touch::minTapTarget);
    const int gap = 8;
    const int width = buttons.isEmpty() ? 0 : (row.getWidth() - gap * (buttons.size() - 1)) / buttons.size();
    for (auto* button : buttons)
    {
        button->setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    }

    area.removeFromBottom (8);
    messageLabel.setBounds (area);
}

void ChoiceCard::paint (juce::Graphics& g)
{
    g.fillAll (cardBackground);
}

void ChoiceCard::outsideTapDismissed()
{
    // Destructive confirms mark their Cancel button: the outside tap runs
    // that exact handler (which pops the card itself), so tapping away is
    // identical to tapping Cancel -- it can never trigger the destructive
    // branch. Cards with no marked cancel index do nothing here and the
    // host pops them plainly.
    if (cancelIndex >= 0 && cancelIndex < buttons.size())
        buttons[cancelIndex]->triggerClick();
}

TextPromptCard::TextPromptCard (const juce::String& title, const juce::String& initialText,
                                std::function<void (juce::String)> onAccepted, std::function<void()> onCancelled)
{
    styleTitle (titleLabel, title);
    addAndMakeVisible (titleLabel);

    field.setFont (juce::Font (18.0f)); // before setText: TextEditor::setFont only styles text added after it
    field.setJustification (juce::Justification::centredLeft);
    field.setText (initialText, false);
    field.onTextChange = [this] { okButton.setEnabled (field.getText().trim().isNotEmpty()); };
    field.onReturnKey = [this] { if (okButton.isEnabled()) okButton.triggerClick(); };
    addAndMakeVisible (field);

    okButton.setEnabled (initialText.trim().isNotEmpty());
    okButton.onClick = [this, onAccepted]
    {
        // Same as ChoiceCard: nothing of this card may be touched once the callback runs.
        auto callback = onAccepted;
        const auto text = field.getText().trim();
        if (callback)
            callback (text);
    };
    addAndMakeVisible (okButton);

    cancelButton.onClick = [onCancelled] { if (auto callback = onCancelled) callback(); };
    addAndMakeVisible (cancelButton);

    setSize (440, 170);
}

void TextPromptCard::resized()
{
    auto area = getLocalBounds().reduced (14);
    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    field.setBounds (area.removeFromTop (touch::minTapTarget));

    auto row = area.removeFromBottom (touch::minTapTarget);
    cancelButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    okButton.setBounds (row.removeFromRight (110));
}

void TextPromptCard::paint (juce::Graphics& g)
{
    g.fillAll (cardBackground);
}

void TextPromptCard::outsideTapDismissed()
{
    // Tapping away is Cancel, including whatever the caller wired to it --
    // not a silent disappearance that skips onCancelled entirely.
    cancelButton.triggerClick();
}

ColourPickerCard::ColourPickerCard (const juce::String& title, juce::Colour initialColour,
                                    std::function<void (juce::Colour)> onAccepted, std::function<void()> onCancelled)
{
    styleTitle (titleLabel, title);
    addAndMakeVisible (titleLabel);

    selector.setColour (juce::ColourSelector::backgroundColourId, cardBackground);
    selector.setCurrentColour (initialColour);
    addAndMakeVisible (selector);

    okButton.onClick = [this, onAccepted]
    {
        // Same as the cards above: nothing of this card may be touched once the callback runs.
        auto callback = onAccepted;
        const auto colour = selector.getCurrentColour();
        if (callback)
            callback (colour);
    };
    addAndMakeVisible (okButton);

    cancelButton.onClick = [onCancelled] { if (auto callback = onCancelled) callback(); };
    addAndMakeVisible (cancelButton);

    setSize (460, 480);
}

void ColourPickerCard::resized()
{
    auto area = getLocalBounds().reduced (14);
    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);

    auto row = area.removeFromBottom (touch::minTapTarget);
    cancelButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    okButton.setBounds (row.removeFromRight (110));

    area.removeFromBottom (8);
    selector.setBounds (area);
}

void ColourPickerCard::paint (juce::Graphics& g)
{
    g.fillAll (cardBackground);
}

void ColourPickerCard::outsideTapDismissed()
{
    cancelButton.triggerClick();
}

NumPadCard::NumPadCard (const juce::String& title, const juce::String& initialText,
                        std::function<void (juce::String)> onAccepted, std::function<void()> onCancelled)
{
    styleTitle (titleLabel, title);
    addAndMakeVisible (titleLabel);

    // Read-only display: tapping it must NOT open the OS keyboard -- that's
    // the whole reason this card exists (see the class comment).
    display.setReadOnly (true);
    display.setCaretVisible (false);
    display.setInterceptsMouseClicks (false, false);
    display.setFont (juce::Font (26.0f, juce::Font::bold));
    display.setJustification (juce::Justification::centred);
    display.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.25f));
    display.setText (initialText);
    addAndMakeVisible (display);

    auto refreshOk = [this] { okButton.setEnabled (display.getText().trim().isNotEmpty()); };
    refreshOk();

    auto appendChar = [this, refreshOk] (juce::juce_wchar c)
    {
        auto t = display.getText();
        // One decimal point max; a '-' only ever leads.
        if (c == '.' && t.containsChar ('.'))
            return;
        display.setText (t + juce::String::charToString (c), false);
        refreshOk();
    };
    auto backspace = [this, refreshOk]
    {
        display.setText (display.getText().dropLastCharacters (1), false);
        refreshOk();
    };
    auto toggleSign = [this, refreshOk]
    {
        auto t = display.getText();
        display.setText (t.startsWithChar ('-') ? t.substring (1) : "-" + t, false);
        refreshOk();
    };

    const juce::String labels[] { "7", "8", "9", "4", "5", "6", "1", "2", "3", "+/-", "0", ".", "Del" };
    for (const auto& keyLabel : labels)
    {
        auto* key = keys.add (new juce::TextButton (keyLabel));
        if (keyLabel == "+/-")
            key->onClick = toggleSign;
        else if (keyLabel == "Del")
            key->onClick = backspace;
        else
            key->onClick = [appendChar, c = keyLabel[0]] { appendChar (c); };
        addAndMakeVisible (*key);
    }

    okButton.onClick = [this, onAccepted]
    {
        // Same as ChoiceCard: nothing of this card may be touched once the callback runs.
        auto callback = onAccepted;
        const auto t = display.getText().trim();
        if (callback && t.isNotEmpty())
            callback (t);
    };
    addAndMakeVisible (okButton);

    cancelButton.onClick = [onCancelled] { if (auto callback = onCancelled) callback(); };
    addAndMakeVisible (cancelButton);

    setSize (340, 470);
}

void NumPadCard::resized()
{
    auto area = getLocalBounds().reduced (14);
    titleLabel.setBounds (area.removeFromTop (26));
    area.removeFromTop (8);
    display.setBounds (area.removeFromTop (touch::minTapTarget));
    area.removeFromTop (10);

    auto row = area.removeFromBottom (touch::minTapTarget);
    cancelButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (8);
    okButton.setBounds (row.removeFromRight (110));
    area.removeFromBottom (10);

    // 13 keys: 1-9, "+/-", "0", "." on a 3-column grid (4 rows), then a
    // full-width "Del" row underneath.
    const int gap = 8;
    const int keyW = (area.getWidth() - 2 * gap) / 3;
    const int keyH = juce::jmax (touch::minTapTarget, (area.getHeight() - 4 * gap) / 5);
    for (int i = 0; i < keys.size(); ++i)
    {
        const int col = i % 3, r = i / 3;
        if (i == 12) // "Del" -- the wide bottom row
            keys[i]->setBounds (area.getX(), area.getY() + r * (keyH + gap), area.getWidth(), keyH);
        else
            keys[i]->setBounds (area.getX() + col * (keyW + gap), area.getY() + r * (keyH + gap), keyW, keyH);
    }
}

void NumPadCard::paint (juce::Graphics& g)
{
    g.fillAll (cardBackground);
}

} // namespace openguitarmultifx
