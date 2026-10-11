#include "MasterEqDialog.h"

#include "OpenGuitarMultiFxLookAndFeel.h"
#include "TouchSizing.h"

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    float defaultFreqFor (MasterEqBand::Type t) noexcept
    {
        switch (t)
        {
            case MasterEqBand::Type::lowCut:    return 80.0f;
            case MasterEqBand::Type::lowShelf:  return 200.0f;
            case MasterEqBand::Type::peak:      return 1000.0f;
            case MasterEqBand::Type::notch:     return 1000.0f;
            case MasterEqBand::Type::highShelf: return 6000.0f;
            case MasterEqBand::Type::highCut:   return 12000.0f;
        }
        return 1000.0f;
    }
}

MasterEqDialog::MasterEqDialog()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Master EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (22.0f, juce::Font::bold));

    addAndMakeVisible (hintLabel);
    hintLabel.setFont (juce::Font (13.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setText ("drag nodes - wheel trims Q - double-tap mutes a band", juce::dontSendNotification);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        bands.clear();
        selectedBand = -1;
        bandsEdited();
    };

    for (int i = 0; i < 6; ++i)
    {
        addAndMakeVisible (addButtons[(size_t) i]);
        addButtons[(size_t) i].setButtonText ("+ " + juce::String (MasterEq::displayName (MasterEq::typeForIndex (i))));
        addButtons[(size_t) i].onClick = [this, i] { addBand (MasterEq::typeForIndex (i)); };
    }

    // Selected-band strip -- created once, shown/hidden by refreshSelectedStrip().
    addAndMakeVisible (selectedLabel);
    selectedLabel.setFont (juce::Font (16.0f, juce::Font::bold));

    addAndMakeVisible (bandEnabledButton);
    bandEnabledButton.setClickingTogglesState (true);
    bandEnabledButton.onClick = [this]
    {
        if (selectedBand >= 0 && selectedBand < (int) bands.size())
        {
            bands[(size_t) selectedBand].enabled = bandEnabledButton.getToggleState();
            bandsEdited();
        }
    };

    addAndMakeVisible (bandDeleteButton);
    bandDeleteButton.onClick = [this] { if (selectedBand >= 0) deleteBand (selectedBand); };

    auto setupSlider = [this] (juce::Slider& s, juce::Label& label, const char* name)
    {
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, 32);
        s.onValueChange = [this] { /* rebound per selection below */ };
        addAndMakeVisible (label);
        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::Font (13.0f));
        label.setColour (juce::Label::textColourId, juce::Colours::grey);
        label.setJustificationType (juce::Justification::centredLeft);
    };
    setupSlider (freqSlider, freqLabel, "Freq");
    setupSlider (gainSlider, gainLabel, "Gain");
    setupSlider (qSlider, qLabel, "Q");

    freqSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < (int) bands.size())
        {
            bands[(size_t) selectedBand].freqHz = (float) freqSlider.getValue();
            bandsEdited();
        }
    };
    gainSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < (int) bands.size())
        {
            bands[(size_t) selectedBand].gainDb = (float) gainSlider.getValue();
            bandsEdited();
        }
    };
    qSlider.onValueChange = [this]
    {
        if (selectedBand >= 0 && selectedBand < (int) bands.size())
        {
            auto& band = bands[(size_t) selectedBand];
            if (typeIsCut (band.type))
                band.slopeDbOct = juce::roundToInt (qSlider.getValue() / 12.0) * 12;
            else
                band.q = (float) qSlider.getValue();
            bandsEdited();
        }
    };

    addAndMakeVisible (noSelectionLabel);
    noSelectionLabel.setText ("tap a node to edit it, or add a band below", juce::dontSendNotification);
    noSelectionLabel.setFont (juce::Font (16.0f));
    noSelectionLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    noSelectionLabel.setJustificationType (juce::Justification::centred);

    // Big enough for a real curve to drag on -- OverlayHost clamps it to the window.
    setSize (980, 640);
}

MasterEqDialog::~MasterEqDialog() = default;

void MasterEqDialog::setBands (const std::vector<MasterEqBand>& newBands)
{
    bands = newBands;
    selectedBand = bands.empty() ? -1 : 0;
    refreshSelectedStrip();
    repaint();
}

// -- coordinate mapping ------------------------------------------------------

double MasterEqDialog::freqAtX (float x) const noexcept
{
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - graphArea.getX()) / graphArea.getWidth()));
    return minFreq * std::pow (maxFreq / minFreq, t);
}

float MasterEqDialog::xAtFreq (double freq) const noexcept
{
    const double t = std::log (freq / minFreq) / std::log (maxFreq / minFreq);
    return graphArea.getX() + graphArea.getWidth() * (float) juce::jlimit (0.0, 1.0, t);
}

float MasterEqDialog::yAtDb (float db) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (maxDb - db) / (maxDb - minDb));
    return graphArea.getY() + graphArea.getHeight() * t;
}

float MasterEqDialog::dbAtY (float y) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (y - graphArea.getY()) / graphArea.getHeight());
    return maxDb - t * (maxDb - minDb);
}

double MasterEqDialog::curveDbAt (double freq, int skipBand) const noexcept
{
    std::vector<MasterEqBand> active;
    for (int i = 0; i < (int) bands.size(); ++i)
        if (i != skipBand)
            active.push_back (bands[(size_t) i]);
    const double mag = MasterEq::magnitudeAt (active, freq, effectiveSampleRate());
    return 20.0 * std::log10 (juce::jmax (1.0e-5, mag));
}

juce::Point<float> MasterEqDialog::nodePosition (int bandIndex) const noexcept
{
    const auto& band = bands[(size_t) bandIndex];
    // The node hugs the SUMMED curve at its own frequency -- same convention
    // Pro-Q / ReaEQ use, so a node always reads as sitting "on" its effect.
    return { xAtFreq (band.freqHz), yAtDb ((float) curveDbAt (band.freqHz)) };
}

int MasterEqDialog::bandAt (juce::Point<float> pos) const noexcept
{
    int best = -1;
    float bestDist = 30.0f; // >48px diameter hit area -- see TouchSizing.h's floor
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const float dist = nodePosition (i).getDistanceFrom (pos);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

juce::Colour MasterEqDialog::bandColour (int bandIndex) const noexcept
{
    static const juce::Colour palette[] = { juce::Colour (0xff3d8fd9), juce::Colour (0xff35d07f), juce::Colour (0xffe8b93a),
                                            juce::Colour (0xffd9534f), juce::Colour (0xffb06fd9), juce::Colour (0xff4fd0c8),
                                            juce::Colour (0xfff08a3c), juce::Colour (0xff8fd93d) };
    return palette[(size_t) bandIndex % 8];
}

// -- edits -------------------------------------------------------------------

void MasterEqDialog::addBand (MasterEqBand::Type type)
{
    if ((int) bands.size() >= MasterEq::maxBands)
        return;

    MasterEqBand band;
    band.type = type;
    band.freqHz = defaultFreqFor (type);
    band.gainDb = typeHasGain (type) ? 4.0f : 0.0f; // a little boost makes a new node visible on the curve
    band.q = typeHasGain (type) || type == MasterEqBand::Type::notch ? 1.0f : 1.0f;
    band.slopeDbOct = 12;
    bands.push_back (band);
    selectBand ((int) bands.size() - 1);
    bandsEdited();
}

void MasterEqDialog::deleteBand (int index)
{
    if (index < 0 || index >= (int) bands.size())
        return;
    bands.erase (bands.begin() + index);
    selectedBand = juce::jmin (selectedBand, (int) bands.size() - 1);
    bandsEdited();
}

void MasterEqDialog::selectBand (int index)
{
    selectedBand = index;
    refreshSelectedStrip();
    repaint();
}

void MasterEqDialog::bandsEdited()
{
    refreshSelectedStrip();
    repaint();
    if (onBandsChanged)
        onBandsChanged (bands);
}

void MasterEqDialog::refreshSelectedStrip()
{
    const bool has = selectedBand >= 0 && selectedBand < (int) bands.size();
    noSelectionLabel.setVisible (! has);
    for (juce::Component* c : { (juce::Component*) &selectedLabel, (juce::Component*) &bandEnabledButton,
                                (juce::Component*) &bandDeleteButton, (juce::Component*) &freqSlider,
                                (juce::Component*) &gainSlider, (juce::Component*) &qSlider,
                                (juce::Component*) &freqLabel, (juce::Component*) &gainLabel, (juce::Component*) &qLabel })
        c->setVisible (has);
    if (! has)
        return;

    const auto& band = bands[(size_t) selectedBand];
    selectedLabel.setText ("Band " + juce::String (selectedBand + 1) + "  " + MasterEq::displayName (band.type),
                           juce::dontSendNotification);
    selectedLabel.setColour (juce::Label::textColourId, bandColour (selectedBand));

    bandEnabledButton.setButtonText (band.enabled ? "On" : "Off");
    bandEnabledButton.setToggleState (band.enabled, juce::dontSendNotification);

    freqSlider.setRange (minFreq, maxFreq, 1.0);
    freqSlider.setSkewFactor (0.28); // ~log travel so the low octaves get usable resolution
    freqSlider.setValue (band.freqHz, juce::dontSendNotification);

    const bool hasGain = typeHasGain (band.type);
    gainSlider.setVisible (hasGain);
    gainLabel.setVisible (hasGain);
    if (hasGain)
    {
        gainSlider.setRange (-24.0, 24.0, 0.1);
        gainSlider.setValue (band.gainDb, juce::dontSendNotification);
    }

    if (typeIsCut (band.type))
    {
        qLabel.setText ("Slope", juce::dontSendNotification);
        qSlider.setRange (12.0, 48.0, 1.0);
        qSlider.setSkewFactor (1.0);
        qSlider.setValue (band.slopeDbOct, juce::dontSendNotification);
    }
    else if (band.type == MasterEqBand::Type::lowShelf || band.type == MasterEqBand::Type::highShelf)
    {
        qLabel.setText ("Slope", juce::dontSendNotification);
        qSlider.setRange (0.2, 2.0, 0.05);
        qSlider.setSkewFactor (1.0);
        qSlider.setValue (band.q, juce::dontSendNotification);
    }
    else
    {
        qLabel.setText ("Q", juce::dontSendNotification);
        qSlider.setRange (0.3, 18.0, 0.01);
        qSlider.setSkewFactor (0.35);
        qSlider.setValue (band.q, juce::dontSendNotification);
    }
}

// -- painting ----------------------------------------------------------------

void MasterEqDialog::resized()
{
    auto area = getLocalBounds().reduced (24, 20);

    auto top = area.removeFromTop (touch::minTapTarget);
    closeButton.setBounds (top.removeFromRight (110).withSizeKeepingCentre (110, touch::minTapTarget));
    clearButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, touch::minTapTarget));
    titleLabel.setBounds (top.removeFromLeft (160));
    hintLabel.setBounds (top);

    area.removeFromTop (10);
    graphArea = area.removeFromTop (juce::jmax (240, area.getHeight() - 220)).toFloat();

    area.removeFromTop (10);

    // Selected-band strip: label+buttons column then three sliders.
    auto strip = area.removeFromTop (112);
    auto meta = strip.removeFromLeft (170);
    selectedLabel.setBounds (meta.removeFromTop (36));
    auto stripButtons = meta.removeFromTop (touch::minTapTarget);
    bandEnabledButton.setBounds (stripButtons.removeFromLeft (80));
    stripButtons.removeFromLeft (8);
    bandDeleteButton.setBounds (stripButtons);
    noSelectionLabel.setBounds (strip);

    auto sliderCol = [] (juce::Rectangle<int>& zone, juce::Label& label, juce::Slider& slider)
    {
        auto cell = zone.removeFromLeft (zone.getWidth() / 3 + 40);
        label.setBounds (cell.removeFromTop (24));
        slider.setBounds (cell.reduced (0, 6));
    };
    sliderCol (strip, freqLabel, freqSlider);
    sliderCol (strip, gainLabel, gainSlider);
    sliderCol (strip, qLabel, qSlider);

    area.removeFromTop (10);

    // Add-band row.
    auto addRow = area.removeFromTop (touch::minTapTarget);
    const int btnW = (addRow.getWidth() - 5 * 8) / 6;
    for (auto& btn : addButtons)
    {
        btn.setBounds (addRow.removeFromLeft (btnW));
        addRow.removeFromLeft (8);
    }
}

void MasterEqDialog::paint (juce::Graphics& g)
{
    paintGraph (g, graphArea);
}

void MasterEqDialog::paintGraph (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colour (0xff101418));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (area, 8.0f, 1.0f);

    // Frequency grid + labels.
    const double freqMarks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    g.setFont (juce::Font (11.0f));
    for (double f : freqMarks)
    {
        const float x = xAtFreq (f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (x, area.getY() + 4, x, area.getBottom() - 4, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 18, area.getBottom() - 18, 36, 14), juce::Justification::centred);
    }

    // dB grid every 6 dB.
    for (float db = -24.0f; db <= 24.01f; db += 6.0f)
    {
        const float y = yAtDb (db);
        const bool zero = std::abs (db) < 0.01f;
        g.setColour (juce::Colours::white.withAlpha (zero ? 0.35f : 0.08f));
        g.drawLine (area.getX() + 4, y, area.getRight() - 4, y, zero ? 1.4f : 1.0f);
        if (! zero)
        {
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                        juce::Rectangle<float> (area.getX() + 6, y - 14, 32, 12), juce::Justification::centredLeft);
        }
    }

    if (bands.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.setFont (juce::Font (18.0f));
        g.drawText ("add a band to shape the master bus", area, juce::Justification::centred);
    }

    // Per-band ghost curves for enabled bands.
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto& band = bands[(size_t) i];
        if (! band.enabled)
            continue;
        juce::Path p;
        for (float x = area.getX() + 4; x <= area.getRight() - 4; x += 3.0f)
        {
            const double f = freqAtX (x);
            std::vector<MasterEqBand> single { band };
            const float y = yAtDb ((float) (20.0 * std::log10 (juce::jmax (1.0e-5, MasterEq::magnitudeAt (single, f, effectiveSampleRate())))));
            if (x <= area.getX() + 4.5f) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (bandColour (i).withAlpha (i == selectedBand ? 0.55f : 0.18f));
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // The summed response curve -- the thing you actually hear.
    juce::Path curve;
    for (float x = area.getX() + 4; x <= area.getRight() - 4; x += 2.0f)
    {
        const float y = yAtDb ((float) curveDbAt (freqAtX (x)));
        if (x <= area.getX() + 4.5f) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }

    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
    juce::Path fill = curve;
    fill.lineTo (area.getRight() - 4, area.getBottom() - 4);
    fill.lineTo (area.getX() + 4, area.getBottom() - 4);
    fill.closeSubPath();
    g.setColour (accent.withAlpha (0.08f));
    g.fillPath (fill);

    g.setColour (accent.withAlpha (0.95f));
    g.strokePath (curve, juce::PathStrokeType (2.2f));

    for (int i = 0; i < (int) bands.size(); ++i)
        paintNode (g, i);
}

void MasterEqDialog::paintNode (juce::Graphics& g, int bandIndex)
{
    const auto& band = bands[(size_t) bandIndex];
    const auto pos = nodePosition (bandIndex);
    const float r = bandIndex == selectedBand ? 13.0f : 10.0f;
    const auto colour = bandColour (bandIndex).withAlpha (band.enabled ? 1.0f : 0.30f);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillEllipse (pos.x - r - 2, pos.y - r - 2, (r + 2) * 2, (r + 2) * 2);
    g.setColour (colour);
    g.fillEllipse (pos.x - r, pos.y - r, r * 2, r * 2);

    if (bandIndex == selectedBand)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (pos.x - r - 3, pos.y - r - 3, (r + 3) * 2, (r + 3) * 2, 1.6f);
    }

    // The band number inside the node, like Pro-Q's numbered dots.
    g.setColour (juce::Colours::black);
    g.setFont (juce::Font (r * 1.1f, juce::Font::bold));
    g.drawText (juce::String (bandIndex + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2, r * 2),
                juce::Justification::centred);
}

// -- interaction -------------------------------------------------------------

void MasterEqDialog::mouseDown (const juce::MouseEvent& event)
{
    if (! graphArea.contains (event.position))
        return;

    const int hit = bandAt (event.position);
    if (hit >= 0)
    {
        draggedBand = hit;
        selectBand (hit);
    }
    else
    {
        draggedBand = -1;
        selectBand (-1);
    }
}

void MasterEqDialog::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedBand < 0 || draggedBand >= (int) bands.size())
        return;

    auto& band = bands[(size_t) draggedBand];
    band.freqHz = juce::jlimit ((float) minFreq, (float) maxFreq, (float) freqAtX (event.position.x));

    if (typeHasGain (band.type))
    {
        // The pointer tracks the SUMMED curve, so the band's own gain is the
        // pointer's dB minus what every OTHER band contributes at this spot.
        const double others = curveDbAt (band.freqHz, draggedBand);
        band.gainDb = juce::jlimit (minDb, maxDb, dbAtY (event.position.y) - (float) others);
    }

    bandsEdited();
}

void MasterEqDialog::mouseDoubleClick (const juce::MouseEvent& event)
{
    const int hit = bandAt (event.position);
    if (hit >= 0)
    {
        bands[(size_t) hit].enabled = ! bands[(size_t) hit].enabled;
        bandsEdited();
    }
}

void MasterEqDialog::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const int hit = bandAt (event.position);
    if (hit < 0)
        return;

    auto& band = bands[(size_t) hit];
    if (typeIsCut (band.type))
        band.slopeDbOct = juce::jlimit (12, 48, band.slopeDbOct + (wheel.deltaY > 0 ? 12 : -12));
    else
        band.q = juce::jlimit (0.1f, 24.0f, band.q * (wheel.deltaY > 0 ? 1.15f : 1.0f / 1.15f));

    if (hit == selectedBand)
        refreshSelectedStrip();
    bandsEdited();
}

} // namespace openguitarmultifx
