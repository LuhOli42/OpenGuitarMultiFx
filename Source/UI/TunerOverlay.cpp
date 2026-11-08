#include "TunerOverlay.h"

#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Theme.h"
#include "TouchSizing.h"

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    /** Equal temperament, A4 = 440 Hz: the nearest note and how far off it is in cents. */
    struct Reading { juce::String letter, accidental; int octave; float cents; };

    Reading readingFor (float hz)
    {
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int nearest = (int) std::lround (midi);
        const auto name = noteNames[((nearest % 12) + 12) % 12];
        return { name.substring (0, 1), name.substring (1), nearest / 12 - 1, (midi - (float) nearest) * 100.0f };
    }

    constexpr float inTuneCents = 5.0f;  // inside this, the display goes green -- the usual tolerance for a tuner
    constexpr float gaugeRangeCents = 50.0f;

    juce::Colour tuningColour (float cents, bool active)
    {
        if (! active)
            return theme::current().textDim;
        if (std::abs (cents) <= inTuneCents)
            return juce::Colour (0xff35d07f);                       // in tune
        if (std::abs (cents) <= 15.0f)
            return juce::Colour (0xffe8b93a);                       // close
        return OpenGuitarMultiFxLookAndFeel::getAppAccentColour();  // way off
    }
}

TunerOverlay::TunerOverlay()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Tuner", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (19.0f, juce::Font::bold));

    addAndMakeVisible (hintLabel);
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setFont (14.0f);
    hintLabel.setText ("Chromatic - any string, any tuning", juce::dontSendNotification);

    addAndMakeVisible (referenceLabel);
    referenceLabel.setJustificationType (juce::Justification::centredRight);
    referenceLabel.setFont (14.0f);
    referenceLabel.setText ("A = 440 Hz", juce::dontSendNotification);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    // Big enough to read from a few feet away on a stage. OverlayHost keeps whatever size the content picks for itself.
    setSize (1080, 650);

    startTimerHz (30);
}

TunerOverlay::~TunerOverlay()
{
    if (onDestroyed != nullptr)
        onDestroyed();
}

void TunerOverlay::resized()
{
    auto area = getLocalBounds().reduced (24, 18);

    auto top = area.removeFromTop (touch::minTapTarget + 8);
    closeButton.setBounds (top.removeFromRight (110).withSizeKeepingCentre (110, touch::minTapTarget));
    referenceLabel.setBounds (top.removeFromRight (130));
    titleLabel.setBounds (top.removeFromLeft (130));
    hintLabel.setBounds (top);

    area.removeFromTop (14);
    gaugeBounds = area.removeFromTop (150).toFloat();
    area.removeFromTop (16);

    const int readoutHeight = 132;
    const int noteHeight = juce::jmax (200, area.getHeight() - readoutHeight);
    noteBounds = area.removeFromTop (noteHeight).toFloat();
    readoutBounds = area.toFloat();
}

void TunerOverlay::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto colour = tuningColour (cents, active);

    g.fillAll (t.background);
    g.setColour (t.line);
    g.drawHorizontalLine (gaugeBounds.getY() - 14.0f, 24.0f, (float) getWidth() - 24.0f);

    const float centreX = gaugeBounds.getCentreX();
    const float halfWidth = gaugeBounds.getWidth() * 0.5f - 42.0f;
    const float baseY = gaugeBounds.getBottom() - 34.0f;

    // ---- the ruler: ticks every 5 cents across +-50, tall at the centre and every 25 cents ----
    for (int tick = -10; tick <= 10; ++tick)
    {
        const float value = (float) tick * 5.0f;
        const float x = centreX + (value / gaugeRangeCents) * halfWidth;
        const bool quarter = (tick % 5) == 0;
        const bool major = (tick % 10) == 0;
        const float height = major ? 52.0f : (quarter ? 32.0f : 16.0f);
        g.setColour (major ? t.textDim : (quarter ? t.textMuted.withAlpha (0.55f) : t.lineStrong));
        g.fillRoundedRectangle (x - (major ? 2.0f : 1.5f), baseY - height, major ? 4.0f : 3.0f, height, 1.5f);
    }

    // Cents labels under the ruler, so the scale is readable without guessing.
    g.setFont (juce::FontOptions (12.5f));
    g.setColour (t.textDim);
    for (int label = -2; label <= 2; ++label)
    {
        const float value = (float) label * 25.0f;
        const float x = centreX + (value / gaugeRangeCents) * halfWidth;
        juce::String text;
        if (label < 0) text = "-" + juce::String ((int) -value);
        else if (label > 0) text = "+" + juce::String ((int) value);
        else text = "0";
        g.drawText (text, juce::Rectangle<float> (x - 24.0f, gaugeBounds.getBottom() - 22.0f, 48.0f, 18.0f),
                    juce::Justification::centred);
    }

    // The in-tune window, so "dead centre" is a target you can see rather than a line you have to guess at.
    const float windowHalf = (inTuneCents / gaugeRangeCents) * halfWidth;
    const bool inWindow = active && std::abs (cents) <= inTuneCents;
    g.setColour ((inWindow ? juce::Colour (0xff35d07f) : t.text).withAlpha (0.10f));
    g.fillRoundedRectangle (centreX - windowHalf, baseY - 58.0f, windowHalf * 2.0f, 62.0f, 6.0f);

    // ---- flat / sharp markers, lit on the side you are on ----
    const bool flat = active && cents < -inTuneCents;
    const bool sharp = active && cents > inTuneCents;
    g.setFont (juce::FontOptions (30.0f).withStyle ("Bold"));
    g.setColour (flat ? colour : t.textDim.withAlpha (0.45f));
    g.drawText ("b", juce::Rectangle<float> (gaugeBounds.getX(), baseY - 58.0f, 34.0f, 58.0f), juce::Justification::centred);
    g.setColour (sharp ? colour : t.textDim.withAlpha (0.45f));
    g.drawText ("#", juce::Rectangle<float> (gaugeBounds.getRight() - 34.0f, baseY - 58.0f, 34.0f, 58.0f), juce::Justification::centred);

    // ---- the needle ----
    if (active)
    {
        const float x = centreX + juce::jlimit (-1.0f, 1.0f, cents / gaugeRangeCents) * halfWidth;
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (x - 7.0f, baseY - 66.0f, 14.0f, 76.0f, 7.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (x - 4.0f, baseY - 62.0f, 8.0f, 72.0f, 4.0f);
        g.fillEllipse (x - 12.0f, baseY + 4.0f, 24.0f, 24.0f);
        g.setColour (t.background);
        g.fillEllipse (x - 4.0f, baseY + 12.0f, 8.0f, 8.0f);
    }

    // ---- the note itself: the thing you read from across a stage ----
    if (active)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (noteBounds.getHeight() * 0.82f).withStyle ("Bold"));
        const auto letterBox = noteBounds.withTrimmedRight (noteBounds.getWidth() * 0.16f);
        g.drawText (noteLetter, letterBox, juce::Justification::centred);

        const float letterWidth = g.getCurrentFont().getStringWidthFloat (noteLetter);
        const float sideX = letterBox.getCentreX() + letterWidth * 0.5f + 16.0f;
        if (noteAccidental.isNotEmpty())
        {
            g.setFont (juce::FontOptions (noteBounds.getHeight() * 0.32f).withStyle ("Bold"));
            g.drawText (noteAccidental, juce::Rectangle<float> (sideX, noteBounds.getY() + 26.0f, 90.0f, 90.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (34.0f).withStyle ("Bold"));
        g.drawText (juce::String (noteOctave), juce::Rectangle<float> (sideX, noteBounds.getBottom() - 116.0f, 90.0f, 70.0f),
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (42.0f));
        g.drawText ("play a note", noteBounds, juce::Justification::centred);
    }

    // ---- readout: how far off, and the raw frequency ----
    if (active)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (38.0f).withStyle ("Bold"));
        const auto centsText = std::abs (cents) <= inTuneCents
                                   ? juce::String ("IN TUNE")
                                   : (cents > 0.0f ? "+" : "") + juce::String (cents, 1) + " cents";
        g.drawText (centsText, readoutBounds.removeFromTop (48.0f), juce::Justification::centred);

        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (juce::String (frequencyHz, 2) + " Hz", readoutBounds, juce::Justification::centredTop);
    }
}

void TunerOverlay::timerCallback()
{
    const float hz = getFrequencyHz != nullptr ? getFrequencyHz() : 0.0f;
    const bool rawActive = hz > 0.0f;

    // The analysis updates a few times a second and each estimate carries its own noise, so a raw readout jitters and a
    // hard on/off flickers between updates: fast attack so a note lights up promptly, slow release so it holds.
    const float target = rawActive ? 1.0f : 0.0f;
    activityLevel += (target > activityLevel ? 0.4f : 0.035f) * (target - activityLevel);
    active = activityLevel > 0.5f;

    if (rawActive)
    {
        const auto reading = readingFor (hz);
        noteLetter = reading.letter;
        noteAccidental = reading.accidental;
        noteOctave = reading.octave;
        frequencyHz = hz;
        smoothedCents += 0.3f * (reading.cents - smoothedCents);
        cents = juce::jlimit (-gaugeRangeCents, gaugeRangeCents, smoothedCents);
    }

    repaint();
}

} // namespace openguitarmultifx
