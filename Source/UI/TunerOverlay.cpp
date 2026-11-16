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

    /** A music sharp sign drawn as vector strokes (the embedded Sora has no U+266F, and a plain '#' in the note's font looks
        like a hash). `r` is the box it fills: two slightly slanted verticals and two thick rising bars. */
    void drawSharp (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
    {
        const float stem = r.getWidth() * 0.095f, bar = r.getHeight() * 0.11f;
        g.setColour (colour);
        for (const float fx : { 0.30f, 0.70f })
        {
            juce::Path p;
            const float x = r.getX() + r.getWidth() * fx;
            p.startNewSubPath (x - r.getWidth() * 0.035f, r.getBottom() - r.getHeight() * 0.04f);
            p.lineTo (x + r.getWidth() * 0.035f, r.getY() + r.getHeight() * 0.04f);
            g.strokePath (p, juce::PathStrokeType (stem, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
        for (const float fy : { 0.36f, 0.66f })
        {
            juce::Path p;
            const float y = r.getY() + r.getHeight() * fy;
            p.startNewSubPath (r.getX(), y + r.getHeight() * 0.07f);
            p.lineTo (r.getRight(), y - r.getHeight() * 0.07f);
            g.strokePath (p, juce::PathStrokeType (bar, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
    }

    constexpr float inTuneCents = 5.0f;  // inside this, the display goes green -- the usual tolerance for a tuner
    constexpr float gaugeRangeCents = 50.0f;

    const auto inTuneColour = juce::Colour (0xff3bd68a);
    const auto flatColour   = juce::Colour (0xffff4d6a);

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
    // LED only exists when a pedal is bound or learn is armed
    midiButton.showDot = false;
    midiButton.onClick = [this] { if (onMidiLearnTapped) onMidiLearnTapped(); };

    setSize (1280, 800);
    startTimerHz (60); // drawn on the GPU now: a frame costs next to nothing, and 60 is what the glide is built for
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

void TunerOverlay::setMidiLabel (const juce::String& text, bool armed, bool bound)
{
    midiButton.text = text;
    midiButton.showDot = armed || bound;
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
    midiButton.setBounds (header.removeFromRight (148).withSizeKeepingCentre (148, 48));
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
        // The letter and the sharp are laid out together and the size is chosen so that BOTH fit the card: drawn as one
        // text string at a fixed 0.82 x height the letter alone filled the width, so "C#" was cut off with an ellipsis
        // and the sharp never showed.
        const bool sharp = noteAccidental.isNotEmpty();
        g.setFont (juce::FontOptions (100.0f).withStyle ("Bold"));
        const float letterPerPx = g.getCurrentFont().getStringWidthFloat (noteLetter) / 100.0f; // advance width per pixel of height
        const float sharpPerPx = sharp ? 0.36f : 0.0f;
        const float usableW = noteArea.getWidth() * 0.84f;                                         // leave room for the octave digit
        const float fh = juce::jmin (noteArea.getHeight() * 0.82f, usableW / (letterPerPx + sharpPerPx + 0.02f));
        g.setFont (juce::FontOptions (fh).withStyle ("Bold"));
        const float letterW = g.getCurrentFont().getStringWidthFloat (noteLetter);
        const float sharpW = fh * sharpPerPx;
        const float totalW = letterW + sharpW;
        const float x0 = noteArea.getCentreX() - totalW * 0.5f - noteArea.getWidth() * 0.04f;

        g.setColour (inTuneColour);
        g.drawText (noteLetter, juce::Rectangle<float> (x0, noteArea.getY(), letterW + 8.0f, noteArea.getHeight()).toNearestInt(),
                    juce::Justification::centredLeft, false);
        if (sharp)
            drawSharp (g, juce::Rectangle<float> (x0 + letterW + fh * 0.02f, noteArea.getCentreY() - fh * 0.42f, sharpW * 0.92f, fh * 0.5f),
                       inTuneColour);

        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (48.0f).withStyle ("Bold"));
        g.drawText (juce::String (noteOctave),
                    juce::Rectangle<float> (x0 + totalW + 6.0f, noteArea.getBottom() - 96.0f, 90.0f, 60.0f).toNearestInt(),
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

    // How far the needle has swept, in ticks (one tick = 2.5 cents = 3 degrees). The tick under it lights fractionally,
    // so the glow grows tick by tick smoothly instead of jumping a whole tick at a time.
    const float sweepTicks = active ? std::abs (cents) / 2.5f : 0.0f;

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
            // light the ticks between the centre and the needle's side, the last one only as far as the needle reached
            const int off = i - 20; // negative = flat side
            const bool side = cents < 0.0f ? off < 0 : off > 0;
            const float cover = side ? juce::jlimit (0.0f, 1.0f, sweepTicks - (float) (std::abs (off) - 1)) : 0.0f;
            if (cover > 0.0f)
                col = t.raisedHigh.interpolatedWith (flatColour, cover);
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

    if (active)
    {
        // The needle itself: a thin line from the fan's pivot to past the tick tips, at 1.2 degrees per cent, sub-pixel exact.
        const float deg = juce::jlimit (-60.0f, 60.0f, cents * 1.2f);
        const auto a = juce::MathConstants<float>::pi * (deg - 90.0f) / 180.0f;
        const auto tip = juce::Point<float> (cx + (r1 + 10.0f) * std::cos (a), cy + (r1 + 10.0f) * std::sin (a));
        const auto base = juce::Point<float> (cx + (r0 - 34.0f) * std::cos (a), cy + (r0 - 34.0f) * std::sin (a));
        const auto needleColour = tuningColour (cents, active);
        g.setColour (needleColour.withAlpha (0.28f));
        g.drawLine ({ base, tip }, 14.0f);
        g.setColour (needleColour);
        g.drawLine ({ base, tip }, 5.0f);
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

    // A real strobe: alternating bands that turn at a speed proportional to the error (sharp clockwise, flat the other way)
    // and stand still when the note is in tune. `stripePhaseDeg` is advanced every frame by the timer, so the ring keeps
    // moving smoothly between detector updates.
    const bool inTune = active && std::abs (cents) <= inTuneCents;
    constexpr int segments = 72;
    constexpr float bandDeg = 30.0f; // 6 lit bands, 6 dark
    for (int i = 0; i < segments; ++i)
    {
        const float deg = (float) i * (360.0f / segments);
        const auto a = juce::MathConstants<float>::pi * (deg - 90.0f) / 180.0f;
        const auto p0 = juce::Point<float> (cx + r0 * std::cos (a), cy + r0 * std::sin (a));
        const auto p1 = juce::Point<float> (cx + r1 * std::cos (a), cy + r1 * std::sin (a));

        const float band = std::fmod (deg - stripePhaseDeg + 720.0f, 2.0f * bandDeg);
        const bool litBand = active && ! inTune && band < bandDeg;
        const float fade = juce::jlimit (0.0f, 1.0f, std::abs (cents) / 12.0f); // faint when nearly in tune
        g.setColour (litBand ? t.raisedHigh.interpolatedWith (flatColour, fade) : t.raisedHigh.withAlpha (0.85f));
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
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastTickMs > 0.0 ? juce::jlimit (0.004f, 0.1f, (float) ((nowMs - lastTickMs) * 0.001)) : 1.0f / 60.0f;
    lastTickMs = nowMs;

    const float hz = getFrequencyHz != nullptr ? getFrequencyHz() : 0.0f;
    const bool rawActive = hz > 0.0f;

    // The analysis updates a few times a second and each estimate carries its own noise, so a raw readout jitters and a
    // hard on/off flickers between updates: fast attack so a note lights up promptly, slow release so it holds.
    // Rates are per second, not per frame, so the feel does not depend on how often this timer really fires.
    const float target = rawActive ? 1.0f : 0.0f;
    activityLevel += (1.0f - std::exp (-dt * (target > activityLevel ? 24.0f : 2.2f))) * (target - activityLevel);
    active = activityLevel > 0.5f;

    if (rawActive)
    {
        const auto reading = readingFor (hz);
        if (reading.letter != noteLetter || reading.accidental != noteAccidental || reading.octave != noteOctave)
        {
            // another note: the old cents value belonged to the previous one, so start the glide from where this one is
            centsStage1 = cents = juce::jlimit (-gaugeRangeCents, gaugeRangeCents, reading.cents);
        }
        noteLetter = reading.letter;
        noteAccidental = reading.accidental;
        noteOctave = reading.octave;
        frequencyHz = hz;
        targetCents = juce::jlimit (-gaugeRangeCents, gaugeRangeCents, reading.cents);
    }

    // Two cascaded one-pole filters (time constant ~55 ms each): the needle follows within a tenth of a second and its
    // velocity is continuous, so a new detector reading bends the motion instead of jerking it.
    const float k = 1.0f - std::exp (-dt / 0.055f);
    centsStage1 += k * (targetCents - centsStage1);
    cents += k * (centsStage1 - cents);

    // The strobe ring turns at a speed proportional to how far off the note is, and stands still when in tune.
    stripePhaseDeg = std::fmod (stripePhaseDeg + (active ? cents : 0.0f) * 9.0f * dt + 360.0f, 360.0f);

    repaint (getLocalBounds().withTrimmedTop (76)); // the header never changes between frames
}

} // namespace openguitarmultifx
