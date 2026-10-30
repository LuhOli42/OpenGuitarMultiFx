#include "EffectBlockComponent.h"

namespace openguitarmultifx
{

EffectBlockComponent::EffectBlockComponent (EffectProcessor& processorToShow)
    : processor (processorToShow)
{
}

void EffectBlockComponent::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void EffectBlockComponent::setPedal (int cc, bool armed)
{
    if (pedalCc != cc || pedalArmed != armed)
    {
        pedalCc = cc;
        pedalArmed = armed;
        repaint();
    }
}

void EffectBlockComponent::mouseDown (const juce::MouseEvent& event)
{
    dragger.startDraggingComponent (this, event);
    isDragging = false; // only becomes true in mouseDrag, once past a small threshold
    longPressFired = false;
    if (onLongPressed != nullptr)
        startTimer (longPressMs);
}

void EffectBlockComponent::timerCallback()
{
    stopTimer();
    if (! isDragging && isMouseButtonDown() && onLongPressed != nullptr)
    {
        longPressFired = true;
        onLongPressed();
    }
}

void EffectBlockComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (! isDragging && event.getDistanceFromDragStart() > 4)
    {
        stopTimer();
        isDragging = true;
        toFront (true); // stay above sibling blocks while dragging over them
    }

    if (isDragging)
        dragger.dragComponent (this, event, nullptr); // free 2D movement -- the chain now wraps into rows
}

void EffectBlockComponent::mouseUp (const juce::MouseEvent&)
{
    stopTimer();
    if (longPressFired)
    {
        longPressFired = false;
        return;
    }

    if (isDragging)
    {
        isDragging = false;
        if (onDragEnded != nullptr)
            onDragEnded (*this);
    }
    else if (onClicked != nullptr)
    {
        onClicked();
    }
}

void EffectBlockComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (3.0f);
    const bool bypassed = processor.isBypassed();
    const auto accent = processor.getAccentColour();

    if (bypassed)
    {
        // Deactivated -- flat grey, no category colour at all.
        g.setColour (juce::Colour (0xff2e2e2e));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (juce::Colours::grey.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.5f);
    }
    else
    {
        // Active -- black by default, with a strong outline in the block's
        // own category colour (see reference: Quad Cortex's Grid). The one
        // block currently open in the parameter panel below gets a light
        // glaze of that same colour so it's obvious which one you're
        // editing, without needing a separate unrelated highlight colour.
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (bounds, 8.0f);

        if (selected)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.fillRoundedRectangle (bounds, 8.0f);
        }

        g.setColour (accent);
        g.drawRoundedRectangle (bounds, 8.0f, selected ? 3.0f : 2.0f);
    }

    // Icon on top, name label along the bottom -- same two-zone layout as
    // the reference (Quad Cortex's Grid blocks): glyph first, text second.
    auto iconArea = bounds.reduced (6.0f);
    auto nameArea = iconArea.removeFromBottom (26.0f);
    iconArea.removeFromBottom (2.0f);

    // The block itself isn't square (110x78) but every drawIcon() is drawn
    // assuming one -- reducing iconArea by different width/height factors
    // used to hand it a stretched rectangle instead, so every icon that
    // didn't defensively jmin(width,height) internally came out visibly
    // squashed. Always hand drawIcon() a proper centred square instead, so
    // no individual icon implementation has to guard against this itself.
    const float squareSize = juce::jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.82f;
    const auto squareIconArea = iconArea.withSizeKeepingCentre (squareSize, squareSize);

    if (bypassed)
        g.beginTransparencyLayer (0.4f);

    processor.drawIcon (g, squareIconArea);

    if (bypassed)
        g.endTransparencyLayer();

    g.setColour (juce::Colours::white.withAlpha (bypassed ? 0.5f : 1.0f));
    g.setFont (18.0f);
    g.drawFittedText (processor.getName(), nameArea.toNearestInt(), juce::Justification::centred, 2);

    if (pedalCc >= 0 || pedalArmed)
    {
        g.setColour (pedalArmed ? juce::Colours::orange : juce::Colours::white.withAlpha (bypassed ? 0.5f : 0.8f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (pedalArmed ? juce::String ("...") : "CC" + juce::String (pedalCc),
                    bounds.reduced (5.0f, 3.0f).toNearestInt(), juce::Justification::topRight);
    }
}

} // namespace openguitarmultifx
