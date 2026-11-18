#include "SnapshotEditorCard.h"
#include "MidiSourceCc.h"

#include "OverlayChrome.h"
#include "SnapshotPresetXml.h"
#include "Theme.h"
#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
constexpr int headerHeight = 76;
constexpr int footerHeight = 80;
constexpr int margin = 24;
constexpr int keyWidth = 170;
constexpr int contentX = margin + keyWidth;
constexpr int swatchSize = 48;
constexpr int swatchGap = 6;
constexpr int chipWidth = 64;
constexpr int chipGap = 8;
constexpr int pedalRowHeight = 52;
constexpr int confirmMs = 3000; // an armed two-tap action falls back after this

using overlaychrome::font;

// Every closing action pops (destroys) this card, its buttons and the Actions holding the callback while the
// callback still runs: call a stack copy, like ChoiceCard does, and touch nothing of the card afterwards.
void callDetached (const std::function<void()>& f)
{
    if (auto copy = f)
        copy();
}
} // namespace

// ---------------------------------------------------------------------------

SnapshotEditorCard::ColourSwatch::ColourSwatch (juce::Colour c, std::function<void()> cb)
    : colour (c), onClick (std::move (cb))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SnapshotEditorCard::ColourSwatch::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    auto area = getLocalBounds().toFloat();

    if (selected)
    {
        // Prototype ring: a gap in the card colour, then a text-coloured ring.
        g.setColour (t.text);
        g.fillRoundedRectangle (area, t.smallRadius + 3.0f);
        g.setColour (t.surface);
        g.fillRoundedRectangle (area.reduced (3.0f), t.smallRadius + 1.0f);
        area = area.reduced (6.0f);
    }
    else
    {
        area = area.reduced (2.0f);
    }

    g.setColour (colour);
    g.fillRoundedRectangle (area, t.smallRadius);

    if (selected)
    {
        // Tick, in black or white so it reads on any hue.
        const auto c = area.getCentre();
        juce::Path tick;
        tick.startNewSubPath (c.x - 7.0f, c.y);
        tick.lineTo (c.x - 2.0f, c.y + 5.0f);
        tick.lineTo (c.x + 8.0f, c.y - 6.0f);
        g.setColour (overlaychrome::inkOn (colour));
        g.strokePath (tick, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void SnapshotEditorCard::ColourSwatch::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
}

// ---------------------------------------------------------------------------

SnapshotEditorCard::SceneChip::SceneChip (std::function<void()> cb)
    : onClick (std::move (cb))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SnapshotEditorCard::SceneChip::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    auto area = getLocalBounds().toFloat().reduced (1.0f);

    const bool filled = selected || armed;
    g.setColour (filled ? info.colour : t.raised);
    g.fillRoundedRectangle (area, t.smallRadius);

    g.setColour (armed ? t.text : (filled ? info.colour : t.lineStrong));
    g.drawRoundedRectangle (area, t.smallRadius, armed ? 2.5f : 1.0f);

    if (! filled)
    {
        // Same colour bar as the top-bar scene cells.
        g.setColour (info.colour.withAlpha (0.8f));
        g.fillRoundedRectangle (area.getX() + 10.0f, area.getBottom() - 8.0f, area.getWidth() - 20.0f, 3.0f, 1.5f);
    }

    const auto ink = filled ? overlaychrome::inkOn (info.colour) : t.text;
    g.setColour (ink);
    if (armed)
    {
        g.setFont (font (12.0f, true));
        g.drawFittedText ("Tap\nagain", getLocalBounds().reduced (2), juce::Justification::centred, 2);
        return;
    }

    g.setFont (font (20.0f, true));
    g.drawText (info.letter, getLocalBounds().withTrimmedBottom (6), juce::Justification::centred);
}

void SnapshotEditorCard::SceneChip::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
}

// ---------------------------------------------------------------------------

SnapshotEditorCard::FootswitchRow::FootswitchRow (juce::String t, int a, Actions& act)
    : title (std::move (t)), action (a), actions (act)
{
    statusLabel.setFont (font (15.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setInterceptsMouseClicks (false, false);
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
    const auto& t = theme::current();
    const int cc = actions.ccFor ? actions.ccFor (action) : -1;
    const bool armed = actions.pedalArmed && actions.pedalArmed (action);

    if (armed)
    {
        statusLabel.setText ("Press the footswitch...", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
        toggleButton.text = "Cancel";
    }
    else if (cc >= 0)
    {
        statusLabel.setText (midicc::label (cc), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, t.text);
        toggleButton.text = "Clear";
    }
    else
    {
        statusLabel.setText ("Not assigned", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, t.textDim);
        toggleButton.text = "Learn";
    }

    toggleButton.repaint();
    repaint();
}

void SnapshotEditorCard::FootswitchRow::resized()
{
    auto area = getLocalBounds();
    toggleButton.setBounds (area.removeFromRight (112).withSizeKeepingCentre (112, touch::minTapTarget));
    area.removeFromRight (12);
    statusLabel.setBounds (area.removeFromRight (200));
}

void SnapshotEditorCard::FootswitchRow::paint (juce::Graphics& g)
{
    g.setColour (theme::current().text);
    g.setFont (font (15.0f, true));
    g.drawText (title, getLocalBounds().removeFromLeft (180), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------

SnapshotEditorCard::SnapshotEditorCard (bool isNewScene, Actions act)
    : isNew (isNewScene), actions (std::move (act))
{
    if (actions.getState)
        state = actions.getState();

    titleLabel.setFont (font (24.0f, true));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    closeButton.onClick = [this] { callDetached (actions.onClose); };
    addAndMakeVisible (closeButton);

    nameEditor.setFont (font (18.0f));
    nameEditor.setTextToShowWhenEmpty ("Clean, Lead, Solo...", theme::current().textDim);
    nameEditor.setInputRestrictions (24);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (14, 0);
    overlaychrome::drawSearchFieldChrome (nameEditor);
    nameEditor.setText (state.name, juce::dontSendNotification);
    // Live: the scene cell in the top bar follows each keystroke.
    nameEditor.onTextChange = [this] { if (actions.onNameChanged) actions.onNameChanged (nameEditor.getText().trim()); };
    nameEditor.onReturnKey = [this] { nameEditor.giveAwayKeyboardFocus(); };
    addAndMakeVisible (nameEditor);

    for (int i = 0; i < snapshotxml::maxSnapshots; ++i)
    {
        const auto colour = snapshotxml::defaultColourFor (i);
        addAndMakeVisible (swatches.add (new ColourSwatch (colour, [this, colour]
        {
            if (actions.onColourChosen) actions.onColourChosen (colour);
            refresh();
        })));
    }

    customButton.onClick = [this] { if (actions.onCustomColour) actions.onCustomColour(); };
    addAndMakeVisible (customButton);

    const char* pedalTitles[] = { "This scene", "Next scene", "Previous scene" };
    const int pedalActions[] = { state.index, -1, -2 };
    for (int i = 0; i < 3; ++i)
        addAndMakeVisible (pedalRows.add (new FootswitchRow (pedalTitles[i], pedalActions[i], actions)));

    copyStatus.setFont (font (14.0f));
    copyStatus.setInterceptsMouseClicks (false, false);
    addChildComponent (copyStatus);

    removeButton.onClick = [this]
    {
        if (state.scenes.size() <= 1)
            return;
        if (! removeArmed)
        {
            disarm();
            removeArmed = true;
            removeButton.text = "Tap again to delete";
            removeButton.repaint();
            startTimer (confirmMs);
            return;
        }
        stopTimer();
        callDetached (actions.onRemove);
    };
    cancelButton.onClick = [this] { callDetached (actions.onCancelNew); };
    doneButton.onClick = [this] { callDetached (actions.onClose); };

    addChildComponent (removeButton);
    addChildComponent (cancelButton);
    cancelButton.setVisible (isNew);
    addAndMakeVisible (doneButton);

    rebuildChips();
    setSize (cardWidth, heightForRows());
    refresh();
}

SnapshotEditorCard::~SnapshotEditorCard()
{
    stopTimer();
}

std::vector<SnapshotEditorCard::Row> SnapshotEditorCard::rows() const
{
    std::vector<Row> r;
    r.push_back ({ "Name", "shown in the scene cell", 76 });
    r.push_back ({ "Colour", "becomes the app accent", 76 });
    if (isNew)
        r.push_back ({ "Start from", "copies that scene's sound", 76 });
    r.push_back ({ "Footswitch", "MIDI Learn", 3 * pedalRowHeight + 20 });
    if (! isNew && state.scenes.size() > 1)
        r.push_back ({ "Copy sound to", "overwrites that scene", 96 });
    return r;
}

int SnapshotEditorCard::heightForRows() const
{
    int h = headerHeight + footerHeight;
    for (const auto& row : rows())
        h += row.height;
    return h;
}

void SnapshotEditorCard::rebuildChips()
{
    sourceChips.clear();
    copyChips.clear();

    for (int i = 0; i < (int) state.scenes.size(); ++i)
    {
        if (i == state.index)
            continue;

        if (isNew)
            addAndMakeVisible (sourceChips.add (new SceneChip ([this, i]
            {
                if (actions.onStartFrom) actions.onStartFrom (i);
                refresh();
            })));
        else
            addAndMakeVisible (copyChips.add (new SceneChip ([this, i]
            {
                if (armedCopyTarget != i)
                {
                    disarm();
                    armedCopyTarget = i;
                    copyStatus.setVisible (false);
                    refresh();
                    startTimer (confirmMs);
                    return;
                }
                stopTimer();
                armedCopyTarget = -1;
                if (actions.onCopyTo) actions.onCopyTo (i);
                refresh();
                copyStatus.setText ("Copied to scene " + state.scenes[(size_t) i].letter + ".", juce::dontSendNotification);
                copyStatus.setColour (juce::Label::textColourId, theme::current().textMuted);
                copyStatus.setVisible (true);
            })));
    }
}

void SnapshotEditorCard::disarm()
{
    stopTimer();
    const bool wasArmed = armedCopyTarget >= 0 || removeArmed;
    armedCopyTarget = -1;
    removeArmed = false;
    removeButton.text = "Delete scene";
    removeButton.repaint();
    if (wasArmed)
        refresh();
}

void SnapshotEditorCard::refresh()
{
    if (actions.getState)
        state = actions.getState();

    const auto letter = snapshotxml::letterFor (state.index);
    titleLabel.setText ((isNew ? "New scene " : "Scene ") + letter, juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, theme::current().text);

    bool paletteHit = false;
    for (auto* swatch : swatches)
    {
        swatch->selected = swatch->colour == state.colour;
        paletteHit = paletteHit || swatch->selected;
        swatch->repaint();
    }
    customButton.selected = ! paletteHit;
    customButton.selectedStyle = overlaychrome::SelectedStyle::chip;
    customButton.showDot = ! paletteHit;
    customButton.dotColour = state.colour;
    customButton.repaint();

    int chip = 0;
    for (int i = 0; i < (int) state.scenes.size(); ++i)
    {
        if (i == state.index)
            continue;
        auto& list = isNew ? sourceChips : copyChips;
        if (auto* c = list[chip++])
        {
            c->info = state.scenes[(size_t) i];
            c->selected = isNew && i == state.sourceIndex;
            c->armed = ! isNew && i == armedCopyTarget;
            c->repaint();
        }
    }

    for (auto* row : pedalRows)
        row->refresh();

    removeButton.setVisible (! isNew && state.scenes.size() > 1); // a preset always keeps one scene

    repaint();
}

void SnapshotEditorCard::visibilityChanged()
{
    if (isVisible())
        refresh(); // back from the colour picker card
}

void SnapshotEditorCard::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    overlaychrome::paintCardBackground (g, getLocalBounds().toFloat());
    overlaychrome::drawHeaderDivider (g, juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), (float) headerHeight));

    // Scene cell in the header, painted like the active cell in the top bar.
    const juce::Rectangle<float> cell ((float) margin, 12.0f, 58.0f, 52.0f);
    g.setColour (state.colour);
    g.fillRoundedRectangle (cell, t.smallRadius);
    g.setColour (overlaychrome::inkOn (state.colour));
    g.setFont (font (26.0f, true));
    g.drawText (snapshotxml::letterFor (state.index), cell, juce::Justification::centred);

    int y = headerHeight;
    for (const auto& row : rows())
    {
        auto key = juce::Rectangle<int> (margin, y + 14, keyWidth - 12, 48);
        g.setColour (t.text);
        g.setFont (font (16.0f, true));
        g.drawText (t.uppercaseLabels ? row.key.toUpperCase() : row.key, key.removeFromTop (24),
                    juce::Justification::centredLeft);
        g.setColour (t.textDim);
        g.setFont (font (12.5f));
        g.drawText (row.hint, key, juce::Justification::topLeft);

        y += row.height;
        g.setColour (t.line);
        g.drawHorizontalLine (y, (float) margin, (float) (getWidth() - margin));
    }
}

void SnapshotEditorCard::resized()
{
    const int right = getWidth() - margin;
    const int contentW = right - contentX;

    closeButton.setBounds (right - 52, 12, 52, 52);
    titleLabel.setBounds (margin + 58 + 16, 0, closeButton.getX() - (margin + 58 + 28), headerHeight);

    int y = headerHeight;
    for (const auto& row : rows())
    {
        const int mid = y + row.height / 2;

        if (row.key == "Name")
        {
            nameEditor.setBounds (contentX, mid - 24, contentW, 48);
        }
        else if (row.key == "Colour")
        {
            int x = contentX;
            for (auto* swatch : swatches)
            {
                swatch->setBounds (x, mid - swatchSize / 2, swatchSize, swatchSize);
                x += swatchSize + swatchGap;
            }
            customButton.setBounds (x + 2, mid - touch::minTapTarget / 2, right - x - 2, touch::minTapTarget);
        }
        else if (row.key == "Start from")
        {
            int x = contentX;
            for (auto* c : sourceChips)
            {
                c->setBounds (x, mid - 28, chipWidth, 56);
                x += chipWidth + chipGap;
            }
        }
        else if (row.key == "Footswitch")
        {
            int py = y + 10;
            for (auto* r : pedalRows)
            {
                r->setBounds (contentX, py, contentW, pedalRowHeight);
                py += pedalRowHeight;
            }
        }
        else if (row.key == "Copy sound to")
        {
            int x = contentX;
            for (auto* c : copyChips)
            {
                c->setBounds (x, y + 12, chipWidth, 56);
                x += chipWidth + chipGap;
            }
            copyStatus.setBounds (contentX, y + 70, contentW, 20);
        }

        y += row.height;
    }

    const int fy = y + (footerHeight - touch::minTapTarget) / 2;
    doneButton.setBounds (right - 140, fy, 140, touch::minTapTarget);
    removeButton.setBounds (margin, fy, 220, touch::minTapTarget);
    cancelButton.setBounds (margin, fy, 140, touch::minTapTarget);
}

} // namespace openguitarmultifx
