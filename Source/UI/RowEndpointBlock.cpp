#include "RowEndpointBlock.h"
#include "Theme.h"
#include "TouchSizing.h"

namespace openguitarmultifx
{

void RowEndpointBlock::setDisplay (juce::String labelIn, juce::String detailIn)
{
    if (label != labelIn || detail != detailIn)
    {
        label = std::move (labelIn);
        detail = std::move (detailIn);
        repaint();
    }
}

void RowEndpointBlock::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool unrouted = label.isEmpty() && detail.isEmpty();

    g.setColour (t.surface);
    g.fillRoundedRectangle (bounds, t.radius);
    g.setColour (t.lineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.radius, 1.0f);

    // Prototype .ep: a bold 13 px label over an 11 px muted detail, the pair centred as one block (gap 4).
    // An unrouted end reads "--" / "Livre" rather than a bare "+": still tappable, but it says what it is.
    const auto shownLabel = unrouted ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : label;
    const auto shownDetail = unrouted ? unroutedDetail : detail;
    constexpr float labelH = 16.0f, detailH = 14.0f, gap = 4.0f;
    const bool hasDetail = shownDetail.isNotEmpty();
    const float blockH = labelH + (hasDetail ? gap + detailH : 0.0f);
    const float top = bounds.getCentreY() - blockH * 0.5f;

    g.setColour (t.text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (shownLabel, juce::Rectangle<float> (bounds.getX() + 4.0f, top, bounds.getWidth() - 8.0f, labelH).toNearestInt(),
                      juce::Justification::centred, 1, 0.8f);

    if (hasDetail)
    {
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (shownDetail, juce::Rectangle<float> (bounds.getX() + 4.0f, top + labelH + gap, bounds.getWidth() - 8.0f, detailH).toNearestInt(),
                          juce::Justification::centred, 1, 0.75f);
    }
}

void RowEndpointBlock::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClicked)
        onClicked();
}

} // namespace openguitarmultifx
