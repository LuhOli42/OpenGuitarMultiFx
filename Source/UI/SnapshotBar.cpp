#include "SnapshotBar.h"

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
    if (position.y < hintHeight)
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
    // First-run discoverability for the hidden long-press (audit #93 item 3),
    // one dim line over the cells -- gone from muscle-memory territory.
    const bool learning = captionOverride.isNotEmpty();
    g.setColour (learning ? juce::Colours::orange : juce::Colours::grey.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (learning ? captionOverride : juce::String (count < snapshotxml::maxSnapshots ? "tap: recall -- hold: edit -- +: new scene"
                                                                                   : "tap: recall -- hold: edit"),
                getLocalBounds().removeFromTop (hintHeight), juce::Justification::centred);

    const float cellsTop = (float) hintHeight;
    const float cellWidth = (float) getWidth() / (float) snapshotxml::maxSnapshots;

    if (const int add = addCellIndex(); add >= 0)
    {
        const auto cell = juce::Rectangle<float> ((float) add * cellWidth, cellsTop, cellWidth,
                                                  (float) getHeight() - cellsTop)
                              .reduced (2.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.6f));
        const float dashes[] = { 4.0f, 3.0f };
        juce::Path outline;
        outline.addRoundedRectangle (cell.reduced (0.5f), 8.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
        const auto c = cell.getCentre();
        const float arm = juce::jmin (cell.getWidth(), cell.getHeight()) * 0.2f;
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
    }

    for (int i = 0; i < count; ++i)
    {
        auto cell = juce::Rectangle<float> ((float) i * cellWidth, cellsTop, cellWidth,
                                            (float) getHeight() - cellsTop)
                        .reduced (2.0f);

        const auto colour = cellColours[(size_t) i].isTransparent() ? snapshotxml::defaultColourFor (i) : cellColours[(size_t) i];
        auto textColour = juce::Colours::grey;
        if (i == current)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (cell, 8.0f);
            // Custom colours can be dark enough to need white text.
            textColour = colour.contrasting (juce::Colours::black, juce::Colours::white);
        }
        else
        {
            g.setColour (juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (cell, 8.0f);
            g.setColour (colour.withAlpha (0.8f)); // a hint of each slot's colour on the idle cells too
            g.drawRoundedRectangle (cell.reduced (0.5f), 8.0f, 1.0f);
        }
        g.setColour (textColour);

        if (pedalCcs[(size_t) i] >= 0)
        {
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("CC" + juce::String (pedalCcs[(size_t) i]), cell.reduced (4.0f, 2.0f),
                        juce::Justification::topRight);
        }

        const auto& label = labels[(size_t) i];
        auto letterArea = cell;
        if (label.isNotEmpty())
        {
            g.setFont (juce::FontOptions (10.0f));
            g.drawFittedText (label, cell.removeFromBottom (13.0f).toNearestInt().reduced (2, 0),
                              juce::Justification::centred, 1, 0.7f);
            letterArea = cell;
        }

        const auto fontHeight = letterArea.getHeight() * (label.isNotEmpty() ? 0.6f : 0.5f);
        g.setFont (displayTypeface != nullptr
                       ? juce::Font (juce::FontOptions (fontHeight).withTypeface (displayTypeface))
                       : juce::Font (juce::FontOptions (fontHeight, juce::Font::bold)));
        g.drawText (snapshotxml::letterFor (i), letterArea, juce::Justification::centred);
    }
}

} // namespace openguitarmultifx
