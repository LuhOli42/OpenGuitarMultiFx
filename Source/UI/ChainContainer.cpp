#include "ChainContainer.h"

#include "OpenGuitarMultiFxLookAndFeel.h"

namespace openguitarmultifx
{

void ChainContainer::setBlockBounds (std::vector<juce::Rectangle<float>> bounds)
{
    blockBounds = std::move (bounds);
    repaint();
}

void ChainContainer::setCrossings (std::vector<int> crossingYs)
{
    crossings = std::move (crossingYs);
    repaint();
}

void ChainContainer::setRowMetrics (int columnsIn, int blockWidthIn, int blockHeightIn, int colGapIn, int rowGapIn)
{
    columns = juce::jmax (1, columnsIn);
    gridBlockWidth = juce::jmax (1, blockWidthIn);
    gridBlockHeight = juce::jmax (1, blockHeightIn);
    gridColGap = colGapIn;
    gridRowGap = rowGapIn;
}

int ChainContainer::rawIndexForPosition (juce::Point<int> pos) const
{
    const int row = juce::jlimit (0, maxVisibleRows - 1, pos.y / (gridBlockHeight + gridRowGap));
    const int col = juce::jlimit (0, columns - 1, pos.x / (gridBlockWidth + gridColGap));
    return row * columns + col;
}

juce::Rectangle<float> ChainContainer::cellBounds (int index) const
{
    const int row = index / columns;
    const int col = index % columns;
    return { (float) (col * (gridBlockWidth + gridColGap)), (float) (row * (gridBlockHeight + gridRowGap)),
             (float) gridBlockWidth, (float) gridBlockHeight };
}

void ChainContainer::mouseMove (const juce::MouseEvent& event)
{
    const int newHover = rawIndexForPosition (event.getPosition());
    if (newHover != hoveredIndex)
    {
        hoveredIndex = newHover;
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
    // No clamping here any more -- an occupied cell is never reachable in
    // the first place (the real block sitting on it intercepts the click
    // before this container sees it), so whatever cell this resolves to is
    // always a genuinely empty one. See the class doc comment.
    if (onSlotClicked)
        onSlotClicked (rawIndexForPosition (event.getPosition()));
}

void ChainContainer::paint (juce::Graphics& g)
{
    // The signal path, always on screen -- not just gaps between blocks.
    // A plain Component's paint() runs before its children's, so this sits
    // behind the blocks; they visually sit ON it, matching the reference
    // UI's always-visible input-to-output line. One per row now that the
    // chain wraps instead of scrolling sideways. usedRows comes from
    // MainComponent (see setUsedRows()'s comment) rather than being
    // derived from blockBounds.size() here, since a block's position can
    // now be sparse (any grid cell, not just the next sequential one).
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    for (int row = 0; row < usedRows; ++row)
    {
        const float y = row * (float) (gridBlockHeight + gridRowGap) + (float) gridBlockHeight * 0.5f;
        g.drawLine (0.0f, y, (float) getWidth(), y, 2.0f);

    }

    // Row-to-row connector crossings. Spans this container's FULL width
    // (edge to edge) so it lines up exactly with the two stubs
    // MainComponent::paint() draws in the gutters just outside this
    // container on either side -- the three segments together read as ONE
    // continuous line from the end of one row to the start of another, per
    // user correction 2026-09-10 ("as linhas se conectarem do final com o
    // início da próxima"). Which rows are linked (if any) is the per-row
    // routing's business, not something derivable here -- rows are
    // independent until explicitly connected.
    for (const int y : crossings)
        g.drawLine (0.0f, (float) y, (float) getWidth(), (float) y, 2.0f);

    // Rows past the real content: a plain grid guide line, same as above
    // but with no connecting seam (nothing actually flows into unused
    // capacity) -- reads as "the grid continues here" without claiming a
    // signal path that doesn't exist. Per user request 2026-09-10 (the
    // full row capacity should read as "configured" from the moment the
    // app opens).
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    for (int row = usedRows; row < maxVisibleRows; ++row)
    {
        const float y = row * (float) (gridBlockHeight + gridRowGap) + (float) gridBlockHeight * 0.5f;
        g.drawLine (0.0f, y, (float) getWidth(), y, 2.0f);
    }

    // Empty chain: on a touch device there is no hover, so the "+" affordance
    // below never appears and a first-run screen reads as four lines and
    // nothing to do. A one-line hint on row 0 (the only row that's wired) says
    // how to start; it disappears as soon as the first block lands.
    if (blockBounds.empty() && usedRows > 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (17.0f);
        g.drawText ("Tap a line to add an effect", 0, 0, getWidth(), gridBlockHeight,
                    juce::Justification::centred);
    }

    // The ONE "+" -- exactly under the cursor's grid cell, only while
    // hovering, instead of a permanent dashed-box tile at every open slot.
    // Per user request 2026-09-10 ("tire todo esses + com caixinha
    // quadrada... quando eu passar o mouse em cima da linha, vai aparecer
    // esse +").
    if (hoveredIndex >= 0)
    {
        const auto bounds = cellBounds (hoveredIndex).reduced (4.0f);
        const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();

        g.setColour (accent.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.5f);

        g.setColour (juce::Colours::white);
        const float plusSize = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.3f;
        const auto c = bounds.getCentre();
        g.drawLine (c.x - plusSize * 0.5f, c.y, c.x + plusSize * 0.5f, c.y, 2.5f);
        g.drawLine (c.x, c.y - plusSize * 0.5f, c.x, c.y + plusSize * 0.5f, 2.5f);
    }
}

} // namespace openguitarmultifx
