#include "ChainContainer.h"
#include "Theme.h"

namespace openguitarmultifx
{
namespace
{
    void drawDashedRoundedRect (juce::Graphics& g, juce::Rectangle<float> bounds, float radius,
                                juce::Colour colour, float thickness)
    {
        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (0.5f), radius);
        const float dashes[] = { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (thickness).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (colour);
        g.fillPath (dashed);
    }
}

void ChainContainer::setBlockBounds (std::vector<juce::Rectangle<float>> bounds)
{
    blockBounds = std::move (bounds);
    occupiedCells.assign ((size_t) columns * (size_t) maxVisibleRows, false);
    for (const auto& b : blockBounds)
    {
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const int col = (int) (cx / (float) (gridBlockWidth + gridColGap));
        const int row = (int) ((cy - (float) gridTopPadding) / (float) (gridBlockHeight + gridRowGap));
        if (col >= 0 && col < columns && row >= 0 && row < maxVisibleRows)
            occupiedCells[(size_t) row * (size_t) columns + (size_t) col] = true;
    }
    repaint();
}

void ChainContainer::setRowMetrics (int columnsIn, int blockWidthIn, int blockHeightIn, int colGapIn, int rowGapIn, int topPaddingIn)
{
    columns = juce::jmax (1, columnsIn);
    gridBlockWidth = juce::jmax (1, blockWidthIn);
    gridBlockHeight = juce::jmax (1, blockHeightIn);
    gridColGap = juce::jmax (0, colGapIn);
    gridRowGap = juce::jmax (0, rowGapIn);
    gridTopPadding = juce::jmax (0, topPaddingIn);
    occupiedCells.assign ((size_t) columns * (size_t) maxVisibleRows, false);
}

void ChainContainer::setCrossings (std::vector<int> crossingYs)
{
    crossings = std::move (crossingYs);
    repaint();
}

juce::Rectangle<float> ChainContainer::cellBounds (int index) const
{
    const int row = index / columns;
    const int col = index % columns;
    return { (float) col * (float) (gridBlockWidth + gridColGap),
             (float) gridTopPadding + (float) row * (float) (gridBlockHeight + gridRowGap),
             (float) gridBlockWidth, (float) gridBlockHeight };
}

bool ChainContainer::cellOccupied (int index) const
{
    return index >= 0 && index < (int) occupiedCells.size() && occupiedCells[(size_t) index];
}

int ChainContainer::rawIndexForPosition (juce::Point<int> pos) const
{
    const float x = (float) pos.x, y = (float) pos.y;
    const float colPitch = (float) (gridBlockWidth + gridColGap);
    const float rowPitch = (float) (gridBlockHeight + gridRowGap);
    const int col = (int) (x / colPitch);
    const int row = (int) ((y - (float) gridTopPadding) / rowPitch);
    if (col < 0 || col >= columns || row < 0 || row >= maxVisibleRows)
        return -1;
    const int index = row * columns + col;
    const auto cell = cellBounds (index);
    return cell.contains (x, y) ? index : -1;
}

void ChainContainer::paint (juce::Graphics& g)
{
    const auto& t = theme::current();

    for (int row = 0; row < maxVisibleRows; ++row)
    {
        const float y = (float) gridTopPadding + (float) row * (float) (gridBlockHeight + gridRowGap)
                        + (float) gridBlockHeight * 0.5f;
        if (t.blockStyle == theme::BlockStyle::glow)
        {
            juce::ColourGradient gradient (t.lineStrong, 0.0f, y,
                                           t.lineStrong.brighter (0.55f), (float) getWidth(), y, false);
            g.setGradientFill (gradient);
            g.fillRect (0.0f, y - 1.0f, (float) getWidth(), 2.0f);
        }
        else
        {
            g.setColour (t.lineStrong);
            g.drawLine (0.0f, y, (float) getWidth(), y, 2.0f);
        }
    }

    for (int row = 0; row < maxVisibleRows; ++row)
        for (int col = 0; col < columns; ++col)
        {
            const int index = row * columns + col;
            if (cellOccupied (index))
                continue;

            const auto cell = cellBounds (index);
            const bool hover = hoveredIndex == index;
            // Prototype: dashed var(--ln) at opacity .55 -- MULTIPLY the token's own alpha (line is ~7% white in
            // Stage Glass); withAlpha() replaced it and drew the empty grid at 55% white, the loudest thing on screen.
            drawDashedRoundedRect (g, cell, t.radius, hover ? t.lineStrong : t.line.withMultipliedAlpha (0.55f), 1.5f);

            const auto c = cell.getCentre();
            const float arm = 7.0f; // the prototype's 14 px "+" glyph
            const auto ink = hover ? t.textDim : t.textDim.withAlpha (0.55f);
            g.setColour (ink);
            g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.0f);
            g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.0f);
        }

    g.setColour (t.lineStrong);
    for (const int y : crossings)
        g.drawLine ((float) getWidth() - 18.0f, (float) y - 5.0f,
                    (float) getWidth() - 2.0f, (float) y + 5.0f, 2.0f);
}

void ChainContainer::mouseMove (const juce::MouseEvent& event)
{
    const int index = rawIndexForPosition (event.getPosition());
    if (index != hoveredIndex)
    {
        hoveredIndex = index;
        repaint();
    }
}

void ChainContainer::mouseExit (const juce::MouseEvent&)
{
    hoveredIndex = -1;
    repaint();
}

void ChainContainer::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasClicked())
        return;
    const int index = rawIndexForPosition (event.getPosition());
    if (index >= 0 && ! cellOccupied (index) && onSlotClicked)
        onSlotClicked (index);
}

} // namespace openguitarmultifx
