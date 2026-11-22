#pragma once

#include "SelectorSwitch.h"

namespace openguitarmultifx
{

/**
    A box listing the options one per row (48 px each, the touch floor) that scrolls DOWN: a vertical drag or the wheel
    scrolls, a tap without a drag selects. Three and a half rows show, so the row cut by the bottom edge says there is
    more; fades and a thin thumb on the right say the same. Index -1 = nothing selected (the parameter holds a value
    that is not one of the options shown -- e.g. an output the current interface does not have).

    Used for EffectProcessor::prefersScrollingSelector parameters (the Output Send / Input Source channel and line
    lists). A SelectorSwitch subclass so ParameterPanel lays it out like any other selector (its preferred size).
*/
class ScrollListSelector : public SelectorSwitch
{
public:
    static constexpr int rowHeight = touch::minTapTarget;
    static constexpr float visibleRows = 3.5f;
    static constexpr int padding = 3;

    ScrollListSelector() { setViewportIgnoreDragFlag (true); }   // a drag here scrolls the list, not the drawer

    void setSelectedIndex (int index, juce::NotificationType notification = juce::sendNotificationAsync) override
    {
        index = juce::jlimit (-1, options.size() - 1, index);
        if (index == selected)
            return;
        selected = index;
        revealSelected();
        repaint();
        if (notification != juce::dontSendNotification && onChange != nullptr && selected >= 0)
            onChange (selected);
    }

    int getPreferredHeight() const override { return (int) std::round (rowHeight * visibleRows) + 2 * padding; }

    int getPreferredWidth() const override
    {
        int widest = 0;
        for (int i = 0; i < options.size(); ++i)
            widest = juce::jmax (widest, optionTextWidth (i));
        return juce::jmax (140, widest + 44);
    }

    void resized() override
    {
        clampScroll();
        revealSelected();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const bool console = t.id == theme::Id::console;
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.raised);
        g.fillRoundedRectangle (box, t.smallRadius);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (box.reduced (1.0f).toNearestInt());
            const float radius = juce::jmax (0.0f, t.smallRadius - 4.0f);
            for (int i = 0; i < options.size(); ++i)
            {
                const auto row = rowBounds (i);
                if (row.getBottom() < box.getY() || row.getY() > box.getBottom())
                    continue;
                const bool active = i == selected;
                if (active)
                {
                    g.setColour (console ? juce::Colour (0xffe6e6e8) : accent);
                    g.fillRoundedRectangle (row.reduced (0.0f, 2.0f), radius);
                }
                else if (i > 0 && i - 1 != selected)
                {
                    g.setColour (t.line.withAlpha (0.5f));
                    g.fillRect (row.withHeight (1.0f).reduced (8.0f, 0.0f));
                }
                g.setColour (active ? (console ? juce::Colour (0xff111111) : theme::inkOn (accent)) : t.text);
                g.setFont (fontFor (13.0f, active));
                g.drawText (optionText (i), row.reduced (14.0f, 0.0f), juce::Justification::centredLeft, false);
            }

            const float fade = 22.0f;
            if (scroll > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (t.raised, 0.0f, box.getY(), t.raised.withAlpha (0.0f), 0.0f, box.getY() + fade, false));
                g.fillRect (box.withHeight (fade));
            }
            if (scroll < maxScroll() - 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (t.raised.withAlpha (0.0f), 0.0f, box.getBottom() - fade, t.raised, 0.0f, box.getBottom(), false));
                g.fillRect (box.withY (box.getBottom() - fade).withHeight (fade));
            }
        }

        g.setColour (t.line);
        g.drawRoundedRectangle (box, t.smallRadius, 1.0f);

        if (maxScroll() > 0.5f)
        {
            const float trackHeight = box.getHeight() - 8.0f;
            const float thumbHeight = juce::jmax (18.0f, trackHeight * box.getHeight() / contentHeight());
            const float thumbY = box.getY() + 4.0f + (trackHeight - thumbHeight) * (scroll / maxScroll());
            g.setColour (t.textMuted.withAlpha (0.6f));
            g.fillRoundedRectangle ({ box.getRight() - 6.0f, thumbY, 3.0f, thumbHeight }, 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressOrigin = e.position;
        pressScroll = scroll;
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.position.getDistanceFrom (pressOrigin) >= 6.0f)   // same tap/drag threshold as SelectorSwitch
            dragging = true;
        if (! dragging)
            return;
        scroll = pressScroll - (e.position.y - pressOrigin.y);
        clampScroll();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging)
            return;
        const int row = (int) std::floor ((e.position.y + scroll - (float) padding) / (float) rowHeight);
        if (row >= 0 && row < options.size())
            setSelectedIndex (row);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        scroll -= wheel.deltaY * 240.0f;
        clampScroll();
        repaint();
    }

    float getScroll() const noexcept { return scroll; }

private:
    juce::Rectangle<float> rowBounds (int i) const
    {
        return { (float) padding, (float) padding + (float) (i * rowHeight) - scroll,
                 (float) getWidth() - 2.0f * padding, (float) rowHeight };
    }

    float contentHeight() const { return (float) (options.size() * rowHeight + 2 * padding); }
    float maxScroll() const { return juce::jmax (0.0f, contentHeight() - (float) getHeight()); }
    void clampScroll() { scroll = juce::jlimit (0.0f, maxScroll(), scroll); }

    void revealSelected()
    {
        if (getHeight() <= 0 || selected < 0)
            return;
        const float top = (float) (selected * rowHeight);
        const float bottom = top + (float) (rowHeight + 2 * padding);
        if (top < scroll)
            scroll = top;
        else if (bottom > scroll + (float) getHeight())
            scroll = bottom - (float) getHeight();
        clampScroll();
    }

    float scroll = 0.0f, pressScroll = 0.0f;
    juce::Point<float> pressOrigin;
    bool dragging = false;
};

} // namespace openguitarmultifx
