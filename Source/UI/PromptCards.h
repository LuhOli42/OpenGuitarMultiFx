#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace openguitarmultifx
{

/**
    Small question card for OverlayHost ("Delete preset X?", "Save changes?"): a title, one line of message and
    2-3 buttons. Not juce::AlertWindow, which opens a separate OS-level window this UI never uses (see OverlayHost).
    The callback gets the tapped button's index and fires before the card is popped, so it may push a new card.
*/
class ChoiceCard : public juce::Component
{
public:
    ChoiceCard (const juce::String& title, const juce::String& message, juce::StringArray buttonLabels,
                std::function<void (int choice)> onChoice);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label titleLabel, messageLabel;
    juce::OwnedArray<juce::TextButton> buttons;
};

/** Same shape as ChoiceCard, with a text field pre-filled with `initialText`: OK hands back the trimmed text,
    Cancel just closes. OK is disabled while the field is empty. */
class TextPromptCard : public juce::Component
{
public:
    TextPromptCard (const juce::String& title, const juce::String& initialText,
                    std::function<void (juce::String)> onAccepted, std::function<void()> onCancelled);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label titleLabel;
    juce::TextEditor field;
    juce::TextButton okButton { "OK" }, cancelButton { "Cancel" };
};

} // namespace openguitarmultifx
