#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "TouchSizing.h"

#include <functional>

namespace openguitarmultifx
{

/** A compact vertical detent switch with its position names alongside the track. */
class SelectorSwitch : public juce::Component
{
public:
    static constexpr int optionWidth = 120;

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
        return n == 0 ? 0 : n * pitchFor (n);
    }

    /** Fired when the user taps a different position. */
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const int n = juce::jmax (1, options.size());
        const float pitch = (float) pitchFor (n);
        const float trackWidth = 26.0f;
        const float thumbSize = 20.0f;
        const float trackX = 8.0f;
        const float centreX = trackX + trackWidth * 0.5f;
        const float firstY = pitch * 0.5f;
        const float selectedY = firstY + (float) selected * pitch;
        const bool console = t.id == theme::Id::console;
        const bool stageGlass = t.id == theme::Id::stageGlass;
        const float trackRadius = console ? 4.0f : trackWidth * 0.5f;
        const float selectedRadius = console ? 3.0f : thumbSize * 0.5f;
        const auto accentLifted = accent.getPerceivedBrightness() < 0.35f ? accent.brighter (0.9f) : accent;

        const auto track = juce::Rectangle<float> (trackX, firstY - trackWidth * 0.5f,
                                                   trackWidth,
                                                   (float) (n - 1) * pitch + trackWidth);
        g.setColour (t.raised);
        g.fillRoundedRectangle (track, trackRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (track, trackRadius, 1.0f);

        if (selected > 0)
        {
            const auto segment = juce::Rectangle<float> (trackX + 1.0f, firstY,
                                                          trackWidth - 2.0f,
                                                          selectedY - firstY);
            if (stageGlass)
            {
                g.setColour (accentLifted.withAlpha (0.12f));
                g.drawRoundedRectangle (segment.expanded (3.0f), trackRadius, 5.0f);
                g.setColour (accentLifted.withAlpha (0.22f));
                g.drawRoundedRectangle (segment.expanded (1.5f), trackRadius, 3.0f);
            }
            g.setColour (accentLifted);
            g.fillRect (segment);
        }

        for (int i = 0; i < n; ++i)
        {
            const bool isSelected = i == selected;
            const float detentY = firstY + (float) i * pitch;
            const auto thumb = juce::Rectangle<float> (centreX - thumbSize * 0.5f,
                                                        detentY - thumbSize * 0.5f,
                                                        thumbSize, thumbSize);
            if (isSelected && stageGlass)
            {
                g.setColour (accentLifted.withAlpha (0.12f));
                g.fillEllipse (thumb.expanded (5.0f));
                g.setColour (accentLifted.withAlpha (0.22f));
                g.fillEllipse (thumb.expanded (2.5f));
            }
            g.setColour (isSelected ? juce::Colours::white : t.textMuted);
            if (console)
                g.fillRoundedRectangle (thumb, selectedRadius);
            else
                g.fillEllipse (thumb);
            if (isSelected)
            {
                g.setColour (t.lineStrong);
                if (console)
                    g.drawRoundedRectangle (thumb, selectedRadius, 1.0f);
                else
                    g.drawEllipse (thumb, 1.0f);
            }

            auto optionText = options[i];
            if (t.uppercaseLabels)
                optionText = optionText.toUpperCase();
            const auto font = juce::Font (12.0f, isSelected ? juce::Font::bold : juce::Font::plain);
            const auto fullText = optionText;
            const auto maxTextWidth = (float) juce::jmax (0, getWidth() - 48);
            while (optionText.isNotEmpty()
                   && font.getStringWidthFloat (optionText + "...") > maxTextWidth)
                optionText = optionText.substring (0, optionText.length() - 1);
            if (optionText != fullText)
                optionText += "...";
            g.setColour (isSelected ? t.text : t.textMuted);
            g.setFont (font);
            g.drawText (optionText, juce::Rectangle<float> (42.0f, detentY - pitch * 0.5f,
                                                             (float) juce::jmax (0, getWidth() - 48),
                                                             pitch),
                        juce::Justification::centredLeft, false);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragOrigin = event.position;
        dragged = false;
        if (options.size() != 2)
            select (event.position);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! dragged && event.position.getDistanceFrom (dragOrigin) >= 3.0f)
            dragged = true;
        if (dragged)
            select (event.position);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (options.size() == 2 && ! dragged)
            setSelectedIndex (1 - selected);
        else if (dragged || options.size() != 2)
            select (event.position);
    }

private:
    static int pitchFor (int n) noexcept
    {
        return n == 2 ? 28 : touch::minTapTarget;
    }

    void select (juce::Point<float> position)
    {
        const int n = juce::jmax (1, options.size());
        const float pitch = (float) pitchFor (n);
        const float firstCentre = pitch * 0.5f;
        const int index = juce::jlimit (0, n - 1,
            juce::roundToInt ((position.y - firstCentre) / pitch));
        setSelectedIndex (index);
    }

    juce::StringArray options;
    int selected = 0;
    juce::Colour accent = juce::Colours::grey;
    juce::Point<float> dragOrigin;
    bool dragged = false;
};

} // namespace openguitarmultifx
