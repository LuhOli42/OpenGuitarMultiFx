#pragma once

#include "SnapshotPresetXml.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace openguitarmultifx
{

/**
    The A-H snapshot strip that sits next to the preset badge in the top
    bar (docs/PresetsAndSnapshots.md, stage 3: "A-H bar"). One cell per
    scene in use, tap to recall, and a "+" cell after them to add the
    next one (user request 2026-11-01: start with one scene, grow to
    eight on demand). Custom-painted rather than eight
    TextButtons, same reasoning as PresetBadge: one component, no
    LookAndFeel font cap, and the current cell draws in the snapshot's
    colour rather than a generic button highlight.

    Every cell is touch::minTapTarget wide -- the bar should be given
    8 * touch::minTapTarget of width by whoever lays it out, and
    touch::minTapTarget + hintHeight of height (cells on the bottom, the
    discoverability caption on top).

    The hint line (audit #93 item 3): the long-press-edit gesture used to
    be entirely invisible -- same hidden-gesture pattern MIDI Learn and
    the preset browser use, but those at least had nowhere else to
    advertise it. A caption this small is read-once chrome, not a
    persistent control, so it gets one line at the top rather than a
    competing button row.
*/
class SnapshotBar : public juce::Component,
                    private juce::Timer
{
public:
    /** 0-7 highlights that letter as the live snapshot. */
    void setCurrent (int index);

    /** Scenes in use (1-8): cells A up to that letter, then a "+" cell while there is room for another. */
    void setCount (int scenesInUse);

    /** Each slot's name (drawn small under its letter; empty = letter only) and colour (the live cell's fill). */
    void setLabels (const std::array<juce::String, snapshotxml::maxSnapshots>& names,
                    const std::array<juce::Colour, snapshotxml::maxSnapshots>& colours);

    /** The footswitch CC learned for each cell (-1 = none), drawn small in the cell's corner, and the
        caption over the cells: normally the gesture hint, while a pedal is being learned it says so
        (empty = back to the hint). */
    void setPedalCcs (const std::array<int, snapshotxml::maxSnapshots>& ccs);
    void setCaptionOverride (const juce::String& text);

    /** The heavier display weight for the letters -- same
        OpenGuitarMultiFxLookAndFeel::getExtraBoldTypeface() PresetBadge
        uses (the target build has no system fonts at all). Falls back to
        the current LookAndFeel's bold if never set. */
    void setDisplayTypeface (juce::Typeface::Ptr typeface);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Tap: recall. Fires on release, unless the press turned into a long-press. */
    std::function<void (int)> onSnapshotSelected;
    /** Press and hold (longPressMs) a cell: the snapshot's edit menu (rename, colour, copy). A tap stays a pure,
        instant recall, which is what the bar is for on stage -- same hidden-gesture pattern as MIDI Learn. */
    std::function<void (int)> onSnapshotLongPressed;
    /** Tap "+": add a scene (the owner copies the live one). Hold "+": choose which scene the new one copies. */
    std::function<void()> onAddPressed;
    std::function<void()> onAddLongPressed;

    static constexpr int longPressMs = 550;

    /** Height of the "tap -- hold" caption strip above the cells. */
    static constexpr int hintHeight = 16;

private:
    int cellIndexAt (juce::Point<int> position) const;
    void timerCallback() override;

    /** The "+" cell's index, or -1 when all eight are in use. */
    int addCellIndex() const noexcept { return count < snapshotxml::maxSnapshots ? count : -1; }

    int current = 0;
    int count = 1;
    int pressedIndex = -1;
    bool longPressFired = false;
    std::array<juce::String, snapshotxml::maxSnapshots> labels;
    std::array<juce::Colour, snapshotxml::maxSnapshots> cellColours;
    std::array<int, snapshotxml::maxSnapshots> pedalCcs { -1, -1, -1, -1, -1, -1, -1, -1 };
    juce::String captionOverride;
    juce::Typeface::Ptr displayTypeface;
};

} // namespace openguitarmultifx
