#include "TunerOverlay.h"

#include "OpenGuitarMultiFxLookAndFeel.h"
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
            return juce::Colours::white.withAlpha (0.25f);
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
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setFont (14.0f);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    hintLabel.setText ("Chromatic - any string, any tuning", juce::dontSendNotification);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    // Big enough to read from a few feet away on a stage (user request 2026-09-22). OverlayHost keeps whatever size the
    // content picks for itself -- without this it collapses to its 240px minimum.
    setSize (880, 560);

    startTimerHz (30);
}

TunerOverlay::~TunerOverlay()
{
    if (onDestroyed != nullptr)
        onDestroyed();
}

void TunerOverlay::resized()
{
    auto area = getLocalBounds().reduced (28, 22);

    auto top = area.removeFromTop (touch::minTapTarget);
    closeButton.setBounds (top.removeFromRight (120).withSizeKeepingCentre (120, touch::minTapTarget));
    titleLabel.setBounds (top.removeFromLeft (110));
    hintLabel.setBounds (top);

    area.removeFromTop (10);
    gaugeBounds = area.removeFromTop (96).toFloat();   // the tick ruler + needle
    noteBounds = area.removeFromTop (250).toFloat();   // the big letter
    readoutBounds = area.toFloat();                    // cents + Hz
}

void TunerOverlay::paint (juce::Graphics& g)
{
    const auto colour = tuningColour (cents, active);

    // ---- the ruler: ticks every 5 cents across +-50, tall at the centre and at +-inTuneCents ----
    const float centreX = gaugeBounds.getCentreX();
    const float halfWidth = gaugeBounds.getWidth() * 0.5f - 30.0f;
    const float baseY = gaugeBounds.getBottom() - 22.0f;

    for (int tick = -10; tick <= 10; ++tick)
    {
        const float value = (float) tick * 5.0f;
        const float x = centreX + (value / gaugeRangeCents) * halfWidth;
        const bool major = (tick % 5) == 0;
        const float height = tick == 0 ? 40.0f : (major ? 24.0f : 13.0f);
        g.setColour (juce::Colours::white.withAlpha (tick == 0 ? 0.55f : (major ? 0.30f : 0.16f)));
        g.fillRoundedRectangle (x - (tick == 0 ? 2.0f : 1.5f), baseY - height, tick == 0 ? 4.0f : 3.0f, height, 1.5f);
    }

    // The in-tune window, so "dead centre" is a target you can see rather than a line you have to guess at.
    const float windowHalf = (inTuneCents / gaugeRangeCents) * halfWidth;
    g.setColour ((active && std::abs (cents) <= inTuneCents ? juce::Colour (0xff35d07f) : juce::Colours::white).withAlpha (0.10f));
    g.fillRoundedRectangle (centreX - windowHalf, baseY - 44.0f, windowHalf * 2.0f, 48.0f, 6.0f);

    // ---- flat / sharp markers, lit on the side you are on ----
    const bool flat = active && cents < -inTuneCents;
    const bool sharp = active && cents > inTuneCents;
    g.setFont (juce::Font (34.0f, juce::Font::bold));
    g.setColour (flat ? colour : juce::Colours::white.withAlpha (0.18f));
    g.drawText ("b", juce::Rectangle<float> (gaugeBounds.getX(), baseY - 44.0f, 26.0f, 44.0f), juce::Justification::centred);
    g.setColour (sharp ? colour : juce::Colours::white.withAlpha (0.18f));
    g.drawText ("#", juce::Rectangle<float> (gaugeBounds.getRight() - 26.0f, baseY - 44.0f, 26.0f, 44.0f), juce::Justification::centred);

    // ---- the needle ----
    if (active)
    {
        const float x = centreX + juce::jlimit (-1.0f, 1.0f, cents / gaugeRangeCents) * halfWidth;
        g.setColour (colour);
        g.fillRoundedRectangle (x - 4.0f, baseY - 52.0f, 8.0f, 60.0f, 4.0f);
        g.fillEllipse (x - 11.0f, baseY + 2.0f, 22.0f, 22.0f);
    }

    // ---- the note itself: the thing you read from across a stage ----
    if (active)
    {
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (noteBounds.getHeight() * 0.86f).withStyle ("Bold")));
        const auto letterBox = noteBounds.withTrimmedRight (noteBounds.getWidth() * 0.12f);
        g.drawText (noteLetter, letterBox, juce::Justification::centred);

        // Sharp sign and octave ride alongside the letter rather than widening it, so the letter keeps its size.
        const float letterWidth = g.getCurrentFont().getStringWidthFloat (noteLetter);
        const float sideX = letterBox.getCentreX() + letterWidth * 0.5f + 12.0f;
        if (noteAccidental.isNotEmpty())
        {
            g.setFont (juce::Font (juce::FontOptions (noteBounds.getHeight() * 0.34f).withStyle ("Bold")));
            g.drawText (noteAccidental, juce::Rectangle<float> (sideX, noteBounds.getY() + 22.0f, 80.0f, 80.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (30.0f)));
        g.drawText (juce::String (noteOctave), juce::Rectangle<float> (sideX, noteBounds.getBottom() - 104.0f, 80.0f, 60.0f),
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.setFont (juce::Font (juce::FontOptions (40.0f)));
        g.drawText ("play a note", noteBounds, juce::Justification::centred);
    }

    // ---- readout: how far off, and the raw frequency ----
    if (active)
    {
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (40.0f).withStyle ("Bold")));
        const auto centsText = std::abs (cents) <= inTuneCents
                                   ? juce::String ("IN TUNE")
                                   : (cents > 0.0f ? "+" : "") + juce::String (cents, 1) + " cents";
        g.drawText (centsText, readoutBounds.removeFromTop (46.0f), juce::Justification::centred);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (20.0f)));
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
