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
*/
inline bool wasTap (const juce::MouseEvent& event)
{
    return event.getDistanceFromDragStart() <= 16.0f;
}

} // namespace openguitarmultifx::touch
