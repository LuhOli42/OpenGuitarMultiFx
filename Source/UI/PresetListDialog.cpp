#include "PresetListDialog.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

PresetListDialog::PresetListDialog (juce::StringArray existingNames)
    : names (std::move (existingNames))
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Presets", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (nameField);
    nameField.setTextToShowWhenEmpty ("Preset name...", juce::Colours::grey);
    nameField.onReturnKey = [this] { saveButton.triggerClick(); };

    addAndMakeVisible (saveButton);
    saveButton.onClick = [this]
    {
        const auto n = nameField.getText().trim();
        if (n.isNotEmpty() && onSaveRequested)
            onSaveRequested (n);
    };

    addAndMakeVisible (listBox);
    listBox.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff141414));
    listBox.setRowHeight (touch::minTapTarget);
    listBox.setVisible (! names.isEmpty());

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setText ("No presets saved yet.", juce::dontSendNotification);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    emptyStateLabel.setVisible (names.isEmpty());

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { withSelected (onPresetChosen); };

    addAndMakeVisible (renameButton);
    renameButton.onClick = [this] { withSelected (onRenameRequested); };

    addAndMakeVisible (duplicateButton);
    duplicateButton.onClick = [this] { withSelected (onDuplicateRequested); };

    addAndMakeVisible (deleteButton);
    deleteButton.onClick = [this] { withSelected (onDeleteRequested); };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    setSize (560, 480);
}

void PresetListDialog::withSelected (const std::function<void (juce::String)>& callback)
{
    const int row = listBox.getSelectedRow();
    if (row >= 0 && row < names.size() && callback)
        callback (names[row]);
}

int PresetListDialog::getNumRows()
{
    return names.size();
}

void PresetListDialog::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= names.size())
        return;

    if (rowIsSelected)
        g.fillAll (juce::Colour (0xff2d5c56));

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);
    g.drawText (names[rowNumber], 14, 0, width - 24, height, juce::Justification::centredLeft);
}

void PresetListDialog::resized()
{
    auto area = getLocalBounds().reduced (14);

    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto saveRow = area.removeFromTop (touch::minTapTarget);
    saveButton.setBounds (saveRow.removeFromRight (120));
    saveRow.removeFromRight (6);
    nameField.setBounds (saveRow);

    area.removeFromTop (8);

    auto bottomRow = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (bottomRow.removeFromRight (90));
    bottomRow.removeFromRight (8);
    deleteButton.setBounds (bottomRow.removeFromRight (80));
    bottomRow.removeFromRight (8);
    duplicateButton.setBounds (bottomRow.removeFromRight (100));
    bottomRow.removeFromRight (8);
    renameButton.setBounds (bottomRow.removeFromRight (90));
    bottomRow.removeFromRight (8);
    loadButton.setBounds (bottomRow.removeFromRight (130));

    area.removeFromBottom (8);
    listBox.setBounds (area);
    emptyStateLabel.setBounds (area);
}

void PresetListDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
}

} // namespace openguitarmultifx
