#include "FooterBar.h"

#include "OpenGuitarMultiFxLookAndFeel.h"

namespace openguitarmultifx
{

FooterBar::FooterBar()
{
    addAndMakeVisible (tunerButton);
    tunerButton.onClick = [this] { if (onTunerTapped) onTunerTapped(); };

    addAndMakeVisible (bpmValueLabel);
    bpmValueLabel.setFont (juce::Font (28.0f, juce::Font::bold));
    bpmValueLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (bpmUnitLabel);
    bpmUnitLabel.setFont (juce::Font (13.0f));
    bpmUnitLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    bpmUnitLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (tapButton);
    tapButton.onClick = [this] { tapTempo(); };

    addAndMakeVisible (eqButton);
    eqButton.onClick = [this] { if (onMasterEqTapped) onMasterEqTapped(); };

    // Master output level, post-graph: -60 dB .. +12 dB (a rotary matches the
    // knob language the rest of the app speaks; double-tap snaps back to unity).
    addAndMakeVisible (masterSlider);
    masterSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterSlider.setRange (-60.0, 12.0, 0.1);
    masterSlider.setSkewFactor (0.4); // most of the throw near 0 dB, where it gets used
    masterSlider.setDoubleClickReturnValue (true, 0.0);
    masterSlider.onValueChange = [this]
    {
        masterDb = (float) masterSlider.getValue();
        updateMasterReadout();
        if (onMasterGainChanged)
            onMasterGainChanged (masterDb);
    };

    addAndMakeVisible (masterValueLabel);
    masterValueLabel.setJustificationType (juce::Justification::centred);
    masterValueLabel.setFont (juce::Font (12.0f));
    masterValueLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    masterSlider.setValue (0.0, juce::dontSendNotification);
    updateMasterReadout();

    setLevels (0.0f, 0.0f);
}

void FooterBar::setMasterGainDb (float db)
{
    masterSlider.setValue (db, juce::dontSendNotification);
    masterDb = db;
    updateMasterReadout();
}

void FooterBar::updateMasterReadout()
{
    masterValueLabel.setText (masterDb <= -59.9f ? "-inf"
                                                 : (masterDb > 0.0f ? "+" : "") + juce::String (masterDb, 1) + " dB",
                              juce::dontSendNotification);
}

void FooterBar::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (lastTapMs > 0.0)
    {
        const double interval = now - lastTapMs;

        // Sane tempo range (20-300 BPM) -- anything outside it is either
        // an accidental double-click or such a long pause that it can't
        // plausibly be "the next beat" in the same tapping sequence, so
        // start a fresh average instead of blending it into the old one.
        if (interval >= 200.0 && interval <= 3000.0)
        {
            recentTapIntervalsMs.push_back (interval);
            if (recentTapIntervalsMs.size() > 8)
                recentTapIntervalsMs.erase (recentTapIntervalsMs.begin());

            double sum = 0.0;
            for (double ms : recentTapIntervalsMs)
                sum += ms;
            const double averageIntervalMs = sum / (double) recentTapIntervalsMs.size();

            bpm = 60000.0 / averageIntervalMs;
            bpmValueLabel.setText (juce::String (juce::roundToInt (bpm)), juce::dontSendNotification);
        }
        else
        {
            recentTapIntervalsMs.clear();
        }
    }

    lastTapMs = now;
}

void FooterBar::setActiveSnapshot (const juce::String& label, juce::Colour colour)
{
    if (snapshotLabel == label && snapshotColour == colour)
        return;

    snapshotLabel = label;
    snapshotColour = colour;
    repaint();
}

void FooterBar::setLevels (float inLevelIn, float outLevelIn)
{
    inLevel = juce::jlimit (0.0f, 1.0f, inLevelIn);
    outLevel = juce::jlimit (0.0f, 1.0f, outLevelIn);
    repaint();
}

void FooterBar::resized()
{
    auto area = getLocalBounds().reduced (16, 10);

    // TUNER button where the live note letter and gauge used to be -- see the header for why they are gone.
    tunerButton.setBounds (area.removeFromLeft (150).withSizeKeepingCentre (150, 52));

    area.removeFromLeft (16);

    // EQ card button + master volume knob -- the master-section controls (see the header).
    eqButton.setBounds (area.removeFromLeft (76).withSizeKeepingCentre (76, 52));
    area.removeFromLeft (12);
    auto masterZone = area.removeFromLeft (120).withSizeKeepingCentre (120, 72);
    masterValueLabel.setBounds (masterZone.removeFromRight (56).reduced (0, 20));
    masterSlider.setBounds (masterZone.withSizeKeepingCentre (64, 64));

    area.removeFromLeft (16);

    // Meters zone: two horizontal bars, IN above OUT -- drawn directly in
    // paint(), no child components there.
    auto meterZone = area.removeFromRight (240);
    inMeterBounds = meterZone.removeFromTop (meterZone.getHeight() / 2).reduced (0, 4).toFloat();
    outMeterBounds = meterZone.reduced (0, 4).toFloat();

    area.removeFromRight (16);

    // Whatever's left in the middle: TAP + BPM readout.
    auto tapZone = area.removeFromRight (76).withSizeKeepingCentre (68, 48);
    tapButton.setBounds (tapZone);
    area.removeFromRight (12);
    auto bpmTextZone = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 100), 58);
    bpmValueLabel.setBounds (bpmTextZone.removeFromTop (36));
    bpmUnitLabel.setBounds (bpmTextZone);
}

void FooterBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    if (snapshotLabel.isNotEmpty())
    {
        // The snapshot strip: a full-width band along the top edge in the
        // active snapshot's colour, with the letter (+ name when it has
        // one) in a tab at the left end -- see setActiveSnapshot().
        constexpr float stripHeight = 6.0f;
        g.setColour (snapshotColour);
        g.fillRect (0.0f, 0.0f, (float) getWidth(), stripHeight);

        const juce::Font tabFont (13.0f, juce::Font::bold);
        const float tabWidth = tabFont.getStringWidthFloat (snapshotLabel) + 22.0f;
        juce::Path tab;
        tab.addRoundedRectangle (16.0f, 0.0f, tabWidth, 21.0f, 6.0f, 6.0f,
                                 false, false, true, true);
        g.fillPath (tab);

        // White on dark custom colours, black on light ones -- a black
        // snapshot would otherwise hide the letter entirely.
        g.setColour (snapshotColour.contrasting (juce::Colours::black, juce::Colours::white));
        g.setFont (tabFont);
        g.drawText (snapshotLabel, juce::Rectangle<float> (16.0f, 0.0f, tabWidth, 21.0f).toNearestInt(),
                    juce::Justification::centred);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);
    }

    drawHorizontalMeter (g, inMeterBounds, inLevel, "In");
    drawHorizontalMeter (g, outMeterBounds, outLevel, "Out");
}

void FooterBar::drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level, const juce::String& label) const
{
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto fill = bounds.withWidth (bounds.getWidth() * level);
    g.setColour (level > 0.85f ? juce::Colours::orangered : OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
    g.fillRoundedRectangle (fill, 4.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText (label, bounds.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
}

} // namespace openguitarmultifx
