#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "OverlayChrome.h"
#include "OverlayHost.h"

namespace openguitarmultifx
{

// Card 4 of docs/ui/REDESIGN.md: the snapshot editor as an OverlayHost card
// instead of the old nested PopupMenu (rename / colour / footswitch-CC /
// copy / remove). MainComponent owns every callback; this class only lays
// out the controls and reports intent, like every other card in PromptCards.
class SnapshotEditorCard : public juce::Component, public OutsideTapAware
{
public:
    // Footswitch actions mirror scenemidi::Bindings: a scene index, or
    // scenemidi::nextScene (-1) / scenemidi::previousScene (-2).
    struct Actions
    {
        std::function<void()> onClose;
        std::function<void (juce::String)> onNameChanged;
        std::function<void (juce::Colour)> onColourChosen;   // Colour() = back to default
        std::function<void()> onCustomColour;                // open the colour picker card
        std::function<int (int action)> ccFor;
        std::function<bool (int action)> pedalArmed;
        std::function<void (int action)> onTogglePedal;
        std::function<void (int targetIndex)> onCopyTo;
        std::function<void (int sourceIndex)> onCopyFrom;
        std::function<void()> onRemove;
    };

    SnapshotEditorCard (int index, juce::String letter, juce::String name,
                        juce::Colour colour, bool usingDefaultColour,
                        Actions actions);

    void resized() override;
    void paint (juce::Graphics&) override;
    void outsideTapDismissed() override { if (actions.onClose) actions.onClose(); }

    // One round swatch of the preset colour row; `selected` draws the ring.
    class ColourSwatch : public juce::Component
    {
    public:
        ColourSwatch (juce::Colour c, bool selected, std::function<void()> onClick);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::Colour colour;
        bool selected = false;
        std::function<void()> onClick;
    };

    // One footswitch-CC row: action label + current CC + Learn/Clear button.
    class FootswitchRow : public juce::Component
    {
    public:
        FootswitchRow (juce::String title, int action, Actions& actions);
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();

    private:
        juce::String title;
        int action;
        Actions& actions;
        juce::Label statusLabel;
        overlaychrome::ActionButton toggleButton;
    };

private:
    int index;
    juce::String letter;
    Actions actions;
    juce::Colour currentColour;
    bool usingDefaultColour = true;

    juce::Label titleLabel;
    overlaychrome::CloseButton closeButton;

    juce::Label nameLabel;
    juce::TextEditor nameEditor;
    overlaychrome::ActionButton nameOkButton { "Set" };

    juce::Label colourLabel;
    juce::OwnedArray<ColourSwatch> swatches;
    overlaychrome::ActionButton customButton { "Custom..." };

    juce::OwnedArray<FootswitchRow> pedalRows;

    overlaychrome::ActionButton copyToButton { "Copy to..." };
    overlaychrome::ActionButton copyFromButton { "Copy from..." };
    overlaychrome::ActionButton removeButton { "Remove scene", overlaychrome::ButtonKind::danger };

    void commitName();
    void rebuildSwatches();
    void rebuildPedalRows();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotEditorCard)
};

} // namespace openguitarmultifx
