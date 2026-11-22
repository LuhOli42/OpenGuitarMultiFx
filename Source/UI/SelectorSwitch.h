#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "TouchSizing.h"

#include <cmath>
#include <functional>

namespace openguitarmultifx
{

/** A touch-sized horizontal selector with compact switch and segmented variants.

    More than maxUnscrolledOptions positions (only reachable through EffectProcessor::prefersScrollingSelector -- the
    Output Send / Input Source device channel lists) turns the segmented bar into a strip capped at maxScrollingWidth
    that scrolls sideways: drag or wheel scrolls, a tap (no drag) selects. */
class SelectorSwitch : public juce::Component
{
public:
    void setOptions (juce::StringArray newOptions)
    {
        options = std::move (newOptions);
        selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), selected);
        setViewportIgnoreDragFlag (isScrolling());
        scrollX = 0.0f;
        resized();
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
        revealSelected();
        repaint();
        if (notification != juce::dontSendNotification && onChange != nullptr)
            onChange (selected);
    }

    int getSelectedIndex() const noexcept { return selected; }
    int getNumOptions() const noexcept { return options.size(); }
    int getPreferredHeight() const noexcept { return touch::minTapTarget; }

    static constexpr int maxUnscrolledOptions = 8;
    static constexpr int maxScrollingWidth = 420;

    bool isScrolling() const noexcept { return options.size() > maxUnscrolledOptions; }

    int getPreferredWidth() const
    {
        const int content = contentWidth();
        return isScrolling() ? juce::jmin (content, maxScrollingWidth) : content;
    }

    void resized() override
    {
        clampScroll();
        revealSelected();
    }

    /** Fired when the user taps a different position. */
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme::current();
        if (options.size() == 2)
            paintTwoOptionSwitch (g, t);
        else if (options.size() >= 3)
            paintSegments (g, t);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragOrigin = event.position;
        dragScrollOrigin = scrollX;
        dragged = false;
        gesture = targetAt (event.position);
        if (options.size() >= 3 && ! isScrolling())
            selectSegmentAt (event.position.x);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        // 3px treated ordinary touchscreen jitter as a drag; use the list
        // threshold so a tap never flips the switch by accident.
        if (! dragged && event.position.getDistanceFrom (dragOrigin) >= 6.0f)
            dragged = true;
        if (! dragged)
            return;

        if (isScrolling())
        {
            scrollX = dragScrollOrigin - (event.position.x - dragOrigin.x);
            clampScroll();
            repaint();
        }
        else if (options.size() == 2 && gesture == Gesture::track
            && std::abs (event.position.x - dragOrigin.x) >= std::abs (event.position.y - dragOrigin.y))
            setSelectedIndex (event.position.x < trackBounds().getCentreX() ? 0 : 1);
        else if (options.size() >= 3)
            selectSegmentAt (event.position.x);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (options.size() == 2)
        {
            if (dragged)
            {
                if (gesture == Gesture::track)
                    setSelectedIndex (event.position.x < trackBounds().getCentreX() ? 0 : 1);
            }
            else if (gesture == Gesture::track)
                setSelectedIndex (1 - selected);
            else if (gesture == Gesture::leftLabel)
                setSelectedIndex (0);
            else if (gesture == Gesture::rightLabel)
                setSelectedIndex (1);
        }
        else if (isScrolling())
        {
            if (! dragged)
                selectSegmentAt (event.position.x);
        }
        else if (options.size() >= 3)
            selectSegmentAt (event.position.x);
    }

    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (! isScrolling())
        {
            Component::mouseWheelMove (event, wheel);
            return;
        }
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollX -= delta * 240.0f;
        clampScroll();
        repaint();
    }

private:
    int contentWidth() const
    {
        if (options.size() == 2)
            return optionTextWidth (0) + labelGap + switchWidth + labelGap + optionTextWidth (1);

        if (options.size() < 3)
            return 0;

        int width = segmentPadding * 2 + segmentGap * (options.size() - 1);
        for (int i = 0; i < options.size(); ++i)
            width += segmentWidth (i);
        return width;
    }

    enum class Gesture { none, leftLabel, track, rightLabel, segments };

    static constexpr int switchWidth = 52;
    static constexpr int switchHeight = 30;
    static constexpr int labelGap = 8;
    static constexpr int thumbSize = 24;
    static constexpr int segmentHeight = 32;
    static constexpr int segmentPadding = 3;
    static constexpr int segmentGap = 3;

    juce::String optionText (int index) const
    {
        auto text = options[index];
        if (theme::current().uppercaseLabels)
            text = text.toUpperCase();
        return text;
    }

    juce::Font fontFor (float height, bool bold) const
    {
        const int style = bold ? juce::Font::bold : juce::Font::plain;
        const juce::Font base (juce::FontOptions (height, style));
        return juce::Font (juce::FontOptions (height, style)
                               .withTypeface (getLookAndFeel().getTypefaceForFont (base)));
    }

    int optionTextWidth (int index) const
    {
        const auto text = optionText (index);
        return (int) std::ceil (juce::jmax (
            juce::GlyphArrangement::getStringWidth (fontFor (12.0f, false), text),
            juce::GlyphArrangement::getStringWidth (fontFor (12.0f, true), text))) + 1;
    }

    int segmentWidth (int index) const
    {
        return juce::jmax (touch::minTapTarget,
                           (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                               fontFor (11.0f, true), optionText (index))) + 16);
    }

    juce::Rectangle<float> trackBounds() const
    {
        const float x = (float) optionTextWidth (0) + labelGap;
        return { x, (getHeight() - switchHeight) * 0.5f, (float) switchWidth, (float) switchHeight };
    }

    Gesture targetAt (juce::Point<float> position) const
    {
        if (options.size() != 2)
            return options.size() >= 3 ? Gesture::segments : Gesture::none;

        const auto track = trackBounds();
        const auto trackHitArea = track.withY (0.0f).withHeight ((float) getHeight());
        if (trackHitArea.contains (position))
            return Gesture::track;
        return position.x < track.getX() ? Gesture::leftLabel
                                         : position.x > track.getRight() ? Gesture::rightLabel
                                                                         : Gesture::none;
    }

    void paintTwoOptionSwitch (juce::Graphics& g, const theme::Tokens& t)
    {
        const bool on = selected == 1;
        const bool console = t.id == theme::Id::console;
        const bool stage = t.id == theme::Id::stageGlass;
        const auto track = trackBounds();
        const float radius = console ? 4.0f : track.getHeight() * 0.5f;
        const auto fill = on ? accent : t.raised;

        if (on && stage)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.drawRoundedRectangle (track.expanded (4.0f), radius + 2.0f, 4.0f);
            g.setColour (accent.withAlpha (0.20f));
            g.drawRoundedRectangle (track.expanded (2.0f), radius + 1.0f, 2.0f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (track, radius);
        g.setColour (t.line);
        g.drawRoundedRectangle (track, radius, 1.0f);

        const float thumbX = on ? track.getRight() - 3.0f - thumbSize : track.getX() + 3.0f;
        const auto thumb = juce::Rectangle<float> (thumbX, track.getY() + 3.0f,
                                                   (float) thumbSize, (float) thumbSize);
        g.setColour (on ? juce::Colours::white : t.textMuted);
        if (console)
            g.fillRoundedRectangle (thumb, 3.0f);
        else
            g.fillEllipse (thumb);

        for (int i = 0; i < 2; ++i)
        {
            const bool active = i == selected;
            const auto font = fontFor (12.0f, active);
            const float x = i == 0 ? 0.0f : track.getRight() + labelGap;
            const float width = i == 0 ? track.getX() - labelGap : (float) getWidth() - x;
            g.setColour (active ? t.text : t.textMuted);
            g.setFont (font);
            g.drawText (optionText (i), juce::Rectangle<float> (x, 0.0f, width, (float) getHeight()),
                        i == 0 ? juce::Justification::centredRight : juce::Justification::centredLeft,
                        false);
        }
    }

    void paintSegments (juce::Graphics& g, const theme::Tokens& t)
    {
        const bool console = t.id == theme::Id::console;
        const bool stage = t.id == theme::Id::stageGlass;
        const int totalWidth = juce::jmin (getPreferredWidth(), getWidth());
        const float xOffset = (getWidth() - totalWidth) * 0.5f;
        const float containerHeight = segmentHeight + segmentPadding * 2;
        const juce::Rectangle<float> container (xOffset, (getHeight() - containerHeight) * 0.5f,
                                                 (float) totalWidth, containerHeight);
        const float containerRadius = t.smallRadius;

        g.setColour (t.raised);
        g.fillRoundedRectangle (container, containerRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (container, containerRadius, 1.0f);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (container.reduced (1.0f).toNearestInt());
        float x = container.getX() + segmentPadding - scrollX;
        for (int i = 0; i < options.size(); ++i)
        {
            const float width = (float) segmentWidth (i);
            const juce::Rectangle<float> segment (x, container.getY() + segmentPadding, width,
                                                   (float) segmentHeight);
            const bool active = i == selected;
            const float radius = juce::jmax (0.0f, t.smallRadius - 4.0f);

            if (active && stage)
            {
                g.setColour (accent.withAlpha (0.12f));
                g.drawRoundedRectangle (segment.expanded (3.0f), radius + 1.0f, 4.0f);
                g.setColour (accent.withAlpha (0.20f));
                g.drawRoundedRectangle (segment.expanded (1.5f), radius + 0.5f, 2.0f);
            }

            if (active)
            {
                g.setColour (console ? juce::Colour (0xffe6e6e8) : accent);
                g.fillRoundedRectangle (segment, radius);
                if (console)
                {
                    g.setColour (accent);
                    g.fillRect (segment.withY (segment.getBottom() - 3.0f).withHeight (3.0f));
                }
            }

            g.setColour (active ? (console ? juce::Colour (0xff111111) : theme::inkOn (accent))
                                : t.textMuted);
            g.setFont (fontFor (11.0f, active));
            g.drawText (optionText (i), segment, juce::Justification::centred, false);
            x += width + segmentGap;
        }

        // Edge fades say "there is more this way".
        const float fade = 28.0f;
        if (scrollX > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (t.raised, container.getX(), 0.0f,
                                                     t.raised.withAlpha (0.0f), container.getX() + fade, 0.0f, false));
            g.fillRect (container.withWidth (fade));
        }
        if (scrollX < maxScroll() - 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (t.raised.withAlpha (0.0f), container.getRight() - fade, 0.0f,
                                                     t.raised, container.getRight(), 0.0f, false));
            g.fillRect (container.withX (container.getRight() - fade).withWidth (fade));
        }
    }

    float maxScroll() const
    {
        return isScrolling() ? juce::jmax (0.0f, (float) (contentWidth() - juce::jmin (getPreferredWidth(), getWidth()))) : 0.0f;
    }

    void clampScroll() { scrollX = juce::jlimit (0.0f, maxScroll(), scrollX); }

    /** Scroll just enough to show the selected segment whole. */
    void revealSelected()
    {
        if (! isScrolling() || getWidth() <= 0)
            return;
        float start = (float) segmentPadding;
        for (int i = 0; i < selected; ++i)
            start += (float) (segmentWidth (i) + segmentGap);
        const float end = start + (float) segmentWidth (selected);
        const float visible = (float) juce::jmin (getPreferredWidth(), getWidth()) - 2.0f * segmentPadding;
        if (start - segmentPadding < scrollX)
            scrollX = start - segmentPadding;
        else if (end > scrollX + visible)
            scrollX = end - visible;
        clampScroll();
    }

    void selectSegmentAt (float positionX)
    {
        const float xOffset = (getWidth() - juce::jmin (getPreferredWidth(), getWidth())) * 0.5f + segmentPadding - scrollX;
        float x = xOffset;
        for (int i = 0; i < options.size(); ++i)
        {
            const float width = (float) segmentWidth (i);
            if (positionX < x + width + segmentGap * 0.5f || i == options.size() - 1)
            {
                setSelectedIndex (i);
                return;
            }
            x += width + segmentGap;
        }
    }

    juce::StringArray options;
    int selected = 0;
    juce::Colour accent = juce::Colours::grey;
    juce::Point<float> dragOrigin;
    float scrollX = 0.0f, dragScrollOrigin = 0.0f;
    bool dragged = false;
    Gesture gesture = Gesture::none;
};

} // namespace openguitarmultifx
