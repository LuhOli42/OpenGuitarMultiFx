#pragma once

#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Full-window modal host for anything that used to be a separate OS
    window (juce::DialogWindow / juce::DocumentWindow) -- TONE3000 login,
    TONE3000 search, the installed-models list. This product is a
    standalone touchscreen pedalboard, not a desktop app: there's no window
    manager to place a second window sensibly, and "open another window"
    isn't a mental model a pedalboard should ever ask of its player. So
    every one of those "popups" is instead a card drawn on top of the
    single app window, mobile-app-sheet style -- see AGENT.md's UI/UX
    Design Philosophy section.

    Supports stacking (pushOverlay/popOverlay) because TONE3000 login opens
    a further card (the embedded browser) on top of the account panel.
    Tapping outside the top card dismisses just that card, same as a phone
    bottom sheet.
*/

/** Optional interface for a card pushed onto OverlayHost. When the top
    card implements this, an outside tap is offered to it first: a
    destructive confirm (or any card with a cancel path) can run that path
    instead of vanishing silently -- "tap outside to cancel" then means
    the same thing as tapping the card's own Cancel button, including any
    side effects that path has. Cards that don't implement it (or don't
    consume the tap) get the host's plain pop, unchanged. */
class PersistentCard
{
public:
    virtual ~PersistentCard() = default;

    /** A card that is built once and shown again (pushPersistent) is not destroyed when it is taken off the screen, so
        whatever its destructor did on closing (stop timers, drop a pending learn) goes here instead. */
    virtual void cardHidden() = 0;
};

class OutsideTapAware
{
public:
    virtual ~OutsideTapAware() = default;

    /** Called on an outside tap. The card may close itself (its cancel
        handler typically pops it) or push a further card; the host pops
        it only if it's still on top afterwards. */
    virtual void outsideTapDismissed() = 0;
};

/** True while a popup card covers the app. What sits underneath is dimmed and unreachable, so its live meters stop asking
    for GPU frames (every frame re-draws the whole tree, popup included). */
inline std::atomic<bool>& overlayCoversApp() noexcept
{
    static std::atomic<bool> value { false };
    return value;
}

class OverlayHost : public juce::Component,
                    private juce::Timer
{
public:
    OverlayHost();

    /** Adds a new top layer. The content's own size (whatever it called
        setSize() with before this call) is kept as its preferred size,
        then centred and clamped to fit inside this host. */
    void pushOverlay (std::unique_ptr<juce::Component> content);

    /** Same, for a card its owner keeps alive: popOverlay() only takes it off the screen. A card that is the same every
        time (the Add Effect browser) is built ONCE and reused, instead of building dozens of components, and painting them
        for the first time, on every open. The owner resets its state before pushing and must outlive the host's use of it. */
    void pushPersistent (juce::Component& content);

    /** Removes the top layer, revealing whatever was under it (or hides
        the whole host if that was the last one). Safe to call with an
        empty stack (a no-op). */
    void popOverlay();

    bool isEmpty() const { return layers.empty(); }

    /** The card currently on top of the stack, or nullptr when empty. */
    juce::Component* topLayer() const { return layers.empty() ? nullptr : layers.back().get(); }

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    void layOutTop();
    void timerCallback() override;
    void applyOpenAnimation();

    // Opening: the card fades in and rises ~20 px over 150 ms while the scrim darkens. Closing: the card goes at once
    // (its owner is destroyed synchronously and expects that), and only the scrim fades out over ~110 ms. Everything
    // here is a transform / alpha: no relayout per frame, so it costs the GPU next to nothing.
    float openProgress = 1.0f;  // 0 -> 1 for the top card
    float scrimAlpha = 0.0f;    // current darkness of the scrim, 0 .. scrimMax
    static constexpr float scrimMax = 0.78f;
    double lastFrameMs = 0.0;

    using Layer = std::unique_ptr<juce::Component, void (*) (juce::Component*)>;
    static void destroyCard (juce::Component* c) { delete c; }
    static void keepCard (juce::Component*) {}
    void pushLayer (Layer layer);

    std::vector<Layer> layers;
};

} // namespace openguitarmultifx
