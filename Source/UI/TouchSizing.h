#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace openguitarmultifx::touch
{

/**
    Minimum comfortable tap target, in logical pixels, for anything
    interactive anywhere in this app: buttons, menu rows, list rows,
    knobs, selector tiles. This product's actual target is a ~10" touch
    panel operated with a fingertip, not a mouse -- see AGENT.md's UI/UX
    Design Philosophy. Never size a tappable element below this on either
    axis, and prefer noticeably larger for primary/frequent actions. It's
    fine for a menu or list to end up physically large as a result --
    that's the point, not a problem to work around.
*/
constexpr int minTapTarget = 48;

/**
    True on the device: image/overlay's ogmfx.service exports OGMFX_KIOSK=1.
    There the app is the only X client (no window manager), fills the panel
    and is driven by a touchscreen that X delivers as a plain pointer.
*/
inline bool kioskMode()
{
    static const bool on = juce::SystemStats::getEnvironmentVariable ("OGMFX_KIOSK", {}).getIntValue() != 0;
    return on;
}

/**
    Finger-drag scrolling for a viewport, tablet style. JUCE's default
    (ScrollOnDragMode::nonHover) only reacts to real touch input sources,
    and the X11 backend reports the touchscreen as a mouse, so on the device
    every viewport has to opt in explicitly. Off on the desktop, where a
    mouse drag is expected to select/drag rather than pan.
*/
inline void enableDragScroll (juce::Viewport& viewport)
{
    if (kioskMode())
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
}

/**
    Finger-tap test for custom-painted controls. `mouseWasClicked()` /
    `mouseWasDraggedSinceMouseDown()` use JUCE's drag threshold, which a
    real touchscreen trips constantly: a fingertip "tap" reports several
    px of jitter, so every gate that checks those helpers silently swallows
    the tap on the device while a mouse on a desktop passes. Anything that
    fires "on tap" should use this instead.

    40px, not the previous 16: cheap capacitive panels jitter well past
    16px on an ordinary tap, which kept cancelling taps even after the
    first fix. 40 stays comfortably under the smallest interactive surface
    (minTapTarget 48px and the 96/108px tiles), so a release that far from
    the press can't have been meant as a neighbouring control either.
*/
inline bool wasTap (const juce::MouseEvent& event, float maxDistance = 40.0f)
{
    return event.getDistanceFromDragStart() <= maxDistance;
}

/**
    Rows/tiles inside a drag-scrollable viewport need a tighter tap
    threshold than wasTap(): kiosk viewports start panning at ~8px, so
    any distance past that means the release was the end of a scroll,
    not a tap on the item.
*/
inline bool wasListTap (const juce::MouseEvent& event)
{
    return wasTap (event, 6.0f);
}

} // namespace openguitarmultifx::touch
