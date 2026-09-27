#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    A multi-position toggle switch -- for a parameter that is really a handful of discrete choices (4/8/16 ohm, Normal
    / Jumped / Bright), not a continuous quantity. Per user request 2026-09-22: a rotary knob that happens to snap to a
    few labelled steps still LOOKS like a knob you'd turn continuously; a real amp or pedal draws that control as a
    physical multi-position switch, and this draws it the same way -- a vertical stack of tap targets, the active one
    lit in the effect's accent colour, laid out in the same footprint `ParameterPanel` gives a knob (see its
    `knobDiameter`/`knobCellHeight`) so it drops into the existing knob grid with no layout changes elsewhere.

    Deliberately NOT a juce::Slider subclass: a slider's drag gesture doesn't match "tap the position you want", and a
    switch never needs a slider's continuous drag, wheel, or text-entry behaviour. `ParameterPanel` keeps its
    `juce::Slider` as a hidden value/param model in parallel (see its SliderRow) and only shows this widget instead;
    onChange() drives that hidden slider, which is what actually writes the AudioParameterFloat.
*/
class SelectorSwitch : public juce::Component
{
public:
    void setOptions (juce::StringArray newOptions)
    {
        options = std::move (newOptions);
        selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), selected);
        repaint();
    }

    void setAccentColour (juce::Colour newAccent) noexcept
    {
        accent = newAccent;
        repaint();
    }

    void setSelectedIndex (int index, juce::NotificationType notification = juce::sendNotificationAsync)
    {
        index = juce::jlimit (0, juce::jmax (0, options.size() - 1), index);
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (notification != juce::dontSendNotification && onChange != nullptr)
            onChange (selected);
    }

    int getSelectedIndex() const noexcept { return selected; }

    /** Fired when the user taps a different position (never for a programmatic setSelectedIndex with dontSendNotification). */
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const int n = juce::jmax (1, options.size());
        const float segmentHeight = bounds.getHeight() / (float) n;

        g.setColour (juce::Colour (0xff232323));
        g.fillRoundedRectangle (bounds, 8.0f);

        for (int i = 0; i < n; ++i)
        {
            const auto segment = bounds.withY (bounds.getY() + segmentHeight * (float) i).withHeight (segmentHeight);
            const bool isSelected = i == selected;

            if (isSelected)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (segment.reduced (2.0f), 6.0f);
            }

            if (i > 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawLine (segment.getX() + 6.0f, segment.getY(), segment.getRight() - 6.0f, segment.getY(), 1.0f);
            }

            g.setColour (isSelected ? juce::Colours::black.withAlpha (0.85f) : juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (segmentHeight > 30.0f ? 13.0f : 11.0f, juce::Font::bold));
            g.drawText (options[i], segment.reduced (4.0f, 0.0f), juce::Justification::centred);
        }

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& event) override { select (event.position); }
    void mouseDrag (const juce::MouseEvent& event) override { select (event.position); } // a drag across segments picks the one released over, like a real rocker

private:
    void select (juce::Point<float> position)
    {
        const int n = juce::jmax (1, options.size());
        const float segmentHeight = (float) getHeight() / (float) n;
        const int index = juce::jlimit (0, n - 1, (int) (position.y / segmentHeight));
        setSelectedIndex (index);
    }

    juce::StringArray options;
    int selected = 0;
    juce::Colour accent = juce::Colours::grey;
};

} // namespace openguitarmultifx
