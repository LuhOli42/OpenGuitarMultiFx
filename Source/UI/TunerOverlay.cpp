#include "TunerOverlay.h"

#include "OverlayChrome.h"
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

    const auto inTuneColour = juce::Colour (0xff3bd68a);
    const auto flatColour   = juce::Colour (0xffff4d6a);
    // single out-of-tune colour (red) on both sides — flat lights left, sharp lights right

    juce::Colour tuningColour (float cents, bool active)
    {
        if (! active)
            return theme::current().textDim;
        if (std::abs (cents) <= inTuneCents)
            return inTuneColour;
        return flatColour;
    }
}

TunerOverlay::TunerOverlay()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setText ("Tuner", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, theme::current().text);

    needleButton.text = "Needle";
    needleButton.selectedStyle = overlaychrome::SelectedStyle::tab;
    needleButton.chromeless = true;
    needleButton.onClick = [this]
    {
        if (! strobeMode)
            return;
        strobeMode = false;
        needleButton.selected = true;
        strobeButton.selected = false;
        repaint();
        if (onStrobeModeChanged) onStrobeModeChanged (false);
    };
    addAndMakeVisible (needleButton);

    strobeButton.text = "Strobe";
    strobeButton.selectedStyle = overlaychrome::SelectedStyle::tab;
    strobeButton.chromeless = true;
    strobeButton.onClick = [this]
    {
        if (strobeMode)
            return;
        strobeMode = true;
        needleButton.selected = false;
        strobeButton.selected = true;
        repaint();
        if (onStrobeModeChanged) onStrobeModeChanged (true);
    };
    addAndMakeVisible (strobeButton);
    needleButton.selected = true;

    referenceLabel.setJustificationType (juce::Justification::centred);
    referenceLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    referenceLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
    referenceLabel.setText ("A = 440 Hz", juce::dontSendNotification);
    addAndMakeVisible (referenceLabel);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    addAndMakeVisible (muteButton);
    muteButton.text = "Mute";
    muteButton.dotColour = flatColour;   // mockup: the red dot appears only while Mute is on
    muteButton.onClick = [this]
    {
        muteButton.selected = ! muteButton.selected;
        muteButton.showDot = muteButton.selected;
        muteButton.borderOverride = muteButton.selected ? flatColour : juce::Colour();
        muteButton.repaint();
        repaint();
        if (onMuteToggled) onMuteToggled (muteButton.selected);
    };

    addAndMakeVisible (midiButton);
    midiButton.text = "MIDI Learn";
    midiButton.showDot = true;
    midiButton.dotColour = theme::current().textMuted;
    midiButton.onClick = [this] { if (onMidiLearnTapped) onMidiLearnTapped(); };

    setSize (1280, 800);
    startTimerHz (30);
}

TunerOverlay::~TunerOverlay()
{
    if (onDestroyed != nullptr)
        onDestroyed();
}

void TunerOverlay::setMuteState (bool muted)
{
    muteButton.selected = muted;
    muteButton.showDot = muted;
    muteButton.borderOverride = muted ? flatColour : juce::Colour();
    muteButton.repaint();
    repaint();
}

void TunerOverlay::setMidiLabel (const juce::String& text, bool armed)
{
    midiButton.text = text;
    midiButton.dotColour = armed ? flatColour : theme::current().textMuted;
    midiButton.borderOverride = armed ? flatColour : juce::Colour();
    midiButton.selected = armed;
    midiButton.repaint();
}

void TunerOverlay::setStrobeMode (bool strobe)
{
    strobeMode = strobe;
    needleButton.selected = ! strobe;
    strobeButton.selected = strobe;
    repaint();
}

void TunerOverlay::resized()
{
    auto header = getLocalBounds().removeFromTop (76).reduced (16, 0);
    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    header.removeFromRight (8);
    muteButton.setBounds (header.removeFromRight (96).withSizeKeepingCentre (96, 48));
    header.removeFromRight (8);
    midiButton.setBounds (header.removeFromRight (178).withSizeKeepingCentre (178, 48));
    header.removeFromRight (8);
    referenceLabel.setBounds (header.removeFromRight (118).withSizeKeepingCentre (118, 48));

    titleLabel.setBounds (header.removeFromLeft (90));
    header.removeFromLeft (12);
    // Mockup .sg: one container (raised fill, line border, 3px padding, 3px gap) holding both segments
    segBounds = header.removeFromLeft (190).withSizeKeepingCentre (190, 54).toFloat();
    auto segInner = segBounds.reduced (3.0f);
    needleButton.setBounds (segInner.removeFromLeft (segInner.getWidth() * 0.5f - 1.5f).toNearestInt());
    segInner.removeFromLeft (3.0f);
    strobeButton.setBounds (segInner.toNearestInt());

    auto body = getLocalBounds().withTrimmedTop (76).reduced (0, 12);
    noteBounds = body.removeFromLeft (430).reduced (24, 0).toFloat();
    meterBounds = body.reduced (40, 24).toFloat();
    readoutBounds = noteBounds.withTrimmedTop (noteBounds.getHeight() * 0.62f);
}

void TunerOverlay::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    g.fillAll (t.background);

    g.setColour (t.surface);
    g.fillRect (getLocalBounds().withHeight (76));
    g.setColour (t.line);
    g.drawHorizontalLine (75.5f, 0.0f, (float) getWidth());

    const auto pill = referenceLabel.getBounds().toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (pill, t.smallRadius);
    g.setColour (t.lineStrong);
    g.drawRoundedRectangle (pill.reduced (0.5f), t.smallRadius, 1.0f);

    // segmented container behind Needle/Strobe (mockup .sg)
    g.setColour (t.raised);
    g.fillRoundedRectangle (segBounds, t.smallRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (segBounds.reduced (0.5f), t.smallRadius, 1.0f);

    paintNoteBlock (g, noteBounds);
    if (strobeMode)
        paintStrobeMeter (g, meterBounds);
    else
        paintNeedleMeter (g, meterBounds);

    if (muteButton.selected)
    {
        auto badge = juce::Rectangle<float> (getWidth() * 0.5f - 110.0f, getHeight() - 66.0f, 220.0f, 44.0f);
        g.setColour (flatColour);
        g.fillRoundedRectangle (badge, 22.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f).withStyle ("Bold"));
        g.drawText ("SOUND MUTED", badge.toNearestInt(), juce::Justification::centred);
    }
}

void TunerOverlay::paintNoteBlock (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& t = theme::current();
    const auto colour = tuningColour (cents, active);

    auto noteArea = bounds.withHeight (bounds.getHeight() * 0.6f);
    if (active)
    {
        g.setColour (inTuneColour);
        g.setFont (juce::FontOptions ((int) (noteArea.getHeight() * 0.82f)).withStyle ("Bold"));
        const juce::String symbol = noteLetter + noteAccidental;
        const auto symbolBounds = noteArea.withTrimmedRight (noteArea.getWidth() * 0.15f);
        g.drawText (symbol, symbolBounds.toNearestInt(), juce::Justification::centred);

        const float symbolWidth = g.getCurrentFont().getStringWidthFloat (symbol);
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (48.0f).withStyle ("Bold"));
        g.drawText (juce::String (noteOctave),
                    juce::Rectangle<float> (symbolBounds.getCentreX() + symbolWidth * 0.55f,
                                            symbolBounds.getBottom() - 96.0f, 90.0f, 60.0f).toNearestInt(),
                    juce::Justification::centredLeft);

        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (juce::String (frequencyHz, 2) + " Hz",
                    readoutBounds.withTrimmedTop (-4.0f).toNearestInt(), juce::Justification::centred);

        const auto centsText = std::abs (cents) <= inTuneCents
                                   ? juce::String ("0.0")
                                   : (cents > 0.0f ? "+" : "") + juce::String (cents, 1);
        g.setColour (colour);
        g.setFont (juce::FontOptions (34.0f).withStyle ("Bold"));
        g.drawText (centsText + " " + juce::String (juce::CharPointer_UTF8 ("\xc2\xa2")),
                    readoutBounds.withTrimmedTop (30.0f).toNearestInt(), juce::Justification::centred);
    }
    else
    {
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (54.0f));
        g.drawText ("play a note", bounds.toNearestInt(), juce::Justification::centred);
    }
}

void TunerOverlay::paintNeedleMeter (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& t = theme::current();
    // Radial fan, TU-3 style: ticks -60..+60 degrees map to -50..+50 cents.
    const float r0 = bounds.getHeight() * 0.40f;
    const float r1 = bounds.getHeight() * 0.84f;
    const float cy = bounds.getBottom() - bounds.getHeight() * 0.06f;
    const float cx = bounds.getCentreX();

    const int litHalf = active ? (int) std::round (std::abs (cents) / 2.5f) : 0;

    for (int i = 0; i <= 40; ++i)
    {
        const float deg = -60.0f + (float) i * 3.0f;
        const auto a = juce::MathConstants<float>::pi * (deg - 90.0f) / 180.0f;
        const auto p0 = juce::Point<float> (cx + r0 * std::cos (a), cy + r0 * std::sin (a));
        const auto p1 = juce::Point<float> (cx + r1 * std::cos (a), cy + r1 * std::sin (a));
        const bool centre = i == 20;
        const bool major = (i % 4) == 0;
        const auto q0 = centre ? p0.withY (p0.y - 14.0f) : p0;
        const auto q1 = centre ? p1.withY (p1.y + 14.0f) : (major ? p1 : p0 + (p1 - p0) * 0.66f);

        juce::Colour col = centre ? inTuneColour : t.raisedHigh;
        float alpha = centre ? 1.0f : 0.85f;
        if (active && ! centre)
        {
            // light the ticks between the centre and the needle's side
            const int off = i - 20; // negative = flat side
            const bool lit = cents < 0.0f ? (off >= litHalf * -1 && off <= 0)
                                          : (off >= 0 && off <= litHalf);
            if (lit && off != 0)
                col = flatColour;
        }
        g.setColour (col.withAlpha (alpha));
        g.drawLine ({ q0, q1 }, centre ? 10.0f : 7.0f);
    }

    if (active && std::abs (cents) <= inTuneCents)
    {
        g.setColour (inTuneColour.withAlpha (0.30f));
        const auto a = juce::MathConstants<float>::pi * -0.5f;
        const auto p1 = juce::Point<float> (cx + (r1 + 14.0f) * std::cos (a), cy + (r1 + 14.0f) * std::sin (a));
        const auto p0 = juce::Point<float> (cx + (r0 - 14.0f) * std::cos (a), cy + (r0 - 14.0f) * std::sin (a));
        g.drawLine ({ p0, p1 }, 22.0f);
    }

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (t.textDim);
    for (int label = -2; label <= 2; ++label)
    {
        if (label == 0)
            continue;
        const float deg = (float) label * 30.0f;
        const auto a = juce::MathConstants<float>::pi * (deg - 90.0f) / 180.0f;
        const float r = r0 - 30.0f;   // inside the fan, clear of the tick tips
        const auto p = juce::Point<float> (cx + r * std::cos (a), cy + r * std::sin (a));
        const auto text = (label < 0 ? "-" : "+") + juce::String (std::abs (label) * 25);
        g.drawText (text, juce::Rectangle<float> (p.x - 24.0f, p.y - 10.0f, 48.0f, 18.0f),
                    juce::Justification::centred);
    }
}

void TunerOverlay::paintStrobeMeter (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& t = theme::current();
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float r0 = bounds.getHeight() * 0.33f;
    const float r1 = bounds.getHeight() * 0.46f;

    // The lit wedge drifts around the ring proportional to cents: its angular offset from the top
    // marker reads like the strobe disc's rotation direction and speed frozen in a frame.
    const float drift = active ? cents * 3.6f : 0.0f; // cents -> degrees of offset
    const float wedgeCentre = drift;                  // deg 0 is the top marker; flat drifts left, sharp right
    const bool inTune = active && std::abs (cents) <= inTuneCents;

    for (int i = 0; i < 72; ++i)
    {
        const float deg = (float) i * 5.0f;
        const auto a = juce::MathConstants<float>::pi * (deg - 90.0f) / 180.0f;
        const auto p0 = juce::Point<float> (cx + r0 * std::cos (a), cy + r0 * std::sin (a));
        const auto p1 = juce::Point<float> (cx + r1 * std::cos (a), cy + r1 * std::sin (a));

        float diff = std::fmod (deg - wedgeCentre + 540.0f, 360.0f) - 180.0f;
        const bool lit = active && ! inTune && std::abs (diff) <= 30.0f;
        g.setColour (lit ? flatColour : t.raisedHigh.withAlpha (0.85f));
        g.drawLine ({ p0, p1 }, 9.0f);
    }

    // fixed top marker: the "zero" the wedge settles under when the note locks
    const auto a = juce::MathConstants<float>::pi * -0.5f;
    const auto m0 = juce::Point<float> (cx + (r0 - 14.0f) * std::cos (a), cy + (r0 - 14.0f) * std::sin (a));
    const auto m1 = juce::Point<float> (cx + (r1 + 14.0f) * std::cos (a), cy + (r1 + 14.0f) * std::sin (a));
    g.setColour (inTuneColour.withAlpha (0.30f));
    g.drawLine ({ m0, m1 }, 24.0f);
    g.setColour (inTuneColour);
    g.drawLine ({ m0, m1 }, 12.0f);
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
