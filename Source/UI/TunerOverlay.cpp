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

    struct StringTarget { const char* note; const char* octave; float frequencyHz; };
    const StringTarget stringTargets[] = {
        { "E", "2", 82.41f }, { "A", "2", 110.00f }, { "D", "3", 146.83f },
        { "G", "3", 196.00f }, { "B", "3", 246.94f }, { "E", "4", 329.63f }
    };

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
    titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);

    referenceLabel.setJustificationType (juce::Justification::centred);
    referenceLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    referenceLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
    referenceLabel.setText ("A = 440 Hz", juce::dontSendNotification);
    addAndMakeVisible (referenceLabel);

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onPopOverlay) onPopOverlay(); };

    setSize (1180, 780);

    startTimerHz (30);
}

TunerOverlay::~TunerOverlay()
{
    if (onDestroyed != nullptr)
        onDestroyed();
}

void TunerOverlay::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (76).reduced (20, 0);
    closeButton.setBounds (header.removeFromRight (52).withSizeKeepingCentre (52, 52));
    referenceLabel.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 48));
    titleLabel.setBounds (header.removeFromLeft (220));

    area.removeFromBottom (132);
    const int contentHeight = 390;
    const int topPad = juce::jmax (0, (area.getHeight() - contentHeight) / 2);
    area.removeFromTop (topPad);

    noteBounds = area.removeFromTop (220).toFloat();
    readoutBounds = area.removeFromTop (56).toFloat();

    auto stripArea = area.removeFromTop (112);
    gaugeBounds = juce::Rectangle<float> (stripArea.getCentreX() - 450.0f,
                                          stripArea.getY() + 8.0f, 900.0f, 96.0f);
}

void TunerOverlay::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto green = juce::Colour (0xff3bd68a);
    const auto colour = tuningColour (cents, active);

    g.fillAll (t.background);

    const auto pill = referenceLabel.getBounds().toFloat();
    if (pill.getWidth() > 0.0f)
    {
        g.setColour (t.raised);
        g.fillRoundedRectangle (pill, t.smallRadius);
        g.setColour (t.lineStrong);
        g.drawRoundedRectangle (pill.reduced (0.5f), t.smallRadius, 1.0f);
    }

    if (active)
    {
        g.setColour (green);
        g.setFont (juce::FontOptions ((int) (noteBounds.getHeight() * 0.72f)).withStyle ("Bold"));
        const juce::String symbol = noteLetter + noteAccidental;
        const auto symbolBounds = noteBounds.withTrimmedRight (noteBounds.getWidth() * 0.18f);
        g.drawText (symbol, symbolBounds.toNearestInt(), juce::Justification::centred);

        const float symbolWidth = g.getCurrentFont().getStringWidthFloat (symbol);
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (42.0f).withStyle ("Bold"));
        g.drawText (juce::String (noteOctave),
                    juce::Rectangle<float> (symbolBounds.getCentreX() + symbolWidth * 0.52f,
                                            noteBounds.getCentreY() - 24.0f, 90.0f, 56.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (t.textDim);
        g.setFont (juce::FontOptions (54.0f));
        g.drawText ("play a note", noteBounds.toNearestInt(), juce::Justification::centred);
    }

    if (active)
    {
        const auto centsText = std::abs (cents) <= inTuneCents
                                   ? juce::String ("0.0 cents")
                                   : (cents > 0.0f ? "+" : "") + juce::String (cents, 1) + " cents";
        g.setColour (t.textMuted);
        g.setFont (juce::FontOptions (22.0f));
        const auto prefix = juce::String (frequencyHz, 2) + " Hz  -  ";
        const auto prefixWidth = g.getCurrentFont().getStringWidthFloat (prefix);
        g.drawText (prefix, readoutBounds.toNearestInt(), juce::Justification::centred);
        g.setColour (std::abs (cents) <= inTuneCents ? green : colour);
        g.setFont (juce::FontOptions (24.0f).withStyle ("Bold"));
        g.drawText (centsText,
                    juce::Rectangle<float> (readoutBounds.getCentreX() - (prefixWidth + g.getCurrentFont().getStringWidthFloat (centsText)) * 0.5f + prefixWidth,
                                            readoutBounds.getY(), 180.0f, readoutBounds.getHeight()).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    const float centreX = gaugeBounds.getCentreX();
    const float halfWidth = gaugeBounds.getWidth() * 0.5f - 34.0f;
    const float tickBase = gaugeBounds.getBottom() - 24.0f;

    for (int tick = -10; tick <= 10; ++tick)
    {
        const float value = (float) tick * 5.0f;
        const float x = centreX + (value / gaugeRangeCents) * halfWidth;
        const bool quarter = (tick % 5) == 0;
        const bool major = (tick % 10) == 0;
        const float height = major ? 58.0f : (quarter ? 36.0f : 16.0f);
        g.setColour (major ? green.withAlpha (0.9f) : (quarter ? t.textMuted.withAlpha (0.55f) : t.lineStrong));
        g.fillRoundedRectangle (x - (major ? 2.0f : 1.5f), tickBase - height,
                                major ? 4.0f : 3.0f, height, 1.5f);
    }

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (t.textDim);
    for (int label = -2; label <= 2; ++label)
    {
        const float value = (float) label * 25.0f;
        const float x = centreX + (value / gaugeRangeCents) * halfWidth;
        juce::String text;
        if (label < 0) text = "-" + juce::String ((int) -value);
        else if (label > 0) text = "+" + juce::String ((int) value);
        else text = "0";
        g.drawText (text, juce::Rectangle<float> (x - 24.0f, gaugeBounds.getBottom() - 18.0f, 48.0f, 16.0f),
                    juce::Justification::centred);
    }

    if (active)
    {
        const float x = centreX + juce::jlimit (-1.0f, 1.0f, cents / gaugeRangeCents) * halfWidth;
        g.setColour (green.withAlpha (0.18f));
        g.fillRoundedRectangle (x - 12.0f, tickBase - 88.0f, 24.0f, 100.0f, 8.0f);
        g.setColour (green.withAlpha (0.35f));
        g.fillEllipse (x - 16.0f, tickBase - 4.0f, 32.0f, 32.0f);
        g.setColour (green);
        g.fillRoundedRectangle (x - 4.0f, tickBase - 82.0f, 8.0f, 88.0f, 4.0f);
        g.fillEllipse (x - 10.0f, tickBase + 2.0f, 20.0f, 20.0f);
    }

    const int tileW = 120, tileH = 86, gap = 12;
    const int totalW = 6 * tileW + 5 * gap;
    int nearestString = -1;
    float nearestCents = 1000.0f;
    if (active && frequencyHz > 0.0f)
    {
        for (int i = 0; i < 6; ++i)
        {
            const float off = 1200.0f * std::log2 (frequencyHz / stringTargets[i].frequencyHz);
            if (std::abs (off) < std::abs (nearestCents))
            {
                nearestCents = off;
                nearestString = i;
            }
        }
        if (std::abs (nearestCents) > gaugeRangeCents)
            nearestString = -1;
    }

    const int firstX = (getWidth() - totalW) / 2;
    const int tileY = getHeight() - 28 - tileH;
    for (int i = 0; i < 6; ++i)
    {
        const auto tile = juce::Rectangle<float> ((float) (firstX + i * (tileW + gap)), (float) tileY,
                                                  (float) tileW, (float) tileH);
        const bool on = i == nearestString;
        const bool ok = on && std::abs (nearestCents) <= inTuneCents;
        g.setColour (t.raised);
        g.fillRoundedRectangle (tile, t.radius);
        g.setColour (on ? green : t.line);
        g.drawRoundedRectangle (tile.reduced (0.5f), t.radius, on ? 2.0f : 1.0f);
        if (on)
        {
            g.setColour (green.withAlpha (0.16f));
            g.drawRoundedRectangle (tile.expanded (2.0f).reduced (1.0f), t.radius + 2.0f, 2.0f);
        }

        g.setColour (on ? green : t.text);
        g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
        g.drawText (stringTargets[i].note, tile.withTrimmedBottom (34.0f).toNearestInt(), juce::Justification::centred);
        g.setColour (ok ? green : t.textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (stringTargets[i].note) + stringTargets[i].octave,
                    tile.withTrimmedTop (tileH - 30).toNearestInt(), juce::Justification::centred);
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
