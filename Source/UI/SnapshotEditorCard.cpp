#include "SnapshotEditorCard.h"

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
        toggleButton.setButtonText ("Cancel");
    }
    else if (cc >= 0)
    {
        statusLabel.setText ("CC " + juce::String (cc), juce::dontSendNotification);
        toggleButton.setButtonText ("Clear");
    }
    else
    {
        statusLabel.setText ("Not learned", juce::dontSendNotification);
        toggleButton.setButtonText ("Learn");
    }

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
                22.0f, true, theme::current().text);
    addAndMakeVisible (titleLabel);

    closeButton.onClick = [this] { if (actions.onClose) actions.onClose(); };
    addAndMakeVisible (closeButton);

    styleLabel (nameLabel, "Name", 15.0f, false, theme::current().textMuted);
    addAndMakeVisible (nameLabel);

    nameEditor.setText (n, juce::dontSendNotification);
    nameEditor.setFont (fontFor (17.0f));
    nameEditor.setColour (juce::TextEditor::textColourId, theme::current().text);
    nameEditor.setColour (juce::TextEditor::outlineColourId, theme::current().lineStrong);
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
    const auto& t = theme::current();
    g.fillAll (t.surface);

    // Snapshot letter chip, tinted with the snapshot's own colour (layer 2).
    g.setColour (currentColour.withAlpha (usingDefaultColour ? 0.25f : 0.55f));
    g.fillRoundedRectangle (juce::Rectangle<int> (16, 14, 56, 56).toFloat(), t.smallRadius);
    g.setColour (t.text);
    g.setFont (fontFor (28.0f, true));
    g.drawText (letter, 16, 14, 56, 56, juce::Justification::centred);
}

void SnapshotEditorCard::resized()
{
    // Header: chip occupies (16,14,56,56); title and close sit beside it.
    titleLabel.setBounds (88, 14, getWidth() - 88 - margin - touch::minTapTarget - 12, 56);
    closeButton.setBounds (getWidth() - margin - touch::minTapTarget, 14,
                           touch::minTapTarget, touch::minTapTarget);

    // Name row.
    nameLabel.setBounds (margin, 92, 60, 24);
    nameEditor.setBounds (88, 116, getWidth() - 88 - margin - 108, 48);
    nameOkButton.setBounds (getWidth() - margin - 96, 116, 96, touch::minTapTarget);

    // Colour row: label, then swatches + custom pill.
    colourLabel.setBounds (margin, 188, 60, 24);
    int x = margin;
    const int swatchY = 216;
    for (auto* swatch : swatches)
    {
        swatch->setBounds (x, swatchY, swatchSize, swatchSize);
        x += swatchSize + rowGap;
    }
    x += 8;
    customButton.setBounds (x, swatchY + 4, 130, touch::minTapTarget);

    // Footswitch rows.
    int y = 284;
    for (auto* row : pedalRows)
    {
        row->setBounds (margin, y, getWidth() - 2 * margin, 56);
        y += 56 + rowGap;
    }

    // Actions.
    const int actionY = y + 8;
    const int btnW = 170;
    copyToButton.setBounds (margin, actionY, btnW, touch::minTapTarget);
    copyFromButton.setBounds (margin + btnW + 12, actionY, btnW, touch::minTapTarget);
    removeButton.setBounds (margin + 2 * (btnW + 12), actionY, btnW, touch::minTapTarget);
}

} // namespace openguitarmultifx
