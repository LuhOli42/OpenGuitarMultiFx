#include "ModelListDialog.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

ModelListDialog::ModelListDialog (juce::String titleText, juce::Array<juce::File> installedFiles,
                                   juce::File importFolderToUse, juce::String importWildcardToUse)
    : files (std::move (installedFiles))
    , importFolder (std::move (importFolderToUse))
    , importWildcard (std::move (importWildcardToUse))
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText (titleText, juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (listBox);
    listBox.setColour (juce::ListBox::backgroundColourId, theme::current().background);
    listBox.setRowHeight (56);
    listBox.setVisible (! files.isEmpty());

    addAndMakeVisible (emptyStateLabel);
    emptyStateLabel.setText ("Nothing installed yet.\nSearch TONE3000 to add some gear.", juce::dontSendNotification);
    emptyStateLabel.setJustificationType (juce::Justification::centred);
    emptyStateLabel.setMinimumHorizontalScale (1.0f);
    emptyStateLabel.setVisible (files.isEmpty());

    // Hidden until resized() decides it applies (wired host + empty list).
    addChildComponent (searchButton);
    searchButton.onClick = [this]
    {
        // Copy before popping: popOverlay destroys this card.
        auto search = onSearchRequested;
        auto pop = onPopOverlay;
        if (pop)
            pop();
        if (search)
            search();
    };

    addAndMakeVisible (importButton);
    importButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Select a file...", importFolder, importWildcard);
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& chooser)
            {
                const auto file = chooser.getResult();
                auto chosen = onFileChosen;
                auto pop = onPopOverlay;
                if (pop)
                    pop();
                if (file != juce::File() && chosen)
                    chosen (file);
            });
    };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    setSize (480, 520);
}

int ModelListDialog::getNumRows()
{
    return files.size();
}

void ModelListDialog::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= files.size())
        return;

    const auto& t = theme::current();
    auto row = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (3.0f, 2.0f);

    if (rowIsSelected)
    {
        g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour().withAlpha (0.28f));
        g.fillRoundedRectangle (row, t.smallRadius);
        g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
        g.fillRoundedRectangle (row.removeFromLeft (4.0f), t.smallRadius);
    }
    else
    {
        g.setColour (t.raised);
        g.fillRoundedRectangle (row, t.smallRadius);
    }

    const auto& file = files.getReference (rowNumber);
    auto textArea = row.reduced (14.0f, 0.0f);
    g.setColour (t.text);
    g.setFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    g.drawText (file.getFileNameWithoutExtension(), textArea.removeFromTop (height * 0.55f), juce::Justification::centredLeft);

    g.setColour (t.textMuted);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (file.getFileExtension() + "  -  " + file.getParentDirectory().getFileName(), textArea,
                juce::Justification::centredLeft);
}

void ModelListDialog::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= files.size())
        return;

    // Pop FIRST, then notify: a failure card pushed inside onFileChosen (a
    // corrupt/incompatible model file) has to survive on screen instead of
    // being instantly popped by this dialog's own close.
    auto chosen = onFileChosen;
    auto pop = onPopOverlay;
    const auto file = files.getReference (row);
    if (pop)
        pop();
    if (chosen)
        chosen (file);
}

void ModelListDialog::resized()
{
    auto area = getLocalBounds().reduced (14);

    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto bottomRow = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (bottomRow.removeFromRight (90));
    bottomRow.removeFromRight (8);
    importButton.setBounds (bottomRow);

    area.removeFromBottom (10);
    listBox.setBounds (area);
    updateEmptyState();
}

void ModelListDialog::parentHierarchyChanged()
{
    // The host wires onSearchRequested AFTER construction (right before the
    // overlay push), and JUCE only calls resized() on SIZE changes -- the
    // host's centring move doesn't refire it. Re-checking here is the only
    // point that's both post-wiring and guaranteed to run.
    updateEmptyState();
}

void ModelListDialog::updateEmptyState()
{
    searchButton.setVisible (files.isEmpty() && onSearchRequested != nullptr);
    if (getWidth() == 0)
        return; // before the first setSize()/resized()

    // Same rect arithmetic as resized(), computed for just the empty-state
    // area so this can run from either hook.
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (32);                          // title + gap
    area.removeFromBottom (touch::minTapTarget + 10); // buttons row + gap

    if (searchButton.isVisible())
    {
        emptyStateLabel.setBounds (area.removeFromTop (60));
        area.removeFromTop (16);
        searchButton.setBounds (area.removeFromTop (touch::minTapTarget)
                                    .withSizeKeepingCentre (220, touch::minTapTarget));
    }
    else
    {
        emptyStateLabel.setBounds (area);
    }
}

void ModelListDialog::paint (juce::Graphics& g)
{
    g.fillAll (theme::current().surface);
}

} // namespace openguitarmultifx
