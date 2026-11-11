#include "OpenGuitarMultiFxLookAndFeel.h"

#include "Theme.h"

#include <BinaryData.h>

namespace openguitarmultifx
{
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
    setColour (juce::Slider::textBoxBackgroundColourId, t.surface);
    setColour (juce::Slider::textBoxOutlineColourId, t.lineStrong);
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
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    return label;
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
