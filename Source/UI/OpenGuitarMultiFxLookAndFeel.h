#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace openguitarmultifx
{

/**
    Sora (SIL OFL, embedded -- see Assets/Fonts/SORA-LICENSE.txt)
    everywhere instead of whatever generic sans the OS happens to have --
    the final target is a from-scratch embedded Linux build with no
    guaranteed font at all, and the default JUCE font reads as generic
    "desktop app", not a real product. Was Inter until 2026-09-10; swapped
    per user request for something that reads less like a stock UI font and
    more like a purpose-built gear product.

    Also bumps the popup menu font, which is what makes
    juce::PopupMenu AND juce::ComboBox's dropdown touch-sized:
    ComboBox has no per-instance "row height" hook the way
    PopupMenu::Options::withStandardItemHeight() does (used directly at
    each PopupMenu call site -- see Source/UI/TouchSizing.h), so this is
    the one place that also covers architectureBox
    (Tone3000SearchDialog's A1/A2/Custom picker) and any future
    juce::ComboBox.
*/
class OpenGuitarMultiFxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OpenGuitarMultiFxLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;
    juce::Font getPopupMenuFont() override;

    /** JUCE never auto-sizes a Slider's value textbox font to the box's own
        pixel size -- it just inherits juce::Label's plain default font
        (see LookAndFeel_V2::createSliderTextBox/getLabelFont), which is why
        every knob's value readout rendered small regardless of how big the
        box was drawn. Bumped explicitly here per user request 2026-09-10
        (needs to read clearly from a couple of metres away on a 10" stage
        display) -- the only juce::Slider usage in this app is
        ParameterPanel's knobs, so this is safe as a blanket override. */
    juce::Label* createSliderTextBox (juce::Slider&) override;

    /** For the handful of places (the big preset number/name -- see
        MainComponent) that specifically want the heavier display weight,
        not just "bold" (juce::Font's styleFlags only distinguish
        plain/bold/italic, not weight beyond that -- getTypefaceForFont()
        above already maps `bold` to Sora Bold; this is the one step
        further, applied by hand where it matters). */
    juce::Typeface::Ptr getExtraBoldTypeface() const noexcept { return extraBold; }

    /** Flat rounded-rect fill + a border that brightens to the app accent
        on hover/press, replacing LookAndFeel_V4's default gradient-y button
        (which also used its stock slate-blue widgetBackground -- visibly a
        different grey family from this app's near-black panels, see the
        colour setup in the .cpp -- and read as "generic desktop app" per
        user feedback 2026-09-10). */
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** The app's CURRENT accent colour -- the knob fill/pointer colour
        (Slider::rotarySliderFillColourId, set in the .cpp) and the drawer's
        scrollbar thumb, and drawButtonBackground()'s fallback border when a
        button hasn't been given its own accentColourId (see below). It
        follows the active snapshot's colour (user request 2026-10-12: "a cor
        principal tbm pode estar atrelada ao snapshot q estarmos") --
        MainComponent::applySnapshotAccent() calls setAppAccentColour()
        whenever the snapshot changes or its colour is edited. Default until
        then is the original blue 0xff42a2c8. */
    static juce::Colour getAppAccentColour() noexcept { return appAccent; }

    /** Repaints the app accent: rewrites the static store above AND every
        accent-derived colour ID on this LookAndFeel (rotary fill, thumb,
        tick, scrollbar, popup highlight, the default accentColourId), so
        components that repaint after this resolve the new colour through
        their normal findColour() path. Caller repaints. */
    void setAppAccentColour (juce::Colour colour);

    /** Per-button accent override -- ParameterPanel sets this to the
        selected processor's own getAccentColour() on its drawer buttons
        (Bypassed/Browse/Search/Remove) so they read as "this effect's
        controls", matching the coloured outline the block already has in
        the chain (EffectBlockComponent::paint()) per user request
        2026-09-10. Falls back to getAppAccentColour() via the default set
        on `this` in the constructor -- Component::findColour() walks up to
        the LookAndFeel's own colour if no instance/parent override exists,
        so buttons that never get a per-effect colour set (or with nothing
        selected) still render sensibly. */
    enum ColourIds
    {
        accentColourId = 0x2f000001
    };

private:
    /** Re-applies every accent-derived colour ID (see the constructor and
        setAppAccentColour()). */
    void applyAccentColours();

    juce::Typeface::Ptr regular, bold, extraBold;
    static juce::Colour appAccent;
};

} // namespace openguitarmultifx
