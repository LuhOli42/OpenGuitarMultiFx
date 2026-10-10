#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    In-app overlay for saving/loading/deleting presets -- opened from the
    preset badge in the top bar. Same "pretty list, empty is fine" pattern
    as ModelListDialog (see AGENT.md's UI/UX Design Philosophy): no native
    file dialog. Unlike ModelListDialog (one tap loads), a row here only
    selects -- Load and Delete are separate explicit actions, since both
    are meaningful things to do to an EXISTING preset and one tap can't
    mean both.
*/
class PresetListDialog : public juce::Component,
                          private juce::ListBoxModel
{
public:
    explicit PresetListDialog (juce::StringArray existingNames);

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Fires when the user taps Load with a preset selected. */
    std::function<void (juce::String presetName)> onPresetChosen;
    /** Fires when the user taps Save with a name typed in. */
    std::function<void (juce::String presetName)> onSaveRequested;
    /** Fires when the user taps Delete with a preset selected. */
    std::function<void (juce::String presetName)> onDeleteRequested;
    /** Fire when the user taps Rename / Duplicate with a preset selected. */
    std::function<void (juce::String presetName)> onRenameRequested;
    std::function<void (juce::String presetName)> onDuplicateRequested;
    std::function<void()> onPopOverlay;

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    juce::Label titleLabel;
    juce::TextEditor nameField;
    juce::TextButton saveButton { "Save current" };
    juce::ListBox listBox { "presets", this };
    juce::Label emptyStateLabel;
    juce::TextButton loadButton { "Load selected" };
    juce::TextButton renameButton { "Rename" };
    juce::TextButton duplicateButton { "Duplicate" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton closeButton { "Close" };

    juce::StringArray names;

    /** Calls `callback` with the selected preset's name, if a row is selected. */
    void withSelected (const std::function<void (juce::String)>& callback);
};

} // namespace openguitarmultifx
