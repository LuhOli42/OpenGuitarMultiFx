#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h> // ColourSelector lives in gui_extra, not gui_basics

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

/** Colour-picker card, for the snapshot colour editor's "Custom colour..." row (2026-10-12): a
    juce::ColourSelector (swatch + HSV space + RGB sliders) with touch-sized OK/Cancel, hosted like every
    other card on OverlayHost rather than ColourSelector's own CallOutBox/native-window entry points. */
class ColourPickerCard : public juce::Component
{
public:
    ColourPickerCard (const juce::String& title, juce::Colour initialColour,
                      std::function<void (juce::Colour)> onAccepted, std::function<void()> onCancelled);

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label titleLabel;
    juce::ColourSelector selector;
    juce::TextButton okButton { "OK" }, cancelButton { "Cancel" };
};

} // namespace openguitarmultifx
