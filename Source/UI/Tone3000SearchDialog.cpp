#include "Tone3000SearchDialog.h"

#include "TouchSizing.h"
#include "../Tone3000/GearRouting.h"

namespace openguitarmultifx
{

Tone3000SearchDialog::Tone3000SearchDialog (Tone3000Manager& managerToUse, juce::String gearFilterToUse,
                                             juce::File destinationFolderToUse)
    : manager (managerToUse), gearFilter (std::move (gearFilterToUse)), destinationFolder (std::move (destinationFolderToUse))
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Search TONE3000 (" + gearFilter + ")", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));

    addAndMakeVisible (searchField);
    searchField.setTextToShowWhenEmpty ("JCM800, Plexi, Tube Screamer...", juce::Colours::grey);
    searchField.onReturnKey = [this] { doSearch(); };

    // TONE3000 defaults to "A1 + Custom, excluding A2" when this isn't sent
    // at all -- see Tone3000Manager::searchTones()'s doc comment. Exposed as
    // an explicit choice rather than picked for the user, since there's no
    // single value that means "everything". A2 is the default since NAM core
    // v0.6.0 loads it: it is the cheaper architecture on the CPU (user
    // request 2026-11-02; 5.6 % vs 7.1 % for a standard A1 WaveNet here).
    addAndMakeVisible (architectureBox);
    architectureBox.addItem ("A2 (default)", 2);
    architectureBox.addItem ("A1 + Custom", 1);
    architectureBox.addItem ("A1 only", 3);
    architectureBox.addItem ("Custom only", 4);
    architectureBox.setSelectedId (2, juce::dontSendNotification);

    addAndMakeVisible (searchButton);
    searchButton.onClick = [this] { doSearch(); };

    addAndMakeVisible (resultsList);
    resultsList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff161616));
    resultsList.setRowHeight (touch::minTapTarget);

    addAndMakeVisible (downloadButton);
    downloadButton.onClick = [this] { doDownloadSelected(); };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setFont (13.5f);

    if (! manager.isLoggedIn())
        statusLabel.setText ("Not logged in -- open TONE3000 from the toolbar to log in first.", juce::dontSendNotification);

    setSize (520, 420);
    searchField.grabKeyboardFocus();
}

void Tone3000SearchDialog::doSearch()
{
    const auto query = searchField.getText().trim();
    if (query.isEmpty())
        return;

    ++searchGeneration;
    const int thisGeneration = searchGeneration;

    // Clear the old results immediately, not just once the new response
    // arrives -- a second search used to leave the first search's results
    // sitting on screen for however long the request took, which read as
    // "the search button doesn't do anything".
    results.clear();
    resultsList.updateContent();
    resultsList.deselectAllRows();

    statusLabel.setText ("Searching for \"" + query + "\"...", juce::dontSendNotification);

    juce::String architectureFilter;
    switch (architectureBox.getSelectedId())
    {
        case 2: architectureFilter = "2"; break;      // A2
        case 3: architectureFilter = "1"; break;      // A1 only
        case 4: architectureFilter = "custom"; break; // Custom only
        default: break;                               // "A1 + Custom (default)" -- send nothing
    }

    manager.searchTones (query, gearFilter, architectureFilter,
        [this, thisGeneration] (bool success, std::vector<Tone3000Manager::Tone> found, juce::String error)
        {
            // A newer search has started (or this one just arrived out of
            // order) -- its own callback already owns the screen, or will.
            if (thisGeneration != searchGeneration)
                return;

            if (! success)
            {
                statusLabel.setText (error, juce::dontSendNotification);
                return;
            }

            results = std::move (found);
            resultsList.updateContent();
            resultsList.deselectAllRows();
            statusLabel.setText (juce::String ((int) results.size()) + " result(s).", juce::dontSendNotification);
        });
}

void Tone3000SearchDialog::doDownloadSelected()
{
    const int row = resultsList.getSelectedRow();
    if (row < 0 || row >= (int) results.size())
    {
        statusLabel.setText ("Select a result first.", juce::dontSendNotification);
        return;
    }

    const auto& toneResult = results[(size_t) row];
    const auto route = tone3000routing::routeFor (toneResult.gear, toneResult.format);

    if (! route.supported)
    {
        statusLabel.setText ("\"" + toneResult.title + "\" is format \"" + toneResult.format
                                  + "\", which this engine can't load.",
                              juce::dontSendNotification);
        return;
    }

    const auto safeName = juce::File::createLegalFileName (
        toneResult.title.isEmpty() ? juce::String (toneResult.id) : toneResult.title);
    destinationFolder.createDirectory();
    const auto destination = destinationFolder.getChildFile (safeName + route.fileExtension);

    statusLabel.setText ("Downloading \"" + toneResult.title + "\"...", juce::dontSendNotification);
    downloadButton.setEnabled (false);

    manager.downloadFirstModelForTone (toneResult.id, destination,
        [this, destination] (bool success, juce::String error)
        {
            downloadButton.setEnabled (true);

            if (! success)
            {
                statusLabel.setText (error, juce::dontSendNotification);
                return;
            }

            statusLabel.setText ("Loaded: " + destination.getFileName(), juce::dontSendNotification);

            if (onFileReady)
                onFileReady (destination);
        });
}

int Tone3000SearchDialog::getNumRows()
{
    return (int) results.size();
}

void Tone3000SearchDialog::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= (int) results.size())
        return;

    const auto& toneResult = results[(size_t) rowNumber];

    if (rowIsSelected)
        g.fillAll (juce::Colour (0xff2d5c56));

    g.setColour (juce::Colours::white);
    g.setFont (15.5f);
    g.drawText (toneResult.title, 8, 0, width - 16, height / 2, juce::Justification::centredLeft);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (12.5f);
    juce::String subtitle = toneResult.author;
    if (toneResult.license.isNotEmpty())
        subtitle += "  ·  " + toneResult.license;
    g.drawText (subtitle, 8, height / 2, width - 16, height / 2, juce::Justification::centredLeft);
}

void Tone3000SearchDialog::resized()
{
    auto area = getLocalBounds().reduced (12);

    titleLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto searchRow = area.removeFromTop (touch::minTapTarget);
    searchButton.setBounds (searchRow.removeFromRight (90));
    searchRow.removeFromRight (6);
    architectureBox.setBounds (searchRow.removeFromRight (150));
    searchRow.removeFromRight (6);
    searchField.setBounds (searchRow);

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    auto bottomRow = area.removeFromBottom (touch::minTapTarget);
    closeButton.setBounds (bottomRow.removeFromRight (90));
    bottomRow.removeFromRight (8);
    downloadButton.setBounds (bottomRow.removeFromRight (170));

    area.removeFromBottom (8);
    resultsList.setBounds (area);
}

void Tone3000SearchDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141414));
}

} // namespace openguitarmultifx
