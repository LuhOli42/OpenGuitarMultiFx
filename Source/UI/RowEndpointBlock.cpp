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

    if (unrouted)
    {
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("+", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (t.text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawFittedText (label, bounds.withTrimmedBottom (bounds.getHeight() * 0.45f).toNearestInt(),
                      juce::Justification::centredTop, 1, 0.8f);

    g.setColour (t.textMuted);
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (detail, bounds.withTrimmedTop (bounds.getHeight() * 0.45f).toNearestInt(),
                      juce::Justification::centredBottom, 2, 0.75f);
}

void RowEndpointBlock::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClicked)
        onClicked();
}

} // namespace openguitarmultifx
