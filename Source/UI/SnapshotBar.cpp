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

int SnapshotBar::getPreferredWidth() const noexcept
{
    const int cells = displayCount() + (addCellIndex() >= 0 ? 1 : 0);
    return cells * cellWidth + juce::jmax (0, cells - 1) * cellGap;
}

int SnapshotBar::displayCount() const noexcept
{
    // The prototype always shows six letter cells (i<6 even unnamed) --
    // cells past `count` draw dim and inert. When the top bar can't give
    // every cell the 48px tap floor, drop filler letters first (down to
    // the real count) so real cells and "+" stay touch-sized.
    const int fit = getWidth() > 0 ? juce::jmax (1, (getWidth() + cellGap) / (48 + cellGap)) : 6;
    const int fillerCap = juce::jmax (0, fit - 1);
    return juce::jmin (snapshotxml::maxSnapshots, juce::jmax (count, juce::jmin (6, fillerCap)));
}

float SnapshotBar::effectiveCellWidth() const noexcept
{
    // The bar is given whatever width the top bar can spare; cells scale
    // down from the prototype's 62px to keep every letter reachable.
    const int cells = displayCount() + (addCellIndex() >= 0 ? 1 : 0);
    const float fit = ((float) getWidth() - (float) (cells - 1) * (float) cellGap) / (float) cells;
    return juce::jmin ((float) cellWidth, juce::jmax (40.0f, fit));
}

int SnapshotBar::cellIndexAt (juce::Point<int> position) const
{
    const int top = captionOverride.isNotEmpty() ? captionHeight : 0;
    if (position.y < top)
        return -1; // the caption strip isn't a cell

    const float pitch = effectiveCellWidth() + (float) cellGap;
    const int index = (int) (position.x / pitch);
    if (index < 0 || index >= displayCount() + (addCellIndex() >= 0 ? 1 : 0))
        return -1;
    if (position.x - index * pitch >= effectiveCellWidth())
        return -1;
    // Only real scenes and the "+" cell respond; filler cells are inert.
    return index < count || index == addCellIndex() ? index : -1;
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
    const float cellW = effectiveCellWidth();
    const float pitch = cellW + (float) cellGap;
    const float cellH = (float) SnapshotBar::cellHeight;

    const bool filledStyle = t.blockStyle == theme::BlockStyle::filled;
    const bool stripeStyle = t.blockStyle == theme::BlockStyle::topStripe;

    if (const int add = addCellIndex(); add >= 0)
    {
        const auto cell = juce::Rectangle<float> ((float) add * pitch, (float) cellsTop, cellW, cellH);
        g.setColour (t.raised);
        g.fillRoundedRectangle (cell, t.smallRadius);
        g.setColour (t.line);
        const float dashes[] = { 4.0f, 3.0f };
        juce::Path outline;
        outline.addRoundedRectangle (cell.reduced (0.5f), t.smallRadius);
        juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
        const auto c = cell.getCentre();
        const float arm = juce::jmin (cell.getWidth(), cell.getHeight()) * 0.2f;
        g.setColour (t.textDim);
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
    }

    for (int i = 0; i < displayCount(); ++i)
    {
        auto cell = juce::Rectangle<float> ((float) i * pitch, (float) cellsTop, cellW, cellH);

        const auto colour = cellColours[(size_t) i].isTransparent() ? snapshotxml::defaultColourFor (i) : cellColours[(size_t) i];
        const bool on = i == current && i < count;
        const bool inert = i >= count;
        juce::Colour ink = juce::Colours::grey;

        if (on && filledStyle)
        {
            // Bold Blocks: the cell IS the snapshot colour.
            g.setColour (colour);
            g.fillRoundedRectangle (cell, t.smallRadius);
            ink = theme::inkOn (colour, juce::Colours::black, juce::Colours::white);
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
            // Stage Glass: plain colour fill -- a fake-blur halo pad reads
            // as a displaced shadow, so the glow stays off the snap cells.
            g.setColour (colour);
            g.fillRoundedRectangle (cell, t.smallRadius);
            ink = theme::inkOn (colour, juce::Colours::black, juce::Colours::white);
        }
        else
        {
            g.setColour (inert ? t.raised.withMultipliedAlpha (0.5f) : t.raised);
            g.fillRoundedRectangle (cell, t.smallRadius);
            g.setColour (t.line);
            g.drawRoundedRectangle (cell.reduced (0.5f), t.smallRadius, 1.0f);

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
                g.setColour (colour.withAlpha (inert ? 0.25f : 0.7f));
                g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() - 5.0f).withTrimmedLeft (8.0f).withTrimmedRight (8.0f).reduced (0.0f, 1.0f), 1.5f);
            }
            ink = t.textMuted;
        }

        if (pedalCcs[(size_t) i] >= 0 && ! inert)
        {
            g.setColour (ink.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("CC" + juce::String (pedalCcs[(size_t) i]), cell.reduced (4.0f, 2.0f),
                        juce::Justification::topRight);
        }

        // Prototype .snap: letter 19px extra-bold centred, name 9.5px 3px
        // below it; the colour bar stays along the bottom edge.
        const auto& label = inert ? juce::String() : labels[(size_t) i];
        auto letterArea = cell.reduced (3.0f).withTrimmedBottom (label.isNotEmpty() ? 0.0f : 4.0f);
        if (label.isNotEmpty())
        {
            g.setColour (ink);
            g.setFont (juce::FontOptions (9.5f));
            g.drawFittedText (label, cell.reduced (3.0f).removeFromBottom (14.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.7f);
            letterArea = cell.reduced (3.0f, 4.0f).withTrimmedBottom (10.0f);
        }

        g.setColour (inert ? ink.withAlpha (0.55f) : ink);
        g.setFont (displayTypeface != nullptr
                       ? juce::Font (juce::FontOptions (19.0f).withTypeface (displayTypeface))
                       : juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
        g.drawText (snapshotxml::letterFor (i), letterArea, juce::Justification::centred);
    }
}

} // namespace openguitarmultifx
