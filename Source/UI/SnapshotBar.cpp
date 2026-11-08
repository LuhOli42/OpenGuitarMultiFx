#include "SnapshotBar.h"
#include "Theme.h"

#include "TouchSizing.h"

namespace openguitarmultifx
{

void SnapshotBar::setCurrent (int index)
{
    index = juce::jlimit (0, snapshotxml::maxSnapshots - 1, index);
    if (current != index)
    {
        current = index;
        repaint();
    }
}

void SnapshotBar::setCount (int scenesInUse)
{
    scenesInUse = juce::jlimit (1, snapshotxml::maxSnapshots, scenesInUse);
    if (count != scenesInUse)
    {
        count = scenesInUse;
        repaint();
    }
}

void SnapshotBar::setLabels (const std::array<juce::String, snapshotxml::maxSnapshots>& names,
                             const std::array<juce::Colour, snapshotxml::maxSnapshots>& colours)
{
    if (names != labels || colours != cellColours)
    {
        labels = names;
        cellColours = colours;
        repaint();
    }
}

void SnapshotBar::setPedalCcs (const std::array<int, snapshotxml::maxSnapshots>& ccs)
{
    if (ccs != pedalCcs)
    {
        pedalCcs = ccs;
        repaint();
    }
}

void SnapshotBar::setCaptionOverride (const juce::String& text)
{
    if (text != captionOverride)
    {
        captionOverride = text;
        // The learn caption grows the bar by captionHeight px; keep it
        // centred on the same spot MainComponent laid it out in.
        const int preferred = getPreferredHeight();
        if (getWidth() > 0 && getHeight() != preferred)
        {
            auto bounds = getBounds();
            bounds.setHeight (preferred);
            bounds.setY (bounds.getCentreY() - preferred / 2);
            setBounds (bounds);
        }
        repaint();
    }
}

void SnapshotBar::setDisplayTypeface (juce::Typeface::Ptr typeface)
{
    displayTypeface = std::move (typeface);
    repaint();
}

int SnapshotBar::cellIndexAt (juce::Point<int> position) const
{
    const int top = captionOverride.isNotEmpty() ? captionHeight : 0;
    if (position.y < top)
        return -1; // the caption strip isn't a cell
    const int index = position.x * snapshotxml::maxSnapshots / juce::jmax (1, getWidth());
    return index >= 0 && (index < count || index == addCellIndex()) ? index : -1;
}

void SnapshotBar::mouseDown (const juce::MouseEvent& event)
{
    pressedIndex = cellIndexAt (event.getPosition());
    longPressFired = false;
    startTimer (longPressMs);
}

void SnapshotBar::timerCallback()
{
    stopTimer();
    if (pressedIndex >= 0 && isMouseButtonDown() && cellIndexAt (getMouseXYRelative()) == pressedIndex)
    {
        longPressFired = true;
        if (pressedIndex == addCellIndex())
        {
            if (onAddLongPressed)
                onAddLongPressed();
        }
        else if (onSnapshotLongPressed)
            onSnapshotLongPressed (pressedIndex);
    }
}

void SnapshotBar::mouseUp (const juce::MouseEvent& event)
{
    stopTimer();
    if (longPressFired)
        return;

    if (! event.mouseWasClicked())
        return;
    const int index = cellIndexAt (event.getPosition());
    if (index >= 0 && index == addCellIndex())
    {
        if (onAddPressed)
            onAddPressed();
    }
    else if (index >= 0 && onSnapshotSelected)
        onSnapshotSelected (index);
}

void SnapshotBar::paint (juce::Graphics& g)
{
    const auto& t = theme::current();

    // Only the transient MIDI-learn caption gets a strip now -- the
    // permanent "tap -- hold" legend was retired in the redesign (the
    // gesture is discoverable from the edit menu on long-press).
    const bool learning = captionOverride.isNotEmpty();
    if (learning)
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (captionOverride, 0, 0, getWidth(), captionHeight, juce::Justification::centred);
    }

    const int cellsTop = learning ? captionHeight : 0;
    const float cellWidth = (float) getWidth() / (float) snapshotxml::maxSnapshots;
    const float cellHeight = (float) getHeight() - (float) cellsTop;

    const bool filledStyle = t.blockStyle == theme::BlockStyle::filled;
    const bool stripeStyle = t.blockStyle == theme::BlockStyle::topStripe;

    if (const int add = addCellIndex(); add >= 0)
    {
        const auto cell = juce::Rectangle<float> ((float) add * cellWidth, (float) cellsTop, cellWidth, cellHeight)
                              .reduced (2.0f);
        g.setColour (t.textDim);
        const float dashes[] = { 4.0f, 3.0f };
        juce::Path outline;
        outline.addRoundedRectangle (cell.reduced (0.5f), t.smallRadius);
        juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
        const auto c = cell.getCentre();
        const float arm = juce::jmin (cell.getWidth(), cell.getHeight()) * 0.2f;
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
    }

    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float> ((float) i * cellWidth, (float) cellsTop, cellWidth, cellHeight)
                        .reduced (2.0f);

        const auto colour = cellColours[(size_t) i].isTransparent() ? snapshotxml::defaultColourFor (i) : cellColours[(size_t) i];
        const bool on = i == current;
        juce::Colour ink = juce::Colours::grey;

        if (on && filledStyle)
        {
            // Bold Blocks: the cell IS the snapshot colour.
            g.setColour (colour);
            g.fillRoundedRectangle (cell, t.radius);
            ink = colour.contrasting (juce::Colours::black, juce::Colours::white);
            if (t.blockStyle == theme::BlockStyle::glow)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.drawRoundedRectangle (cell.expanded (3.0f), t.radius + 3.0f, 5.0f);
            }
        }
        else if (on && stripeStyle)
        {
            // Console: raised-high card with the snapshot colour as border.
            g.setColour (t.raisedHigh);
            g.fillRoundedRectangle (cell, t.smallRadius);
            g.setColour (colour);
            g.drawRoundedRectangle (cell, t.smallRadius, 2.0f);
            ink = t.text;
        }
        else if (on)
        {
            // Stage Glass: colour fill + glow, same as Bold's generic rule.
            g.setColour (colour);
            g.fillRoundedRectangle (cell, t.radius);
            ink = colour.contrasting (juce::Colours::black, juce::Colours::white);
            g.setColour (colour.withAlpha (0.3f));
            g.drawRoundedRectangle (cell.expanded (3.0f), t.radius + 3.0f, 5.0f);
        }
        else
        {
            g.setColour (t.raised);
            g.fillRoundedRectangle (cell, filledStyle ? t.radius : (stripeStyle ? t.smallRadius : t.radius));
            g.setColour (t.line);
            g.drawRoundedRectangle (cell.reduced (0.5f), filledStyle ? t.radius : (stripeStyle ? t.smallRadius : t.radius), 1.0f);

            if (stripeStyle)
            {
                // Console: the slot colour as a stripe on top of the idle cell.
                juce::Path clip;
                clip.addRoundedRectangle (cell, t.smallRadius);
                g.saveState();
                g.reduceClipRegion (clip);
                g.setColour (colour);
                g.fillRect (cell.getX(), cell.getY(), cell.getWidth(), 4.0f);
                g.restoreState();
            }
            else
            {
                // Stage Glass / Bold Blocks: a hint of the slot colour as a
                // bar along the bottom edge.
                g.setColour (colour.withAlpha (0.7f));
                g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() - 5.0f).withTrimmedLeft (8.0f).withTrimmedRight (8.0f).reduced (0.0f, 1.0f), 1.5f);
            }
            ink = t.textMuted;
        }

        if (pedalCcs[(size_t) i] >= 0)
        {
            g.setColour (ink.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("CC" + juce::String (pedalCcs[(size_t) i]), cell.reduced (4.0f, 2.0f),
                        juce::Justification::topRight);
        }

        // Name lives inside the cell now (redesign card 2): small line at
        // the bottom, letter above it.
        const auto& label = labels[(size_t) i];
        auto letterArea = cell.reduced (3.0f);
        if (label.isNotEmpty())
        {
            g.setColour (ink);
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (label, letterArea.removeFromBottom (12.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
            letterArea = cell.reduced (3.0f);
        }

        g.setColour (ink);
        const auto fontHeight = letterArea.getHeight() * (label.isNotEmpty() ? 0.62f : 0.55f);
        g.setFont (displayTypeface != nullptr
                       ? juce::Font (juce::FontOptions (fontHeight).withTypeface (displayTypeface))
                       : juce::Font (juce::FontOptions (fontHeight, juce::Font::bold)));
        g.drawText (snapshotxml::letterFor (i), letterArea, juce::Justification::centred);
    }
}

} // namespace openguitarmultifx
