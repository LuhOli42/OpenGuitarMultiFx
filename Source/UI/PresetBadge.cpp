#include "PresetBadge.h"
#include "Theme.h"
#include "OpenGuitarMultiFxLookAndFeel.h"

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

void PresetBadge::setPresetText (juce::String name, juce::String subtitle)
{
    if (presetName != name || presetSubtitle != subtitle)
    {
        presetName = std::move (name);
        presetSubtitle = std::move (subtitle);
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
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool console = t.blockStyle == theme::BlockStyle::topStripe;

    g.setColour (t.raised);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.smallRadius, 1.0f);

    juce::String numberText;
    if (currentNumber > 0)
        numberText = juce::String (currentNumber);
    if (snapshotIndex >= 0)
        numberText += juce::String::charToString ((juce::juce_wchar) ('A' + snapshotIndex));
    if (numberText.isEmpty())
        numberText = "-";

    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    const float tileX = bounds.getX() + 6.0f;
    const float tileY = bounds.getCentreY() - 22.0f;
    auto tile = juce::Rectangle<float> (tileX, tileY, 58.0f, 44.0f);

    if (console)
    {
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (tile, 2.0f);
        g.setColour (accent);
        g.fillRect (tile.getX(), tile.getY() + 2.0f, 4.0f, tile.getHeight() - 4.0f);
    }
    else
    {
        g.setColour (accent);
        g.fillRoundedRectangle (tile, juce::jmax (2.0f, t.smallRadius - 3.0f));
    }

    g.setFont (displayTypeface != nullptr
                   ? juce::Font (juce::FontOptions (22.0f).withTypeface (displayTypeface))
                   : juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.setColour (console ? t.text : juce::Colours::black);
    g.drawText (numberText, tile.toNearestInt(), juce::Justification::centred);

    // Prototype .preset: name (20 px bold, line-height 1.1) and sub (11 px, 2 px gap) centred together as one block;
    // with no subtitle the name alone sits on the centre line instead of hugging the top.
    const auto textArea = bounds.withTrimmedLeft (tile.getRight() - bounds.getX() + 12.0f).withTrimmedRight (14.0f);
    constexpr float nameH = 22.0f, subGap = 2.0f, subH = 14.0f;
    const bool hasSub = presetSubtitle.isNotEmpty();
    const float blockH = nameH + (hasSub ? subGap + subH : 0.0f);
    const float top = textArea.getCentreY() - blockH * 0.5f;

    const auto name = presetName.isNotEmpty() ? presetName : juce::String ("No preset loaded");
    g.setColour (t.text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawFittedText (name, textArea.withY (top).withHeight (nameH).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);

    if (hasSub)
    {
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (presetSubtitle, textArea.withY (top + nameH + subGap).withHeight (subH).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
}

} // namespace openguitarmultifx
