#pragma once

#include "../Effects/EffectProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    One tile in the horizontal signal chain (Quad Cortex's "Grid" is the
    reference point: a row of device blocks you tap to select and edit).
    Purely a view -- it never owns the processor, never touches the audio
    thread, and reads/writes only through the generic EffectProcessor
    contract (bypass, name, status) so it works for any registered effect
    type without knowing about it.
*/
class EffectBlockComponent : public juce::Component,
                             private juce::Timer
{
public:
    explicit EffectBlockComponent (EffectProcessor& processorToShow);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

    void setSelected (bool shouldBeSelected);

    /** The footswitch CC that toggles this block (-1 = none), drawn small in the corner; `armed` while the app waits
        for the stomp that will be learned (drawn "..."). */
    void setPedal (int cc, bool armed);

    /** Which of the maxVisibleRows x chainColumns grid cells this block
        currently occupies (row*columns+col) -- independent of this block's
        position in MainComponent's `blocks`/`chain` arrays, which stay
        sorted by THIS value and are what SignalGraph actually reads in
        order. Lets a block be dropped/added at any grid cell you click,
        not just the next sequential slot, while the underlying processing
        order stays a plain contiguous array with no notion of "gaps" of
        its own -- per user request 2026-09-10 ("ta adicionando ainda
        sequencialmente em vez daonde eu clico"). See
        MainComponent::layoutChain()/addEffect()/handleBlockDragEnded(). */
    int gridSlot = 0;

    /** Press and hold (longPressMs) without dragging: the block's footswitch menu. A tap still selects and a drag still
        moves, so this is the same hidden-gesture pattern as the snapshot bar and MIDI Learn. */
    std::function<void()> onLongPressed;
    static constexpr int longPressMs = 550;

    /** A plain click (no meaningful drag) selects the block for editing. */
    std::function<void()> onClicked;

    /** Fires once, on mouse-up, only if this block was actually dragged --
        the block's current (mid-drag) x position is where it was dropped;
        the caller decides what index that maps to and reorders. */
    std::function<void (EffectBlockComponent&)> onDragEnded;

    EffectProcessor& processor;

private:
    void timerCallback() override;

    int pedalCc = -1;
    bool pedalArmed = false;
    bool longPressFired = false;
    bool selected = false;
    bool isDragging = false;
    juce::ComponentDragger dragger;
};

} // namespace openguitarmultifx
