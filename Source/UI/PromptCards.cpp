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
                        std::function<void (int)> onChoice)
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

} // namespace openguitarmultifx
