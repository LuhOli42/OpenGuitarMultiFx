#pragma once

#include "OverlayHost.h"

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

    `cancelButtonIndex` marks which button is the cancel/dismiss choice on
    a confirm card (a destructive "Delete?" / "Replace?" / "Save, Discard,
    Cancel"): an outside tap then routes through that button's handler --
    tapping away reads as pressing Cancel, never as doing nothing or as
    confirming. Cards without one (a plain "OK" error card) dismiss plainly.
*/
class ChoiceCard : public juce::Component,
                   public OutsideTapAware
{
public:
    ChoiceCard (const juce::String& title, const juce::String& message, juce::StringArray buttonLabels,
                std::function<void (int choice)> onChoice, int cancelButtonIndex = -1);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void outsideTapDismissed() override;

private:
    juce::Label titleLabel, messageLabel;
    juce::OwnedArray<juce::TextButton> buttons;
    int cancelIndex = -1;
};

/** Same shape as ChoiceCard, with a text field pre-filled with `initialText`: OK hands back the trimmed text,
    Cancel just closes. OK is disabled while the field is empty. */
class TextPromptCard : public juce::Component,
                       public OutsideTapAware
{
public:
    TextPromptCard (const juce::String& title, const juce::String& initialText,
                    std::function<void (juce::String)> onAccepted, std::function<void()> onCancelled);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void outsideTapDismissed() override;

private:
    juce::Label titleLabel;
    juce::TextEditor field;
    juce::TextButton okButton { "OK" }, cancelButton { "Cancel" };
};

/** Colour-picker card, for the snapshot colour editor's "Custom colour..." row (2026-10-12): a
    juce::ColourSelector (swatch + HSV space + RGB sliders) with touch-sized OK/Cancel, hosted like every
    other card on OverlayHost rather than ColourSelector's own CallOutBox/native-window entry points. */
class ColourPickerCard : public juce::Component,
                         public OutsideTapAware
{
public:
    ColourPickerCard (const juce::String& title, juce::Colour initialColour,
                      std::function<void (juce::Colour)> onAccepted, std::function<void()> onCancelled);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void outsideTapDismissed() override;

private:
    juce::Label titleLabel;
    juce::ColourSelector selector;
    juce::TextButton okButton { "OK" }, cancelButton { "Cancel" };
};

/** On-screen numeric keypad for typing an exact parameter value -- the
    touchscreen has no OS keyboard for a slider's text box, and a rotary
    drag can't reliably land an exact number (audit #93's "numeric keypad
    card" option). OK hands back the typed text; the caller converts it
    through AudioParameterFloat::getValueForText, whose getFloatValue
    parse tolerates any unit suffix the param's getText() appends. The
    pre-filled value is replaced by the first digit or decimal key. */
class NumPadCard : public juce::Component,
                   public OutsideTapAware
{
public:
    NumPadCard (const juce::String& title, const juce::String& initialText,
                std::function<void (juce::String)> onAccepted, std::function<void()> onCancelled);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void outsideTapDismissed() override { cancelButton.triggerClick(); }

private:
    juce::Label titleLabel;
    juce::TextEditor display;
    juce::OwnedArray<juce::TextButton> keys;
    juce::TextButton okButton { "OK" }, cancelButton { "Cancel" };
    bool replaceOnNextKey = true;
};

} // namespace openguitarmultifx
