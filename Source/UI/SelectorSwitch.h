#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

#include <functional>

namespace openguitarmultifx
{

/**
    A stack of discrete option buttons -- for a parameter that is really a handful of labelled choices
    (4/8/16 ohm, Normal / Jumped / Bright), matching the prototype's .selst: separate 110x48 buttons with
    6px gaps, the active one filled with the drawer accent. `ParameterPanel` keeps its `juce::Slider` as a
    hidden value/param model in parallel and only shows this widget instead; onChange() drives that hidden
    slider, which is what actually writes the AudioParameterFloat.
*/
class SelectorSwitch : public juce::Component
{
public:
    static constexpr int optionWidth = 110;
    static constexpr int optionHeight = 48; // touch::minTapTarget
    static constexpr int optionGap = 6;

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
    int getNumOptions() const noexcept { return options.size(); }
    int getPreferredHeight() const noexcept
    {
        const int n = juce::jmax (0, options.size());
        return n == 0 ? 0 : n * optionHeight + (n - 1) * optionGap;
    }

    /** Fired when the user taps a different position. */
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const int n = juce::jmax (1, options.size());
        const float pitch = (float) (optionHeight + optionGap);
        const auto accentLifted = accent.getPerceivedBrightness() < 0.35f ? accent.brighter (0.9f) : accent;

        for (int i = 0; i < n; ++i)
        {
            const auto button = getLocalBounds().toFloat()
                                    .withY ((float) i * pitch)
                                    .withHeight ((float) optionHeight);
            const bool isSelected = i == selected;

            g.setColour (isSelected ? accentLifted : t.raised);
            g.fillRoundedRectangle (button, t.smallRadius);
            g.setColour (isSelected ? accentLifted : t.line);
            g.drawRoundedRectangle (button, t.smallRadius, 1.0f);

            g.setColour (isSelected ? theme::inkOn (accentLifted, t.background, t.text)
                                    : t.textMuted);
            g.setFont (juce::Font (13.0f, isSelected ? juce::Font::bold : juce::Font::plain));
            auto optionText = options[i];
            if (t.uppercaseLabels)
                optionText = optionText.toUpperCase();
            g.drawText (optionText, button.reduced (8.0f, 0.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override { select (event.position); }
    void mouseDrag (const juce::MouseEvent& event) override { select (event.position); }

private:
    void select (juce::Point<float> position)
    {
        const int n = juce::jmax (1, options.size());
        const float pitch = (float) (optionHeight + optionGap);
        const int index = juce::jlimit (0, n - 1, (int) (position.y / pitch));
        setSelectedIndex (index);
    }

    juce::StringArray options;
    int selected = 0;
    juce::Colour accent = juce::Colours::grey;
};

} // namespace openguitarmultifx
