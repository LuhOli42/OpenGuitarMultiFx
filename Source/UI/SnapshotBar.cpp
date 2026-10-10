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

void SnapshotBar::setDisplayTypeface (juce::Typeface::Ptr typeface)
{
    displayTypeface = std::move (typeface);
    repaint();
}

int SnapshotBar::cellIndexAt (juce::Point<int> position) const
{
    const int index = position.x * snapshotxml::maxSnapshots / juce::jmax (1, getWidth());
    return index >= 0 && index < snapshotxml::maxSnapshots ? index : -1;
}

void SnapshotBar::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onSnapshotSelected)
        if (const int index = cellIndexAt (event.getPosition()); index >= 0)
            onSnapshotSelected (index);
}

void SnapshotBar::paint (juce::Graphics& g)
{
    const float cellWidth = (float) getWidth() / (float) snapshotxml::maxSnapshots;

    for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
    {
        auto cell = juce::Rectangle<float> ((float) i * cellWidth, 0.0f, cellWidth, (float) getHeight())
                        .reduced (2.0f);

        if (i == current)
        {
            g.setColour (snapshotxml::defaultColourFor (i));
            g.fillRoundedRectangle (cell, 8.0f);
            g.setColour (juce::Colours::black);
        }
        else
        {
            g.setColour (juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (cell, 8.0f);
            g.setColour (juce::Colours::grey);
        }

        const auto fontHeight = cell.getHeight() * 0.5f;
        g.setFont (displayTypeface != nullptr
                       ? juce::Font (juce::FontOptions (fontHeight).withTypeface (displayTypeface))
                       : juce::Font (juce::FontOptions (fontHeight, juce::Font::bold)));
        g.drawText (snapshotxml::letterFor (i), cell, juce::Justification::centred);
    }
}

} // namespace openguitarmultifx
