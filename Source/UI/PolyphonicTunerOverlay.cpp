#include "PolyphonicTunerOverlay.h"

#include "OpenGuitarMultiFxLookAndFeel.h"
#include "TouchSizing.h"

namespace openguitarmultifx
{

namespace
{
    constexpr int rowHeight = 64;
    constexpr int minOctave = 0;
    constexpr int maxOctave = 6;

    // Quick-pick presets shown in presetCombo -- ids are arbitrary but
    // fixed, matched in PolyphonicTunerOverlay::applyPreset().
    enum PresetId
    {
        preset6String = 1,
        preset7String,
        preset8String,
        presetBass4,
        presetBass5,
        presetBass6,
    };

    bool tuningMatches (const TuningProfile& a, const TuningProfile& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].noteName != b[i].noteName)
                return false;
        return true;
    }
}

PolyphonicTunerOverlay::PolyphonicTunerOverlay (TuningProfile initialTuning)
    : profile (std::move (initialTuning))
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Polyphonic Tuner", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (presetLabel);
    presetLabel.setFont (14.0f);
    presetLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    addAndMakeVisible (presetCombo);
    presetCombo.addItem ("6-String Standard", preset6String);
    presetCombo.addItem ("7-String Standard", preset7String);
    presetCombo.addItem ("8-String Standard", preset8String);
    presetCombo.addItem ("4-String Bass", presetBass4);
    presetCombo.addItem ("5-String Bass", presetBass5);
    presetCombo.addItem ("6-String Bass", presetBass6);
    presetCombo.onChange = [this] { applyPreset (presetCombo.getSelectedId()); };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    rebuildStringRows();
    startTimerHz (20);
}

PolyphonicTunerOverlay::~PolyphonicTunerOverlay() = default;

void PolyphonicTunerOverlay::applyPreset (int presetMenuId)
{
    switch (presetMenuId)
    {
        case preset6String: profile = tunings::standardGuitar6(); break;
        case preset7String: profile = tunings::standardGuitar7(); break;
        case preset8String: profile = tunings::standardGuitar8(); break;
        case presetBass4:   profile = tunings::standardBass4();   break;
        case presetBass5:   profile = tunings::standardBass5();   break;
        case presetBass6:   profile = tunings::standardBass6();   break;
        default: return;
    }

    rebuildStringRows(); // also calls resized() indirectly via setSize()
    if (onTuningChanged)
        onTuningChanged (profile);
}

void PolyphonicTunerOverlay::refreshPresetComboText()
{
    struct NamedPreset { int id; TuningProfile profile; };
    const NamedPreset named[] = {
        { preset6String, tunings::standardGuitar6() },
        { preset7String, tunings::standardGuitar7() },
        { preset8String, tunings::standardGuitar8() },
        { presetBass4,   tunings::standardBass4() },
        { presetBass5,   tunings::standardBass5() },
        { presetBass6,   tunings::standardBass6() },
    };

    for (auto& p : named)
    {
        if (tuningMatches (profile, p.profile))
        {
            presetCombo.setSelectedId (p.id, juce::dontSendNotification);
            return;
        }
    }

    presetCombo.setSelectedId (0, juce::dontSendNotification);
    presetCombo.setText ("Custom", juce::dontSendNotification);
}

void PolyphonicTunerOverlay::rebuildStringRows()
{
    for (auto& row : rows)
    {
        removeChildComponent (&row->targetLabel);
        removeChildComponent (&row->noteCombo);
    }
    rows.clear();

    for (size_t i = 0; i < profile.size(); ++i)
    {
        auto row = std::make_unique<StringRow>();

        row->targetLabel.setJustificationType (juce::Justification::centredLeft);
        row->targetLabel.setFont (juce::Font (16.0f, juce::Font::bold));
        row->targetLabel.setText (profile[i].noteName, juce::dontSendNotification);
        addAndMakeVisible (row->targetLabel);

        int itemId = 1;
        for (int octave = minOctave; octave <= maxOctave; ++octave)
        {
            row->noteCombo.addSectionHeading ("Octave " + juce::String (octave));
            for (int semitone = 0; semitone < 12; ++semitone)
            {
                const int midiNote = (octave + 1) * 12 + semitone;
                row->noteCombo.addItem (tunings::midiNoteToName (midiNote), midiNote);
                juce::ignoreUnused (itemId);
                ++itemId;
            }
        }

        const int currentMidi = tunings::noteNameToMidiNote (profile[i].noteName);
        if (currentMidi >= 0)
            row->noteCombo.setSelectedId (currentMidi, juce::dontSendNotification);

        const int stringIndex = (int) i;
        row->noteCombo.onChange = [this, stringIndex]
        {
            if (stringIndex < 0 || stringIndex >= (int) rows.size())
                return;

            const int midiNote = rows[(size_t) stringIndex]->noteCombo.getSelectedId();
            if (midiNote <= 0)
                return;

            const auto name = tunings::midiNoteToName (midiNote);
            profile[(size_t) stringIndex] = { name, tunings::noteNameToFrequency (name) };
            rows[(size_t) stringIndex]->targetLabel.setText (name, juce::dontSendNotification);

            refreshPresetComboText();
            if (onTuningChanged)
                onTuningChanged (profile);
        };

        addAndMakeVisible (row->noteCombo);
        rows.push_back (std::move (row));
    }

    refreshPresetComboText();

    // setSize() only calls resized() when the pixel size actually changes
    // (e.g. switching between two presets with the same string count,
    // like 6-String Standard <-> 6-String Bass, wouldn't) -- every row
    // component here is freshly recreated above with no bounds yet, so
    // resized() must run unconditionally or they'd render at (0,0,0,0).
    setSize (480, 130 + (int) rows.size() * rowHeight + 60);
    resized();
}

void PolyphonicTunerOverlay::timerCallback()
{
    if (! getReading)
        return;

    bool anyChanged = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto reading = getReading ((int) i);
        auto& row = *rows[i];
        if (row.active != reading.active || ! juce::exactlyEqual (row.cents, reading.cents))
            anyChanged = true;
        row.active = reading.active;
        row.cents = reading.cents;
    }

    if (anyChanged)
        repaint();
}

void PolyphonicTunerOverlay::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto top = area.removeFromTop (28);
    closeButton.setBounds (top.removeFromRight (70));
    titleLabel.setBounds (top);

    area.removeFromTop (10);
    auto presetRow = area.removeFromTop (touch::minTapTarget);
    presetLabel.setBounds (presetRow.removeFromLeft (60));
    presetCombo.setBounds (presetRow);

    area.removeFromTop (14);

    for (auto& row : rows)
    {
        auto rowArea = area.removeFromTop (rowHeight).reduced (0, 4);
        row->targetLabel.setBounds (rowArea.removeFromLeft (56));
        rowArea.removeFromLeft (8);
        row->noteCombo.setBounds (rowArea.removeFromLeft (110).withSizeKeepingCentre (110, touch::minTapTarget));
        rowArea.removeFromLeft (12);
        row->gaugeBounds = rowArea.withSizeKeepingCentre (rowArea.getWidth(), 20).toFloat();
    }
}

void PolyphonicTunerOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    for (auto& row : rows)
        drawGauge (g, row->gaugeBounds, *row);
}

void PolyphonicTunerOverlay::drawGauge (juce::Graphics& g, juce::Rectangle<float> bounds, const StringRow& row) const
{
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    const float centreX = bounds.getCentreX();
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawLine (centreX, bounds.getY() - 3.0f, centreX, bounds.getBottom() + 3.0f, 2.0f);

    if (! row.active)
        return; // string not currently sounding -- just the empty track + centre tick

    const float deviation = juce::jlimit (-1.0f, 1.0f, row.cents / 50.0f);
    const float needleX = centreX + deviation * bounds.getWidth() * 0.5f;
    const bool inTune = std::abs (row.cents) < 5.0f;
    g.setColour (inTune ? juce::Colours::limegreen : OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
    g.fillEllipse (needleX - 8.0f, bounds.getCentreY() - 8.0f, 16.0f, 16.0f);
}

} // namespace openguitarmultifx
