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
    return index >= 0 && index < snapshotxml::maxSnapshots ? index : -1;
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
        if (onSnapshotLongPressed)
            onSnapshotLongPressed (pressedIndex);
    }
}

void SnapshotBar::mouseUp (const juce::MouseEvent& event)
{
    stopTimer();
    if (longPressFired)
        return;

    if (event.mouseWasClicked() && onSnapshotSelected)
        if (const int index = cellIndexAt (event.getPosition()); index >= 0)
            onSnapshotSelected (index);
}

void SnapshotBar::paint (juce::Graphics& g)
{
    // First-run discoverability for the hidden long-press (audit #93 item 3),
    // one dim line over the cells -- gone from muscle-memory territory.
    g.setColour (juce::Colours::grey.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("tap: recall -- hold: edit", getLocalBounds().removeFromTop (hintHeight),
                juce::Justification::centred);

    const float cellsTop = (float) hintHeight;
    const float cellWidth = (float) getWidth() / (float) snapshotxml::maxSnapshots;

    for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
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
