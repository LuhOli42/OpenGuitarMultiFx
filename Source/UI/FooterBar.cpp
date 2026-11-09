#include "FooterBar.h"
#include "Theme.h"

#include "OpenGuitarMultiFxLookAndFeel.h"

namespace openguitarmultifx
{
namespace
{
    juce::String caps (const juce::String& text)
    {
        return theme::current().uppercaseLabels ? text.toUpperCase() : text;
    }

    void drawCaps (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
    {
        const auto& t = theme::current();
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (caps (text), area.toNearestInt(), juce::Justification::centredLeft);
    }
}

void FooterButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (down ? t.raisedHigh : t.raised);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);

    if (isMouseOverOrDragging() && isEnabled())
    {
        g.setColour (t.lineStrong.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), t.smallRadius, 1.0f);
    }

    if (contentPainter)
        contentPainter (g, getLocalBounds().toFloat().reduced (14.0f, 0.0f));
}

FooterBar::FooterBar()
{
    addAndMakeVisible (tunerButton);
    tunerButton.onClick = [this] { if (onTunerTapped) onTunerTapped(); };
    tunerButton.contentPainter = [this] (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& t = theme::current();
        drawCaps (g, area.removeFromLeft (70.0f), "Tuner");
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("--", area.toNearestInt(), juce::Justification::centredRight);
    };

    addAndMakeVisible (eqButton);
    eqButton.onClick = [this] { if (onMasterEqTapped) onMasterEqTapped(); };
    eqButton.contentPainter = [this] (juce::Graphics& g, juce::Rectangle<float> area)
    {
        drawCaps (g, area.removeFromLeft (90.0f), "Master EQ");
        auto curveArea = area.removeFromRight (70.0f).withSizeKeepingCentre (70.0f, 26.0f);
        juce::Path curve;
        curve.startNewSubPath (curveArea.getX(), curveArea.getBottom() - 8.0f);
        curve.cubicTo (curveArea.getX() + 12.0f, curveArea.getBottom() - 8.0f,
                       curveArea.getX() + 14.0f, curveArea.getY() + 9.0f,
                       curveArea.getX() + 24.0f, curveArea.getY() + 10.0f);
        curve.cubicTo (curveArea.getX() + 36.0f, curveArea.getY() + 11.0f,
                       curveArea.getX() + 38.0f, curveArea.getBottom() - 6.0f,
                       curveArea.getX() + 50.0f, curveArea.getBottom() - 12.0f);
        curve.cubicTo (curveArea.getX() + 60.0f, curveArea.getBottom() - 16.0f,
                       curveArea.getX() + 62.0f, curveArea.getY() + 6.0f,
                       curveArea.getRight(), curveArea.getY() + 8.0f);
        g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
        g.strokePath (curve, juce::PathStrokeType (2.5f));
    };

    addAndMakeVisible (tapButton);
    tapButton.onClick = [this] { tapTempo(); };
    tapButton.contentPainter = [this] (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto& t = theme::current();
        drawCaps (g, area.removeFromLeft (42.0f), "BPM");
        auto pill = area.removeFromRight (46.0f).withSizeKeepingCentre (46.0f, 40.0f);
        area.removeFromRight (10.0f);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (bpm)), area.toNearestInt(), juce::Justification::centredRight);
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (t.text);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("TAP", pill.toNearestInt(), juce::Justification::centred);
    };

    addAndMakeVisible (masterSlider);
    masterSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterSlider.setRange (-60.0, 12.0, 0.1);
    masterSlider.setSkewFactor (0.4);
    masterSlider.setDoubleClickReturnValue (true, 0.0);
    masterSlider.onValueChange = [this]
    {
        masterDb = (float) masterSlider.getValue();
        updateMasterReadout();
        if (onMasterGainChanged)
            onMasterGainChanged (masterDb);
    };

    addAndMakeVisible (masterTitleLabel);
    masterTitleLabel.setFont (juce::Font (12.0f));
    masterTitleLabel.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (masterValueLabel);
    masterValueLabel.setJustificationType (juce::Justification::centredRight);
    masterValueLabel.setFont (juce::Font (15.0f, juce::Font::bold));

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
        if (interval >= 200.0 && interval <= 3000.0)
        {
            recentTapIntervalsMs.push_back (interval);
            if (recentTapIntervalsMs.size() > 8)
                recentTapIntervalsMs.erase (recentTapIntervalsMs.begin());
            double sum = 0.0;
            for (double ms : recentTapIntervalsMs)
                sum += ms;
            bpm = 60000.0 / (sum / (double) recentTapIntervalsMs.size());
            tapButton.repaint();
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
    auto area = getLocalBounds().reduced (16, 13);

    tunerButton.setBounds (area.removeFromLeft (150));
    area.removeFromLeft (14);
    eqButton.setBounds (area.removeFromLeft (190));
    area.removeFromLeft (14);

    auto masterZone = area.removeFromLeft (170);
    masterTitleLabel.setBounds (masterZone.removeFromLeft (54));
    masterValueLabel.setBounds (masterZone.removeFromRight (72));
    masterSlider.setBounds (masterZone.withSizeKeepingCentre (48, 48));

    auto meterZone = area.removeFromRight (220);
    meterZone = meterZone.withSizeKeepingCentre (220, 58);
    inMeterBounds = juce::Rectangle<float> ((float) meterZone.getX(), (float) meterZone.getY() + 12.0f, 220.0f, 14.0f);
    outMeterBounds = juce::Rectangle<float> ((float) meterZone.getX(), (float) meterZone.getY() + 33.0f, 220.0f, 14.0f);

    tapButton.setBounds (area.removeFromRight (180));
}

void FooterBar::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.surface);

    const auto accent = snapshotColour.isTransparent() ? OpenGuitarMultiFxLookAndFeel::getAppAccentColour()
                                                       : snapshotColour;
    g.setColour (accent);
    g.fillRect (0.0f, 0.0f, (float) getWidth(), 4.0f);
    if (t.blockStyle == theme::BlockStyle::glow)
    {
        g.setColour (accent.withAlpha (0.28f));
        g.fillRect (0.0f, 4.0f, (float) getWidth(), 4.0f);
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (0.0f, 8.0f, (float) getWidth(), 3.0f);
    }

    if (snapshotLabel.isNotEmpty())
    {
        const juce::Font tabFont (10.5f, juce::Font::bold);
        const float tabWidth = tabFont.getStringWidthFloat (snapshotLabel) + 20.0f;
        juce::Path tab;
        tab.addRoundedRectangle (16.0f, 3.0f, tabWidth, 20.0f, 8.0f, 8.0f, false, false, true, true);
        g.setColour (accent);
        g.fillPath (tab);
        g.setColour (accent.contrasting (juce::Colours::black, juce::Colours::white));
        g.setFont (tabFont);
        g.drawText (snapshotLabel, juce::Rectangle<int> (16, 3, (int) tabWidth, 20),
                    juce::Justification::centred);
    }

    drawHorizontalMeter (g, inMeterBounds, inLevel, "IN");
    drawHorizontalMeter (g, outMeterBounds, outLevel, "OUT");
}

void FooterBar::drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level,
                                     const juce::String& label) const
{
    const auto& t = theme::current();
    const auto accent = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();

    g.setColour (t.raisedHigh);
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto fill = bounds.withWidth (bounds.getWidth() * juce::jlimit (0.0f, 1.0f, level));
    if (fill.getWidth() > 0.5f)
    {
        if (t.blockStyle == theme::BlockStyle::topStripe)
        {
            juce::Path clip;
            clip.addRoundedRectangle (fill, 4.0f);
            g.saveState();
            g.reduceClipRegion (clip);
            g.setColour (accent.withAlpha (0.85f));
            for (float x = fill.getX(); x < fill.getRight(); x += 7.0f)
                g.fillRect (x, fill.getY(), juce::jmin (5.0f, fill.getRight() - x), fill.getHeight());
            g.restoreState();
        }
        else
        {
            juce::ColourGradient grad (accent.withAlpha (0.85f), fill.getX(), fill.getY(),
                                       accent.brighter (0.25f).withAlpha (0.85f), fill.getRight(), fill.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 4.0f);
        }
    }

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (label, bounds.toNearestInt().reduced (7, 0), juce::Justification::centredLeft);
}

} // namespace openguitarmultifx
