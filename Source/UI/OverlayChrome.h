#pragma once

#include "Theme.h"
#include "TouchSizing.h"
#include "OpenGuitarMultiFxLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace openguitarmultifx::overlaychrome
{

inline float cardRadius() noexcept { return theme::current().radius + 4.0f; }

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Colour inkOn (juce::Colour colour)
{
    return colour.contrasting (juce::Colours::black, juce::Colours::white);
}

inline void paintCardBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& t = theme::current();
    const float radius = cardRadius();

    juce::DropShadow shadow (juce::Colours::black.withAlpha (0.45f), 28, { 0, 10 });
    shadow.drawForRectangle (g, bounds.toNearestInt());

    if (t.id == theme::Id::stageGlass)
    {
        juce::ColourGradient gradient (t.raised.brighter (0.04f), bounds.getCentreX(), bounds.getY(),
                                        t.background, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bounds, radius);
    }
    else
    {
        g.setColour (t.surface);
        g.fillRoundedRectangle (bounds, radius);
    }

    g.setColour (t.lineStrong.withAlpha (t.id == theme::Id::stageGlass ? 0.85f : 1.0f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

inline void drawHeaderDivider (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour (theme::current().line);
    g.drawHorizontalLine (bounds.getBottom() - 0.5f, bounds.getX(), bounds.getRight());
}

enum class ButtonKind { normal, primary, danger, ghost };
enum class SelectedStyle { none, tab, chip };

class ActionButton : public juce::Component
{
public:
    ActionButton() = default;

    ActionButton (juce::String buttonText, ButtonKind buttonKind = ButtonKind::normal,
                  std::function<void()> click = {})
        : text (std::move (buttonText)), kind (buttonKind), onClick (std::move (click))
    {
    }

    juce::String text;
    ButtonKind kind = ButtonKind::normal;
    SelectedStyle selectedStyle = SelectedStyle::none;
    bool selected = false;
    bool showDot = false;
    bool leftAccent = false;
    bool mutedIdle = false;
    juce::Colour dotColour;
    juce::String trailingText;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = kind == ButtonKind::normal && selectedStyle == SelectedStyle::chip && t.id != theme::Id::console
                                 ? juce::jmin (20.0f, bounds.getHeight() * 0.5f)
                                 : t.smallRadius;

        const bool primary = kind == ButtonKind::primary;
        const bool danger = kind == ButtonKind::danger;
        const bool ghost = kind == ButtonKind::ghost;

        juce::Colour fill = t.raised;
        juce::Colour border = t.lineStrong;
        juce::Colour textColour = t.text;

        if (primary)
        {
            fill = OpenGuitarMultiFxLookAndFeel::getAppAccentColour();
            border = fill;
            textColour = juce::Colours::black;
        }
        else if (danger)
        {
            textColour = juce::Colour (0xffff6b6b);
        }
        else if (ghost)
        {
            fill = juce::Colours::transparentBlack;
        }

        if (! primary && ! selected && mutedIdle)
            textColour = t.textMuted;
        if (selected && selectedStyle == SelectedStyle::tab && ! primary)
        {
            fill = t.raisedHigh;
            border = t.lineStrong;
            textColour = t.text;
        }
        if (selected && selectedStyle == SelectedStyle::chip && ! primary)
        {
            fill = t.text;
            border = t.text;
            textColour = t.background;
        }
        if (isMouseOverOrDragging())
        {
            if (! primary && kind != ButtonKind::danger && kind != ButtonKind::ghost)
                fill = fill.brighter (0.08f);
        }

        g.setColour (fill);
        if (ghost) g.fillRoundedRectangle (bounds, radius);
        else       g.fillRoundedRectangle (bounds, radius);

        if (selected && selectedStyle == SelectedStyle::tab && ! primary)
        {
            if (t.id == theme::Id::boldBlocks)
            {
                g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
                g.fillRoundedRectangle (bounds, radius);
                textColour = juce::Colours::black;
            }
            else if (leftAccent)
            {
                g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
                g.fillRoundedRectangle (bounds.withWidth (3.0f).withX (bounds.getX() + 1.0f), 1.5f);
            }
            else
            {
                g.setColour (OpenGuitarMultiFxLookAndFeel::getAppAccentColour());
                g.fillRoundedRectangle (bounds.withY (bounds.getBottom() - 3.0f).withHeight (3.0f), 1.5f);
            }
        }

        g.setColour (border.withAlpha (ghost ? 0.8f : 1.0f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

        auto textArea = bounds.reduced (10.0f, 0.0f);
        if (showDot)
        {
            const auto dot = juce::Rectangle<float> (textArea.getX(), textArea.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (dotColour);
            g.fillEllipse (dot);
            textArea.removeFromLeft (16.0f);
        }
        if (trailingText.isNotEmpty())
            textArea.removeFromRight (juce::jmax (34.0f, juce::GlyphArrangement::getStringWidth (font (11.0f), trailingText) + 14.0f));

        g.setColour (textColour);
        g.setFont (font (14.0f, primary || selected));
        g.drawFittedText (text, textArea.toNearestInt(), juce::Justification::centredLeft, 1);

        if (trailingText.isNotEmpty())
        {
            auto tail = bounds.withSizeKeepingCentre (juce::jmin (48.0f, bounds.getWidth() * 0.4f), bounds.getHeight());
            tail = bounds.withX (bounds.getRight() - tail.getWidth());
            g.setColour (textColour.withAlpha (0.72f));
            g.setFont (font (11.0f));
            g.drawFittedText (trailingText, tail.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! event.mouseWasDraggedSinceMouseDown() && onClick)
            onClick();
    }

    void triggerClick() { if (onClick) onClick(); }
};

class CloseButton : public juce::Component
{
public:
    explicit CloseButton (std::function<void()> click = {}) : onClick (std::move (click)) {}

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const float radius = juce::jmin (t.smallRadius, bounds.getHeight() * 0.5f);
        g.setColour (t.raised);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (t.lineStrong);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

        const auto c = bounds.getCentre();
        const float arm = bounds.getWidth() * 0.22f;
        g.setColour (t.text);
        g.drawLine (c.x - arm, c.y - arm, c.x + arm, c.y + arm, 2.2f);
        g.drawLine (c.x + arm, c.y - arm, c.x - arm, c.y + arm, 2.2f);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! event.mouseWasDraggedSinceMouseDown() && onClick)
            onClick();
    }
};

class PlusButton : public juce::Component
{
public:
    explicit PlusButton (std::function<void()> click = {}) : onClick (std::move (click)) {}

    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (t.smallRadius, bounds.getHeight() * 0.5f);
        g.setColour (t.raisedHigh);
        g.fillRoundedRectangle (bounds, radius);
        const auto c = bounds.getCentre();
        const float arm = bounds.getWidth() * 0.24f;
        g.setColour (t.text);
        g.drawLine (c.x - arm, c.y, c.x + arm, c.y, 2.5f);
        g.drawLine (c.x, c.y - arm, c.x, c.y + arm, 2.5f);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (! event.mouseWasDraggedSinceMouseDown() && onClick)
            onClick();
    }
};

inline void drawSearchFieldChrome (juce::TextEditor& editor)
{
    editor.setColour (juce::TextEditor::backgroundColourId, theme::current().raised);
    editor.setColour (juce::TextEditor::textColourId, theme::current().text);
    editor.setColour (juce::TextEditor::outlineColourId, theme::current().lineStrong);
    editor.setColour (juce::TextEditor::highlightedTextColourId, theme::current().text);
    editor.setColour (juce::TextEditor::highlightColourId, OpenGuitarMultiFxLookAndFeel::getAppAccentColour().withAlpha (0.3f));
    editor.setFont (font (14.0f));
}

} // namespace openguitarmultifx::overlaychrome
