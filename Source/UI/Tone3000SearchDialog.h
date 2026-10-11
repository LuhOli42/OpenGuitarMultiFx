#pragma once

#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    A focused popup for searching and downloading ONE category of TONE3000
    gear -- gearFilter is fixed at construction (see
    GearRouting.h::gearFilterForProcessorName), not a dropdown the user
    picks, because this dialog is always opened from a specific block
    ("Search TONE3000..." on a Neural Amp block only ever searches amps).
*/
class Tone3000SearchDialog : public juce::Component,
                              private juce::ListBoxModel
{
public:
    Tone3000SearchDialog (Tone3000Manager& managerToUse, juce::String gearFilterToUse, juce::File destinationFolderToUse);

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Fires once a result has been downloaded and is ready to load. */
    std::function<void (juce::File)> onFileReady;

    /** Wired by whoever hosts this dialog to OverlayHost::popOverlay -- see
        AGENT.md's UI/UX Design Philosophy: this is a card on top of the app
        window, not a juce::DialogWindow, so "close" just means "pop me". */
    std::function<void()> onPopOverlay;

private:
    void doSearch();
    void doDownloadSelected();

    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    Tone3000Manager& manager;
    juce::String gearFilter;
    juce::File destinationFolder;

    // Bumped on every doSearch() call; a search's async callback only
    // applies its results if this still matches what it started with --
    // otherwise a newer search has already started (or the network
    // returned out of order) and the response is stale. Without this, a
    // second search could leave the first search's results on screen, or
    // even have a slow first response overwrite a faster second one.
    int searchGeneration = 0;

    juce::Label titleLabel;
    juce::TextEditor searchField;
    juce::ComboBox architectureBox;
    juce::TextButton searchButton { "Search" };
    juce::ListBox resultsList { "tone3000results", this };
    juce::TextButton downloadButton { "Download & load" };
    juce::TextButton closeButton { "Close" };
    juce::Label statusLabel;

    std::vector<Tone3000Manager::Tone> results;
};

} // namespace openguitarmultifx
