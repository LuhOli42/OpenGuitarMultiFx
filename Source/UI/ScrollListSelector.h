#pragma once

#include "SelectorSwitch.h"

#include <vector>

namespace openguitarmultifx
{

/**
    A box listing the options one per row (48 px each, the touch floor) that scrolls DOWN: a vertical drag or the wheel
    scrolls, a tap without a drag selects. Three and a half rows show, so the row cut by the bottom edge says there is
    more; fades and a thin thumb on the right say the same. Index -1 = nothing selected (the parameter holds a value
    that is not one of the options shown -- e.g. an output the current interface does not have).

    setGroups() adds a row of tabs on top -- "Stereo", "Mono", "Line" -- each listing only the options whose mask has
    that bit. Opening, or selecting from outside, shows the tab the selected option is in. Without groups (the
    default) there are no tabs.

    Used for EffectProcessor::prefersScrollingSelector parameters (the Output Send / Input Source channel and line
    lists). A SelectorSwitch subclass so ParameterPanel lays it out like any other selector (its preferred size).
*/
class ScrollListSelector : public SelectorSwitch
{
public:
    static constexpr int rowHeight = touch::minTapTarget;
    static constexpr float visibleRows = 3.5f;
    static constexpr int padding = 3;
    static constexpr int tabHeight = touch::minTapTarget;
    static constexpr int tabGap = 6;
    enum Group { stereo = 0, mono = 1, line = 2 };
    static constexpr int numGroups = 3;

    ScrollListSelector() { setViewportIgnoreDragFlag (true); }   // a drag here scrolls the list, not the drawer

    /** One mask per option (bit 0 Stereo, bit 1 Mono, bit 2 Line); empty = no tabs. Call after setOptions(). */
    void setGroups (std::vector<int> masks)
    {
        groups = std::move (masks);
        if ((int) groups.size() != options.size())
            groups.clear();
        showGroupOf (selected);
        rebuildVisible();
        resized();
        repaint();
    }

    bool hasTabs() const noexcept { return ! groups.empty(); }
    int getActiveGroup() const noexcept { return activeGroup; }

    void setActiveGroup (int group)
    {
        if (! hasTabs() || group == activeGroup)
            return;
        activeGroup = juce::jlimit (0, numGroups - 1, group);
        scroll = 0.0f;
        rebuildVisible();
        revealSelected();
        repaint();
    }

    void setSelectedIndex (int index, juce::NotificationType notification = juce::sendNotificationAsync) override
    {
        index = juce::jlimit (-1, options.size() - 1, index);
        if (index == selected)
            return;
        selected = index;
        showGroupOf (selected);
        rebuildVisible();
        revealSelected();
        repaint();
        if (notification != juce::dontSendNotification && onChange != nullptr && selected >= 0)
            onChange (selected);
    }

    int getPreferredHeight() const override
    {
        return (hasTabs() ? tabHeight + tabGap : 0) + (int) std::round (rowHeight * visibleRows) + 2 * padding;
    }

    int getPreferredWidth() const override
    {
        int widest = 0;
        for (int i = 0; i < options.size(); ++i)
            widest = juce::jmax (widest, optionTextWidth (i));
        return juce::jmax (hasTabs() ? 204 : 140, widest + 44);
    }

    void resized() override
    {
        rebuildVisible();
        clampScroll();
        revealSelected();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        const bool console = t.id == theme::Id::console;
        if (hasTabs())
            paintTabs (g, t, console);

        const auto box = listBox();
        g.setColour (t.raised);
        g.fillRoundedRectangle (box, t.smallRadius);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (box.reduced (1.0f).toNearestInt());
            const float radius = juce::jmax (0.0f, t.smallRadius - 4.0f);
            for (int r = 0; r < (int) visible.size(); ++r)
            {
                const int i = visible[(size_t) r];
                const auto row = rowBounds (r);
                if (row.getBottom() < box.getY() || row.getY() > box.getBottom())
                    continue;
                const bool active = i == selected;
                if (active)
                {
                    g.setColour (console ? juce::Colour (0xffe6e6e8) : accent);
                    g.fillRoundedRectangle (row.reduced (0.0f, 2.0f), radius);
                }
                else if (r > 0 && visible[(size_t) r - 1] != selected)
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
        pressedTabs = hasTabs() && e.position.y < (float) tabHeight;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressedTabs)
            return;
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
        if (pressedTabs)
        {
            if (e.position.y < (float) tabHeight)
                setActiveGroup (juce::jlimit (0, numGroups - 1, (int) (e.position.x * (float) numGroups / (float) getWidth())));
            return;
        }
        if (dragging)
            return;
        const float y = e.position.y - listBox().getY();
        const int r = (int) std::floor ((y + scroll - (float) padding) / (float) rowHeight);
        if (r >= 0 && r < (int) visible.size())
            setSelectedIndex (visible[(size_t) r]);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        scroll -= wheel.deltaY * 240.0f;
        clampScroll();
        repaint();
    }

    float getScroll() const noexcept { return scroll; }
    int getNumVisibleRows() const noexcept { return (int) visible.size(); }

private:
    juce::Rectangle<float> listBox() const
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        if (hasTabs())
            b.removeFromTop ((float) (tabHeight + tabGap));
        return b;
    }

    void paintTabs (juce::Graphics& g, const theme::Tokens& t, bool console)
    {
        const juce::Rectangle<float> strip (0.5f, 0.5f, (float) getWidth() - 1.0f, (float) tabHeight - 1.0f);
        g.setColour (t.raised);
        g.fillRoundedRectangle (strip, t.smallRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (strip, t.smallRadius, 1.0f);
        const char* names[] = { "Stereo", "Mono", "Line" };
        const float cellWidth = strip.getWidth() / (float) numGroups;
        for (int i = 0; i < numGroups; ++i)
        {
            const auto cell = juce::Rectangle<float> (strip.getX() + cellWidth * (float) i, strip.getY(), cellWidth, strip.getHeight()).reduced (4.0f);
            const bool active = i == activeGroup;
            if (active)
            {
                g.setColour (console ? juce::Colour (0xffe6e6e8) : accent);
                g.fillRoundedRectangle (cell, juce::jmax (0.0f, t.smallRadius - 4.0f));
            }
            g.setColour (active ? (console ? juce::Colour (0xff111111) : theme::inkOn (accent)) : t.textMuted);
            g.setFont (fontFor (12.0f, active));
            juce::String text (names[i]);
            g.drawText (t.uppercaseLabels ? text.toUpperCase() : text, cell, juce::Justification::centred, false);
        }
    }

    void showGroupOf (int option)
    {
        if (! hasTabs() || option < 0 || option >= (int) groups.size())
            return;
        const int mask = groups[(size_t) option];
        if ((mask & (1 << activeGroup)) == 0)
            for (int g = 0; g < numGroups; ++g)
                if ((mask & (1 << g)) != 0)
                {
                    activeGroup = g;
                    break;
                }
    }

    void rebuildVisible()
    {
        visible.clear();
        for (int i = 0; i < options.size(); ++i)
            if (! hasTabs() || (groups[(size_t) i] & (1 << activeGroup)) != 0)
                visible.push_back (i);
    }

    juce::Rectangle<float> rowBounds (int r) const
    {
        const auto box = listBox();
        return { box.getX() + (float) padding, box.getY() + (float) padding + (float) (r * rowHeight) - scroll,
                 box.getWidth() - 2.0f * padding, (float) rowHeight };
    }

    float contentHeight() const { return (float) ((int) visible.size() * rowHeight + 2 * padding); }
    float maxScroll() const { return juce::jmax (0.0f, contentHeight() - listBox().getHeight()); }
    void clampScroll() { scroll = juce::jlimit (0.0f, maxScroll(), scroll); }

    void revealSelected()
    {
        if (getHeight() <= 0 || selected < 0)
            return;
        const auto it = std::find (visible.begin(), visible.end(), selected);
        if (it == visible.end())
            return;
        const float top = (float) ((int) (it - visible.begin()) * rowHeight);
        const float bottom = top + (float) (rowHeight + 2 * padding);
        const float height = listBox().getHeight();
        if (top < scroll)
            scroll = top;
        else if (bottom > scroll + height)
            scroll = bottom - height;
        clampScroll();
    }

    std::vector<int> groups, visible;
    int activeGroup = stereo;
    float scroll = 0.0f, pressScroll = 0.0f;
    juce::Point<float> pressOrigin;
    bool dragging = false, pressedTabs = false;
};

} // namespace openguitarmultifx
