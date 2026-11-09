#include "SnapshotEditorCard.h"

#include "OverlayChrome.h"
#include "SnapshotPresetXml.h"
#include "Theme.h"
#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
constexpr int cardWidth = 640;
constexpr int cardHeight = 560;
constexpr int swatchSize = 48;
constexpr int rowGap = 8;
constexpr int margin = 20;

juce::Font fontFor (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

void styleLabel (juce::Label& label, juce::String text, float size, bool bold, juce::Colour colour)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (fontFor (size, bold));
    label.setColour (juce::Label::textColourId, colour);
}
} // namespace

SnapshotEditorCard::ColourSwatch::ColourSwatch (juce::Colour c, bool sel, std::function<void()> cb)
    : colour (c), selected (sel), onClick (std::move (cb))
{
    setSize (swatchSize, swatchSize);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SnapshotEditorCard::ColourSwatch::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (colour);
    g.fillRoundedRectangle (area, t.smallRadius);

    g.setColour (selected ? t.text : t.lineStrong);
    g.drawRoundedRectangle (area.reduced (selected ? 2.0f : 0.0f), t.smallRadius,
                            selected ? 2.0f : 1.0f);
}

void SnapshotEditorCard::ColourSwatch::mouseUp (const juce::MouseEvent&)
{
    if (onClick) onClick();
}

SnapshotEditorCard::FootswitchRow::FootswitchRow (juce::String t, int a, Actions& act)
    : title (std::move (t)), action (a), actions (act)
{
    styleLabel (statusLabel, "", 15.0f, false, theme::current().textMuted);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (toggleButton);

    toggleButton.onClick = [this]
    {
        if (actions.onTogglePedal) actions.onTogglePedal (action);
        refresh();
    };

    refresh();
}

void SnapshotEditorCard::FootswitchRow::refresh()
{
    const int cc = actions.ccFor ? actions.ccFor (action) : -1;
    const bool armed = actions.pedalArmed && actions.pedalArmed (action);

    if (armed)
    {
        statusLabel.setText ("Move the pedal...", juce::dontSendNotification);
        toggleButton.text = "Cancel";
    }
    else if (cc >= 0)
    {
        statusLabel.setText ("CC " + juce::String (cc), juce::dontSendNotification);
        toggleButton.text = "Clear";
    }
    else
    {
        statusLabel.setText ("Not learned", juce::dontSendNotification);
        toggleButton.text = "Learn";
    }

    toggleButton.repaint();
    repaint();
}

void SnapshotEditorCard::FootswitchRow::resized()
{
    auto area = getLocalBounds();
    toggleButton.setBounds (area.removeFromRight (140).reduced (0, 4));
    area.removeFromRight (12);
    statusLabel.setBounds (area.removeFromRight (220).reduced (0, 4));
}

void SnapshotEditorCard::FootswitchRow::paint (juce::Graphics& g)
{
    g.setColour (theme::current().text);
    g.setFont (fontFor (16.0f, true));
    g.drawText (title, getLocalBounds().removeFromLeft (150).reduced (2, 0),
                juce::Justification::centredLeft);
}

SnapshotEditorCard::SnapshotEditorCard (int idx, juce::String l, juce::String n,
                                        juce::Colour col, bool defaultCol, Actions act)
    : index (idx), letter (std::move (l)), actions (std::move (act)),
      currentColour (col), usingDefaultColour (defaultCol)
{
    setSize (cardWidth, cardHeight);

    styleLabel (titleLabel, "Snapshot " + letter + (n.isNotEmpty() ? "  -  " + n : ""),
                24.0f, true, theme::current().text);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    closeButton.onClick = [this] { if (actions.onClose) actions.onClose(); };
    addAndMakeVisible (closeButton);

    styleLabel (nameLabel, "Name", 15.0f, false, theme::current().textMuted);
    addAndMakeVisible (nameLabel);

    nameEditor.setText (n, juce::dontSendNotification);
    nameEditor.setFont (fontFor (17.0f));
    nameEditor.setColour (juce::TextEditor::textColourId, theme::current().text);
    nameEditor.setColour (juce::TextEditor::outlineColourId, theme::current().lineStrong);
    overlaychrome::drawSearchFieldChrome (nameEditor);
    nameEditor.onReturnKey = [this] { commitName(); };
    nameEditor.onFocusLost = [this] { commitName(); };
    addAndMakeVisible (nameEditor);

    nameOkButton.onClick = [this]
    {
        commitName();
        nameEditor.giveAwayKeyboardFocus();
    };
    addAndMakeVisible (nameOkButton);

    styleLabel (colourLabel, "Colour", 15.0f, false, theme::current().textMuted);
    addAndMakeVisible (colourLabel);

    customButton.onClick = [this] { if (actions.onCustomColour) actions.onCustomColour(); };
    addAndMakeVisible (customButton);

    rebuildSwatches();
    rebuildPedalRows();

    copyToButton.onClick = [this] { if (actions.onCopyTo) actions.onCopyTo (index); };
    addAndMakeVisible (copyToButton);

    copyFromButton.onClick = [this] { if (actions.onCopyFrom) actions.onCopyFrom (index); };
    addAndMakeVisible (copyFromButton);

    removeButton.onClick = [this] { if (actions.onRemove) actions.onRemove(); };
    addAndMakeVisible (removeButton);
}

void SnapshotEditorCard::commitName()
{
    if (actions.onNameChanged)
        actions.onNameChanged (nameEditor.getText().trim());
}

void SnapshotEditorCard::rebuildSwatches()
{
    swatches.clear();
    for (int i = 0; i < 8; ++i)
    {
        const auto colour = snapshotxml::defaultColourFor (i);
        auto* swatch = swatches.add (new ColourSwatch (colour, currentColour == colour,
            [this, colour]
            {
                if (actions.onColourChosen) actions.onColourChosen (colour);
            }));
        addAndMakeVisible (*swatch);
    }
}

void SnapshotEditorCard::rebuildPedalRows()
{
    pedalRows.clear();
    const char* titles[] = { "This scene", "Next scene", "Previous scene" };
    const int actionIds[] = { index, -1, -2 };
    for (int i = 0; i < 3; ++i)
    {
        auto* row = pedalRows.add (new FootswitchRow (titles[i], actionIds[i], actions));
        row->setBounds (0, 0, 600, 56);
        addAndMakeVisible (*row);
    }
}

void SnapshotEditorCard::paint (juce::Graphics& g)
{
    overlaychrome::paintCardBackground (g, getLocalBounds().toFloat());
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), 76.0f));

    const auto& t = theme::current();
    g.setColour (t.line);
    for (float y : { 152.0f, 236.0f, 316.0f, 396.0f })
        g.drawHorizontalLine (y, margin, (float) getWidth() - margin);

    // Snapshot letter chip, tinted with the snapshot's own colour (layer 2).
    g.setColour (currentColour.withAlpha (usingDefaultColour ? 0.35f : 0.75f));
    g.fillRoundedRectangle (juce::Rectangle<float> (20, 12, 56, 52), t.smallRadius);
    g.setColour (t.text);
    g.setFont (fontFor (26.0f, true));
    g.drawText (letter, 20, 12, 56, 52, juce::Justification::centred);
}

void SnapshotEditorCard::resized()
{
    titleLabel.setBounds (88, 0, getWidth() - 88 - margin - 52 - 12, 76);
    closeButton.setBounds (getWidth() - margin - 52, 12, 52, 52);

    nameLabel.setBounds (margin, 88, 96, 24);
    nameEditor.setBounds (120, 82, getWidth() - 120 - margin - 92, 48);
    nameOkButton.setBounds (getWidth() - margin - 84, 82, 84, touch::minTapTarget);

    colourLabel.setBounds (margin, 164, 96, 24);
    int x = margin;
    const int swatchY = 196;
    for (auto* swatch : swatches)
    {
        swatch->setBounds (x, swatchY, swatchSize, swatchSize);
        x += swatchSize + 8;
    }
    x += 8;
    customButton.setBounds (x, swatchY + 2, 130, touch::minTapTarget);

    int y = 268;
    for (auto* row : pedalRows)
    {
        row->setBounds (margin, y, getWidth() - 2 * margin, 56);
        y += 56 + 8;
    }

    const int actionY = 452;
    const int btnW = 168;
    copyToButton.setBounds (margin, actionY, btnW, touch::minTapTarget);
    copyFromButton.setBounds (margin + btnW + 12, actionY, btnW, touch::minTapTarget);
    removeButton.setBounds (margin + 2 * (btnW + 12), actionY, btnW, touch::minTapTarget);
}

} // namespace openguitarmultifx
