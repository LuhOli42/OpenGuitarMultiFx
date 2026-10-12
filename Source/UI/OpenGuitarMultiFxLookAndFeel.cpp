#include "OpenGuitarMultiFxLookAndFeel.h"

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

    // Colour setup -- per user feedback 2026-09-10 ("muito amador", buttons
    // and contrast called out specifically). LookAndFeel_V4's default dark
    // scheme uses a slate-blue widgetBackground (0xff323e44) for buttons/
    // comboboxes/text fields; every custom-painted surface in this app
    // (MainComponent/ParameterPanel's paint(), EffectBlockComponent) is a
    // near-black from a completely different grey family (0xff141414/
    // 0xff1e1e1e/0xff2e2e2e). Sitting the stock widgets on top of that
    // produced a visible seam -- two different "greys" -- which read as
    // unpolished more than any single colour being wrong. Everything below
    // is pulled from that same near-black family instead, with one shared
    // accent (getAppAccentColour(), already what the rotary knobs render
    // in by default) for anything interactive that needs to stand out.
    const auto widget  = juce::Colour (0xff262626); // buttons/comboboxes/fields -- one step up from panel bg, reads as "raised"
    const auto outline = juce::Colour (0xff3c3c3c); // resting border for the above

    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff141414));

    setColour (juce::TextButton::buttonColourId, widget);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, widget);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, juce::Colours::white);

    setColour (juce::Slider::rotarySliderOutlineColourId, outline);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1a1a1a));
    setColour (juce::Slider::textBoxOutlineColourId, outline);

    setColour (juce::ToggleButton::tickDisabledColourId, juce::Colours::grey);
    setColour (juce::ToggleButton::textColourId, juce::Colours::white);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1e1e1e));
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);

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
    constexpr float cornerSize = 6.0f;

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
