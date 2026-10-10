#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    Shows the current preset's NUMBER, big and bold -- this is a stage
    instrument, not a desktop app; the player needs to read it from a few
    feet away, not squint at a toolbar (see AGENT.md's UI/UX Design
    Philosophy). Custom-painted rather than a juce::TextButton so its font
    size isn't at the mercy of LookAndFeel::getTextButtonFont()'s
    proportional-to-height cap, same idea as EffectBlockComponent/
    RowEndpointBlock elsewhere in this UI.

    Shows number+snapshot letter ("3B") -- snapshots A-H are live now
    (docs/PresetsAndSnapshots.md, stage 3). The letter is drawn whenever
    one is set, even without a loaded preset (an unsaved chain still has
    an active snapshot).
*/
class PresetBadge : public juce::Component
{
public:
    /** 0 = no preset loaded (shows just the snapshot letter, or a
        placeholder dash if no letter is set either). */
    void setNumber (int number);

    /** Which snapshot letter to draw after the number ("3B"): -1 hides
        it. Snapshots A-H map to indices 0-7 (see SnapshotPresetXml.h). */
    void setSnapshotIndex (int index);

    /** The heavier display weight for the big number -- see
        OpenGuitarMultiFxLookAndFeel::getExtraBoldTypeface(). Falls back to the
        current LookAndFeel's bold if never set. */
    void setDisplayTypeface (juce::Typeface::Ptr typeface);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void()> onClicked;

private:
    int currentNumber = 0;
    int snapshotIndex = -1;
    juce::Typeface::Ptr displayTypeface;
};

} // namespace openguitarmultifx
