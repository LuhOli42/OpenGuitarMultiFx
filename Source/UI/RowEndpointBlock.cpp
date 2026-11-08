#include "RowEndpointBlock.h"
#include "Theme.h"

namespace openguitarmultifx
{

void RowEndpointBlock::setDisplay (juce::String labelText, juce::String detailText)
{
    label = std::move (labelText);
    detail = std::move (detailText);
    repaint();
}

void RowEndpointBlock::mouseUp (const juce::MouseEvent&)
{
    if (onClicked)
        onClicked();
}

void RowEndpointBlock::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    auto bounds = getLocalBounds().toFloat().reduced (4.0f);

    if (label.isEmpty() && detail.isEmpty())
    {
        // Unrouted: a "+" you can tap, matching the one the grid shows on
        // hover for an empty cell -- the same "there's something you can
        // add here" language, not a filled-in tile pretending to be set up.
        g.setColour (t.lineStrong.withAlpha (0.7f));
        g.drawRoundedRectangle (bounds, t.radius, 1.5f);

        g.setColour (t.textDim);
        const float arm = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.22f;
        const auto c = bounds.getCentre();
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.5f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.5f);
        return;
    }

    // A plain, borderless pill -- same reference as the effect blocks
    // (Quad Cortex's "In 1" / "Out 1/2" tiles): no outline, no icon, just
    // the label on top and what it's routed to below it.
    g.setColour (theme::current().surface);
    g.fillRoundedRectangle (bounds, theme::current().radius);
    g.setColour (theme::current().lineStrong);
    g.drawRoundedRectangle (bounds, theme::current().radius, 1.0f);

    g.setColour (theme::current().text);
    g.setFont (15.5f);
    g.drawFittedText (label, bounds.reduced (4.0f).toNearestInt(), juce::Justification::centredTop, 1);

    g.setColour (theme::current().textMuted);
    g.setFont (12.5f);
    g.drawFittedText (detail, bounds.reduced (4.0f).toNearestInt(), juce::Justification::centredBottom, 2);
}

} // namespace openguitarmultifx
