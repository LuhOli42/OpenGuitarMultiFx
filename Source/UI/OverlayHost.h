#pragma once

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
class OutsideTapAware
{
public:
    virtual ~OutsideTapAware() = default;

    /** Called on an outside tap. The card may close itself (its cancel
        handler typically pops it) or push a further card; the host pops
        it only if it's still on top afterwards. */
    virtual void outsideTapDismissed() = 0;
};

class OverlayHost : public juce::Component
{
public:
    OverlayHost();

    /** Adds a new top layer. The content's own size (whatever it called
        setSize() with before this call) is kept as its preferred size,
        then centred and clamped to fit inside this host. */
    void pushOverlay (std::unique_ptr<juce::Component> content);

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

    std::vector<std::unique_ptr<juce::Component>> layers;
};

} // namespace openguitarmultifx
