#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace openguitarmultifx
{

/**
    In-app list of already-installed model files for one category (see
    GearRouting.h) -- this is what "Browse installed..." opens, and it
    replaces what used to be a juce::PopupMenu plus a native file-open
    dialog. Per AGENT.md's UI/UX Design Philosophy: a pedalboard player
    picks gear, they don't browse a filesystem. Tapping a row loads it
    immediately -- no separate "confirm" step. An empty list is a normal,
    plainly-labelled state, not an error.

    "Import from device" is kept as a single small affordance at the
    bottom for getting a file onto the PC prototype in the first place; it
    still opens a native file picker under the hood (unavoidable -- the OS
    has to hand over file bytes somehow), but it is not the primary path
    and won't exist at all once the final device only ever gets gear via
    TONE3000 search or a companion app.
*/
class ModelListDialog : public juce::Component,
                         private juce::ListBoxModel
{
public:
    ModelListDialog (juce::String titleText, juce::Array<juce::File> installedFiles,
                      juce::File importFolder, juce::String importWildcard);

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Fires with the chosen (or imported) file. */
    std::function<void (juce::File)> onFileChosen;

    /** Wired by whoever hosts this dialog to OverlayHost::popOverlay. */
    std::function<void()> onPopOverlay;

    /** Optional escape from the empty state: pops this card and opens the
        TONE3000 search. Only shown when wired AND the list is empty -- when
        installed files exist the host's own Search button already covers it. */
    std::function<void()> onSearchRequested;

private:
    void parentHierarchyChanged() override;
    void updateEmptyState();

    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    juce::Label titleLabel;
    juce::ListBox listBox { "installedModels", this };
    juce::Label emptyStateLabel;
    juce::TextButton searchButton { "Search TONE3000..." };
    juce::TextButton importButton { "Import from device..." };
    juce::TextButton closeButton { "Close" };

    juce::Array<juce::File> files;
    juce::File importFolder;
    juce::String importWildcard;
    std::unique_ptr<juce::FileChooser> fileChooser; // kept alive for the async picker
};

} // namespace openguitarmultifx
