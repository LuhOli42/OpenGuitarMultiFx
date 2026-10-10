#include "PresetBadge.h"

namespace openguitarmultifx
{

void PresetBadge::setNumber (int number)
{
    if (currentNumber != number)
    {
        currentNumber = number;
        repaint();
    }
}

void PresetBadge::setSnapshotIndex (int index)
{
    index = index >= 0 && index < 8 ? index : -1;
    if (snapshotIndex != index)
    {
        snapshotIndex = index;
        repaint();
    }
}

void PresetBadge::setDisplayTypeface (juce::Typeface::Ptr typeface)
{
    displayTypeface = std::move (typeface);
    repaint();
}

void PresetBadge::mouseUp (const juce::MouseEvent& event)
{
    if (event.mouseWasClicked() && onClicked)
        onClicked();
}

void PresetBadge::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillRoundedRectangle (bounds, 10.0f);

    juce::String text;
    if (currentNumber > 0)
        text = juce::String (currentNumber);
    if (snapshotIndex >= 0)
        text += juce::String::charToString ((juce::juce_wchar) ('A' + snapshotIndex));
    if (text.isEmpty())
        text = juce::String::fromUTF8 ("\xe2\x80\x94");

    // The number used to be a single glyph; two ("3B") or three ("12B")
    // characters need progressively smaller type to fit the same slot.
    const auto fontScale = text.length() <= 1 ? 0.6f : (text.length() == 2 ? 0.5f : 0.42f);
    auto font = displayTypeface != nullptr
                    ? juce::Font (juce::FontOptions (bounds.getHeight() * fontScale).withTypeface (displayTypeface))
                    : juce::Font (juce::FontOptions (bounds.getHeight() * fontScale, juce::Font::bold));
    g.setFont (font);

    g.setColour (currentNumber > 0 || snapshotIndex >= 0 ? juce::Colours::white : juce::Colours::grey);
    g.drawText (text, bounds, juce::Justification::centred);
}

} // namespace openguitarmultifx
