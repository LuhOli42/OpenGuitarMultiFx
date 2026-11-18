#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "OverlayChrome.h"
#include "OverlayHost.h"

namespace openguitarmultifx
{

// The scene (snapshot) editor card, laid out after the "snap" page of
// docs/ui/prototype.html: one row per setting (name, colour, start-from,
// footswitch, copy) with the label column on the left, and Delete / Done in
// the footer. It opens from a hold on a scene cell, and right after the "+"
// cell creates a scene ("new" mode: Start from + Cancel instead of Copy +
// Delete).
//
// MainComponent owns every callback and the data; the card pulls a fresh
// State after each action (and when it becomes visible again, e.g. back from
// the colour picker), so what it shows is never stale.
class SnapshotEditorCard : public juce::Component,
                           public OutsideTapAware,
                           private juce::Timer
{
public:
    struct SceneInfo
    {
        juce::String letter;
        juce::String name;
        juce::Colour colour;
    };

    struct State
    {
        int index = 0;
        juce::String name;
        juce::Colour colour;
        bool usingDefaultColour = true;
        std::vector<SceneInfo> scenes; // every scene of the preset, A first
        int sourceIndex = -1;          // new mode: the scene this one was copied from
    };

    // Footswitch actions mirror scenemidi::Bindings: a scene index, or
    // scenemidi::nextScene (-1) / scenemidi::previousScene (-2).
    struct Actions
    {
        std::function<State()> getState;
        std::function<void()> onClose;                       // Done / X / tap outside
        std::function<void()> onCancelNew;                   // new mode: drop the scene again
        std::function<void (juce::String)> onNameChanged;
        std::function<void (juce::Colour)> onColourChosen;   // a palette colour
        std::function<void()> onCustomColour;                // open the colour picker card
        std::function<int (int action)> ccFor;
        std::function<bool (int action)> pedalArmed;
        std::function<void (int action)> onTogglePedal;
        std::function<void (int sourceIndex)> onStartFrom;   // new mode: re-copy from that scene
        std::function<void (int targetIndex)> onCopyTo;
        std::function<void()> onRemove;
    };

    SnapshotEditorCard (bool isNewScene, Actions actions);
    ~SnapshotEditorCard() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void outsideTapDismissed() override { if (actions.onClose) actions.onClose(); }

    // Re-reads State and updates every control.
    void refresh();

    // A 48 px colour square of the palette row; `selected` draws the ring.
    class ColourSwatch : public juce::Component
    {
    public:
        ColourSwatch (juce::Colour c, std::function<void()> onClick);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        juce::Colour colour;
        bool selected = false;
        std::function<void()> onClick;
    };

    // A scene letter cell in the scene's own colour (Start from / Copy to).
    class SceneChip : public juce::Component
    {
    public:
        explicit SceneChip (std::function<void()> onClick);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        SceneInfo info;
        bool selected = false;
        bool armed = false;  // first tap of a two-tap action
        std::function<void()> onClick;
    };

    // One footswitch row: action label + current CC + Learn/Clear button.
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

    static constexpr int cardWidth = 760;

private:
    const bool isNew;
    Actions actions;
    State state;

    juce::Label titleLabel;
    overlaychrome::CloseButton closeButton;

    juce::TextEditor nameEditor;

    juce::OwnedArray<ColourSwatch> swatches;
    overlaychrome::ActionButton customButton { "Custom..." };

    juce::OwnedArray<SceneChip> sourceChips; // new mode
    juce::OwnedArray<SceneChip> copyChips;   // edit mode, every scene but this one
    juce::Label copyStatus;
    int armedCopyTarget = -1;

    juce::OwnedArray<FootswitchRow> pedalRows;

    overlaychrome::ActionButton removeButton { "Delete scene", overlaychrome::ButtonKind::danger };
    overlaychrome::ActionButton cancelButton { "Cancel" };
    overlaychrome::ActionButton doneButton { "Done", overlaychrome::ButtonKind::primary };
    bool removeArmed = false;

    // Row layout, shared by paint() (labels + separators) and resized().
    struct Row { juce::String key, hint; int height; };
    std::vector<Row> rows() const;
    int heightForRows() const;

    void rebuildChips();
    void disarm();
    void timerCallback() override { disarm(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapshotEditorCard)
};

} // namespace openguitarmultifx
