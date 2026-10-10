#pragma once

#include "SnapshotPresetXml.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace openguitarmultifx
{

/**
    The A-H snapshot strip that sits next to the preset badge in the top
    bar (docs/PresetsAndSnapshots.md, stage 3: "A-H bar"). Eight fixed
    cells, one per snapshot, tap to recall -- the whole recall surface
    for this stage; the full snapshots editor (names, colours, per-knob
    markers) is a later card. Custom-painted rather than eight
    TextButtons, same reasoning as PresetBadge: one component, no
    LookAndFeel font cap, and the current cell draws in the snapshot's
    colour rather than a generic button highlight.

    Every cell is touch::minTapTarget wide -- the bar should be given
    8 * touch::minTapTarget of width by whoever lays it out.
*/
class SnapshotBar : public juce::Component,
                    private juce::Timer
{
public:
    /** 0-7 highlights that letter as the live snapshot. */
    void setCurrent (int index);

    /** Each slot's name (drawn small under its letter; empty = letter only) and colour (the live cell's fill). */
    void setLabels (const std::array<juce::String, snapshotxml::maxSnapshots>& names,
                    const std::array<juce::Colour, snapshotxml::maxSnapshots>& colours);

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

    static constexpr int longPressMs = 550;

private:
    int cellIndexAt (juce::Point<int> position) const;
    void timerCallback() override;

    int current = 0;
    int pressedIndex = -1;
    bool longPressFired = false;
    std::array<juce::String, snapshotxml::maxSnapshots> labels;
    std::array<juce::Colour, snapshotxml::maxSnapshots> cellColours;
    juce::Typeface::Ptr displayTypeface;
};

} // namespace openguitarmultifx
