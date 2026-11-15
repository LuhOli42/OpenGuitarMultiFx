#include "OpenGuitarMultiFxLookAndFeel.h"

#include "Theme.h"

#include <BinaryData.h>

#include <cmath>

namespace openguitarmultifx
{
namespace
{
    class SliderValueBox final : public juce::Label
    {
    public:
        void paint (juce::Graphics& g) override
        {
            const auto& t = theme::current();
            const auto bounds = getLocalBounds().toFloat();
            const auto visual = bounds.withY (bounds.getCentreY() - 18.0f).withHeight (36.0f);
            const float radius = t.id == theme::Id::console ? 3.0f : 9.0f;

            g.setColour (t.raised);
            g.fillRoundedRectangle (visual, radius);
            g.setColour (t.line);
            g.drawRoundedRectangle (visual, radius, 1.0f);
            juce::Label::paint (g);
        }
    };
}

// The fallback accent, used until the first applySnapshotAccent() call
// moves it to the live snapshot's colour (see the header).
juce::Colour OpenGuitarMultiFxLookAndFeel::appAccent { 0xff42a2c8 };

OpenGuitarMultiFxLookAndFeel::OpenGuitarMultiFxLookAndFeel()
{
    regular   = juce::Typeface::createSystemTypefaceFor (BinaryData::SoraRegular_ttf,
                                                           (size_t) BinaryData::SoraRegular_ttfSize);
    bold      = juce::Typeface::createSystemTypefaceFor (BinaryData::SoraBold_ttf,
                                                           (size_t) BinaryData::SoraBold_ttfSize);
    extraBold = juce::Typeface::createSystemTypefaceFor (BinaryData::SoraExtraBold_ttf,
                                                           (size_t) BinaryData::SoraExtraBold_ttfSize);

    applyTheme();
}

void OpenGuitarMultiFxLookAndFeel::applyTheme()
{
    // Colour setup -- per user feedback 2026-09-10 ("muito amador", buttons
    // and contrast called out specifically): the stock widgets must come from
    // the same surface family as every custom-painted panel, or the two
    // different "greys" read as a visible seam. Since the three selectable
    // themes (Theme.h) both families come from the same tokens, with one
    // shared accent (getAppAccentColour()) for anything interactive.
    const auto& t = theme::current();

    setColour (juce::ResizableWindow::backgroundColourId, t.background);

    setColour (juce::TextButton::buttonColourId, t.raised);
    setColour (juce::TextButton::textColourOnId, t.text);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::ComboBox::backgroundColourId, t.raised);
    setColour (juce::ComboBox::outlineColourId, t.lineStrong);
    setColour (juce::ComboBox::textColourId, t.text);

    setColour (juce::Label::textColourId, t.text);
    setColour (juce::TextEditor::backgroundColourId, t.raised);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.lineStrong);
    setColour (juce::ListBox::backgroundColourId, t.surface);

    setColour (juce::Slider::rotarySliderOutlineColourId, t.lineStrong);
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxBackgroundColourId, t.raised);
    setColour (juce::Slider::textBoxOutlineColourId, t.line);
    setColour (juce::Slider::trackColourId, t.raisedHigh);
    setColour (juce::Slider::backgroundColourId, t.raised);

    setColour (juce::ToggleButton::tickDisabledColourId, t.textDim);
    setColour (juce::ToggleButton::textColourId, t.text);

    setColour (juce::PopupMenu::backgroundColourId, t.surface);
    setColour (juce::PopupMenu::textColourId, t.text);

        applyAccentColours();
}

void OpenGuitarMultiFxLookAndFeel::applyAccentColours()
{
    const auto accent = appAccent;

    setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.35f));
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ScrollBar::thumbColourId, accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));

    // Default for the per-button accent override (see the header) -- any
    // button that hasn't had accentColourId set on it, or on a parent,
    // resolves here via Component::findColour()'s walk-up-to-LookAndFeel
    // fallback.
    setColour (accentColourId, accent);
}

void OpenGuitarMultiFxLookAndFeel::setAppAccentColour (juce::Colour colour)
{
    appAccent = colour;
    applyAccentColours();
}

juce::Typeface::Ptr OpenGuitarMultiFxLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // A real bold file, not JUCE's algorithmic embolden of the regular
    // weight -- looks meaningfully better, especially at the large sizes
    // the preset name/number use (see MainComponent).
    return font.isBold() ? bold : regular;
}

juce::Font OpenGuitarMultiFxLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (20.0f));
}

juce::Label* OpenGuitarMultiFxLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = new SliderValueBox();
    label->setJustificationType (juce::Justification::centred);
    label->setEditable (true, true, false);
    label->setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Slider::SliderLayout OpenGuitarMultiFxLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);
    if (slider.getSliderStyle() == juce::Slider::LinearVertical
        && slider.getProperties().getWithDefault ("graphicEqFullTrack", false))
        layout.sliderBounds = slider.getLocalBounds();

    if (slider.getProperties().getWithDefault ("masterEqTrackGap", false))
    {
        layout.sliderBounds = layout.sliderBounds.expanded (getSliderThumbRadius (slider), 0);
        layout.sliderBounds.removeFromRight (8);
    }
    return layout;
}

void OpenGuitarMultiFxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                       float sliderPos, const float rotaryStartAngle,
                                                       const float rotaryEndAngle, juce::Slider& slider)
{
    const auto& t = theme::current();
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (8);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const bool glowStyle = t.blockStyle == theme::BlockStyle::glow;
    const bool discStyle = t.blockStyle == theme::BlockStyle::filled;

    // Small footer knobs (Master): the prototype's 44 px ring -- a 5 px track and a round-capped value arc, nothing
    // inside. The full knob recipe below assumes a ~110 px cell and collapses into a blob at this size.
    if (slider.getProperties().getWithDefault ("compactRing", false))
    {
        const auto box = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float ringW = 5.0f;
        const float r = juce::jmin (box.getWidth(), box.getHeight()) * 0.5f - ringW * 0.5f - 2.0f;
        const auto c = box.getCentre();
        auto ring = [&] (float from, float to, juce::Colour colour)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        ring (0.0f, juce::MathConstants<float>::twoPi, t.raisedHigh);
        if (slider.isEnabled() && toAngle > rotaryStartAngle + 0.001f)
            ring (rotaryStartAngle, toAngle, accent);
        return;
    }

    // The arc ring sits near the edge; the Bold Blocks disc fills the area
    // inside it. Geometry mirrors LookAndFeel_V4's drawRotarySlider.
    const float lineW = discStyle ? 5.0f : 6.0f;
    const float arcRadius = radius - lineW * 0.5f - (discStyle ? 4.0f : 8.0f);

    auto strokeArc = [&g] (juce::Point<float> c, float r, float from, float to, float thickness, juce::Colour colour)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    strokeArc (centre, arcRadius, rotaryStartAngle, rotaryEndAngle, lineW, t.raisedHigh);

    if (slider.isEnabled())
    {
        if (glowStyle)
        {
            // Stage Glass: the value arc glows -- a wide faint pass under a
            // crisp core, like the block icons' drop-shadow in card 2.
            // Prototype: drop-shadow(0 0 6px accent). Approximated with stacked translucent strokes (no blur pass,
            // which would cost a box blur per knob per repaint).
            strokeArc (centre, arcRadius, rotaryStartAngle, toAngle, lineW * 3.4f, accent.withAlpha (0.10f));
            strokeArc (centre, arcRadius, rotaryStartAngle, toAngle, lineW * 2.4f, accent.withAlpha (0.18f));
            strokeArc (centre, arcRadius, rotaryStartAngle, toAngle, lineW * 1.6f, accent.withAlpha (0.30f));
            strokeArc (centre, arcRadius, rotaryStartAngle, toAngle, lineW, accent);
        }
        else
        {
            strokeArc (centre, arcRadius, rotaryStartAngle, toAngle, lineW, accent);
        }
    }

    if (discStyle)
    {
        // Bold Blocks: a solid disc in the snapshot accent with a pointer --
        // the knob IS the colour, like the chain blocks.
        const float discRadius = arcRadius - lineW * 0.5f - 3.0f;
        g.setColour (accent);
        g.fillEllipse (centre.x - discRadius, centre.y - discRadius, discRadius * 2.0f, discRadius * 2.0f);

        const auto tip = juce::Point<float> (centre.x + discRadius * 0.72f * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                              centre.y + discRadius * 0.72f * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        juce::Path pointer;
        pointer.startNewSubPath (centre.x, centre.y);
        pointer.lineTo (tip.x, tip.y);
        g.setColour (theme::inkOn (accent, juce::Colours::black, juce::Colours::white).withAlpha (0.9f));
        g.strokePath (pointer, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // Stage Glass / Console: thumb dot at the arc's end (Stage Glass
        // gets a halo, Console stays flat).
        const float thumbRadius = glowStyle ? 6.5f : 5.0f;
        const auto thumbPoint = juce::Point<float> (centre.x + arcRadius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                                     centre.y + arcRadius * std::sin (toAngle - juce::MathConstants<float>::halfPi));
        if (glowStyle)
        {
            g.setColour (accent.withAlpha (0.28f));
            g.fillEllipse (thumbPoint.x - thumbRadius * 1.9f, thumbPoint.y - thumbRadius * 1.9f,
                           thumbRadius * 3.8f, thumbRadius * 3.8f);
        }
        g.setColour (slider.isEnabled() ? accent : t.textDim);
        g.fillEllipse (thumbPoint.x - thumbRadius, thumbPoint.y - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f);
    }
}

void OpenGuitarMultiFxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                       float sliderPos, float, float,
                                                       juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto& t = theme::current();
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId)
                            .withAlpha (slider.isEnabled() ? 1.0f : 0.4f);
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    const bool console = t.id == theme::Id::console;
    const bool stageGlass = t.id == theme::Id::stageGlass;

    if (! horizontal)
    {
        const float trackWidth = 6.0f;
        const float consoleThumbWidth = 26.0f;
        const float thumbSize = console ? 12.0f : 22.0f;
        const float top = (float) y;
        const float bottom = (float) (y + height);
        const float thumbInset = thumbSize * 0.5f;
        const float pos = juce::jlimit (top + thumbInset, bottom - thumbInset, sliderPos);
        const auto track = juce::Rectangle<float> ((float) x + (float) width * 0.5f - trackWidth * 0.5f,
                                                   top, trackWidth, (float) height);
        const auto range = slider.getRange();
        const bool bipolar = std::abs (range.getStart() + range.getEnd()) < 0.0001;
        const float fillStart = bipolar ? track.getCentreY() : pos;
        const float fillEnd = bipolar ? pos : bottom;
        const auto fill = juce::Rectangle<float> (track.getX(), juce::jmin (fillStart, fillEnd),
                                                   trackWidth, std::abs (fillEnd - fillStart));

        g.setColour (t.background);
        g.fillRoundedRectangle (track, 3.0f);
        if (fill.getHeight() > 0.0f)
        {
            if (stageGlass)
            {
                g.setColour (accent.withAlpha (0.12f));
                g.drawRoundedRectangle (fill.expanded (2.0f), 4.0f, 5.0f);
                g.setColour (accent.withAlpha (0.22f));
                g.drawRoundedRectangle (fill.expanded (1.0f), 4.0f, 3.0f);
            }
            g.setColour (accent);
            g.fillRoundedRectangle (fill, 3.0f);
        }

        const float thumbWidth = console ? consoleThumbWidth : thumbSize;
        const float thumbHeight = thumbSize;
        const auto thumb = juce::Rectangle<float> ((float) x + (float) width * 0.5f - thumbWidth * 0.5f,
                                                   pos - thumbHeight * 0.5f, thumbWidth, thumbHeight);
        if (stageGlass)
        {
            g.setColour (accent.withAlpha (0.18f));
            g.fillEllipse (thumb.getCentreX() - 13.0f, pos - 13.0f, 26.0f, 26.0f);
        }
        g.setColour (console ? juce::Colour (0xffe6e6e8)
                             : juce::Colours::white.withAlpha (slider.isEnabled() ? 1.0f : 0.45f));
        if (console)
            g.fillRoundedRectangle (thumb, 3.0f);
        else
            g.fillEllipse (thumb);

        g.setColour (console ? juce::Colour (0xff35353a) : accent);
        g.drawLine (thumb.getCentreX() - (console ? 7.0f : 5.0f), pos,
                    thumb.getCentreX() + (console ? 7.0f : 5.0f), pos, 2.0f);
        return;
    }

    const float thumbSize = 24.0f;
    juce::Rectangle<float> track;
    juce::Rectangle<float> fill;
    juce::Rectangle<float> thumb;

    const float left = (float) x + thumbSize * 0.5f;
    const float right = juce::jmax (left, (float) (x + width) - thumbSize * 0.5f);
    const float pos = juce::jlimit (left, right, sliderPos);
    track = { left, (float) y + (float) height * 0.5f - 4.0f, right - left, 8.0f };
    fill = { left, track.getY(), pos - left, track.getHeight() };
    thumb = { pos - thumbSize * 0.5f, track.getCentreY() - thumbSize * 0.5f, thumbSize, thumbSize };

    g.setColour (t.raised);
    g.fillRoundedRectangle (track, 4.0f);
    if (fill.getWidth() > 0.0f && fill.getHeight() > 0.0f)
    {
        if (stageGlass)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.drawRoundedRectangle (fill.expanded (2.0f), 4.0f, 5.0f);
            g.setColour (accent.withAlpha (0.22f));
            g.drawRoundedRectangle (fill.expanded (1.0f), 4.0f, 3.0f);
        }
        g.setColour (accent);
        g.fillRoundedRectangle (fill, 4.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (slider.isEnabled() ? 1.0f : 0.45f));
    if (console)
        g.fillRoundedRectangle (thumb, 3.0f);
    else
        g.fillEllipse (thumb);
}

void OpenGuitarMultiFxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& backgroundColour,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float cornerSize = juce::jmin (theme::current().smallRadius, bounds.getHeight() * 0.5f);

    // Flat fill, no gradient -- pressed reads as "pushed in" (slightly
    // darker), hovered as "lit up" (slightly lighter), rather than V4's
    // default translucent-overlay treatment which barely read as a state
    // change at this button size.
    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker (0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerSize);

    // Border is always tinted by this button's accent (per-effect if
    // ParameterPanel set one, else the app default -- see accentColourId),
    // at rest as well as hover/press, so the drawer's buttons read as
    // belonging to the selected effect the same way its coloured outline
    // does in the chain (EffectBlockComponent::paint()) -- brightening
    // further on hover/press so the state change still reads clearly.
    const auto accent = button.findColour (accentColourId);
    auto border = accent.withAlpha (0.55f);
    if (shouldDrawButtonAsDown)
        border = accent;
    else if (shouldDrawButtonAsHighlighted)
        border = accent.withAlpha (0.8f);

    g.setColour (border);
    g.drawRoundedRectangle (bounds, cornerSize, 1.2f);
}

} // namespace openguitarmultifx
