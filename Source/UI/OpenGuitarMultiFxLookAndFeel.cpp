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
