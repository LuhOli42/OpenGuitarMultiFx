#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/** Grid wires, empty dashed slots and row-to-row crossings. The whole grid
    is clickable: ChainContainer resolves the cell and MainComponent adds the
    effect there. Real blocks are children painted over this. */
class ChainContainer : public juce::Component
{
public:
    void setBlockBounds (std::vector<juce::Rectangle<float>> bounds);

    /** columns/inset/pitch come from MainComponent::layoutChain(); topPadding
        is the prototype's first-lane offset inside the grid. */
    void setRowMetrics (int columnsIn, int blockWidthIn, int blockHeightIn, int colGapIn, int rowGapIn, int topPaddingIn = 0);

    void setMaxVisibleRows (int rows) { maxVisibleRows = juce::jmax (1, rows); }
    void setCrossings (std::vector<int> crossingYs);
    void setUsedRows (int rows) { usedRows = juce::jlimit (1, maxVisibleRows, rows); }

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& event) override;

    std::function<void (int gridSlot)> onSlotClicked;

private:
    std::vector<juce::Rectangle<float>> blockBounds;
    std::vector<bool> occupiedCells;
    int columns = 1;
    int gridBlockWidth = 1, gridBlockHeight = 1, gridColGap = 0, gridRowGap = 0, gridTopPadding = 0;
    int maxVisibleRows = 4;
    int usedRows = 1;
    std::vector<int> crossings;
    int hoveredIndex = -1;

    int rawIndexForPosition (juce::Point<int> pos) const;
    juce::Rectangle<float> cellBounds (int index) const;
    bool cellOccupied (int index) const;
};

} // namespace openguitarmultifx
