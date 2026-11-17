#include "OverlayHost.h"
#include "Theme.h"

#include <cmath>

namespace openguitarmultifx
{

namespace
{
    bool content_isFullscreen (const juce::Component& c) { return (bool) c.getProperties().getWithDefault ("fullscreen", false); }
}

OverlayHost::OverlayHost()
{
    setVisible (false); // nothing to show until the first pushOverlay()
}

void OverlayHost::pushOverlay (std::unique_ptr<juce::Component> content)
{
    if (! layers.empty())
        layers.back()->setVisible (false); // only the top card is interactive/visible

    addAndMakeVisible (*content);
    layers.push_back (std::move (content));

    setInterceptsMouseClicks (true, true);
    overlayCoversApp().store (true, std::memory_order_relaxed);
    setVisible (true);
    toFront (false);
    layOutTop();

    openProgress = 0.0f;
    lastFrameMs = 0.0;
    // While the card fades and rises it is drawn ONCE into a texture and that texture is moved/faded each frame; without
    // this the transparency forces the GPU to redraw every row of a big card (the Add Effect list) on every frame. The
    // buffering is dropped when the animation ends, so the live card is exactly as before.
    if (! content_isFullscreen (*layers.back()))
        layers.back()->setBufferedToImage (true);
    applyOpenAnimation();
    startTimerHz (60);
}

void OverlayHost::applyOpenAnimation()
{
    if (layers.empty())
        return;
    auto* top = layers.back().get();
    const float eased = 1.0f - (1.0f - openProgress) * (1.0f - openProgress) * (1.0f - openProgress); // ease-out cubic
    const bool fullscreen = top->getProperties().getWithDefault ("fullscreen", false);
    top->setAlpha (eased);
    top->setTransform (fullscreen || eased >= 1.0f ? juce::AffineTransform() : juce::AffineTransform::translation (0.0f, (1.0f - eased) * 20.0f));
}

void OverlayHost::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = lastFrameMs > 0.0 ? juce::jlimit (0.004f, 0.1f, (float) ((now - lastFrameMs) * 0.001)) : 1.0f / 60.0f;
    lastFrameMs = now;

    const float target = layers.empty() ? 0.0f : scrimMax;
    if (openProgress < 1.0f)
    {
        openProgress = juce::jmin (1.0f, openProgress + dt / 0.15f);
        applyOpenAnimation();
        if (openProgress >= 1.0f && ! layers.empty())
            layers.back()->setBufferedToImage (false);
    }
    scrimAlpha += (1.0f - std::exp (-dt * (layers.empty() ? 26.0f : 22.0f))) * (target - scrimAlpha);
    if (std::abs (target - scrimAlpha) < 0.004f)
        scrimAlpha = target;

    const bool settled = openProgress >= 1.0f && scrimAlpha == target;
    if (layers.empty() && scrimAlpha <= 0.0f)
        setVisible (false); // the scrim has faded out: nothing left to show
    if (settled)
        stopTimer();
    repaint();
}

void OverlayHost::popOverlay()
{
    if (layers.empty())
        return;

    layers.back()->setTransform (juce::AffineTransform());
    layers.back()->setBufferedToImage (false);
    removeChildComponent (layers.back().get());
    layers.pop_back();

    if (layers.empty())
    {
        overlayCoversApp().store (false, std::memory_order_relaxed);
        setInterceptsMouseClicks (false, false); // still painting the fading scrim, but taps go to the app underneath
        lastFrameMs = 0.0;
        startTimerHz (60);
    }
    else
    {
        layers.back()->setVisible (true);
        layers.back()->setAlpha (1.0f);
        layers.back()->setBufferedToImage (false);
        openProgress = 1.0f;
        layOutTop();
    }

    repaint();
}

void OverlayHost::layOutTop()
{
    if (layers.empty())
        return;

    auto* top = layers.back().get();

    // The content already picked its own preferred size via setSize() in
    // its constructor -- clamp that to fit this host rather than trusting
    // it blindly (a search dialog built for a wide desktop window would
    // otherwise overflow a narrow touchscreen).
    // A card that sets the "fullscreen" property (the stage screen) takes the whole host.
    if (top->getProperties().getWithDefault ("fullscreen", false))
    {
        top->setBounds (getLocalBounds());
        return;
    }

    const int w = juce::jlimit (240, (int) (getWidth()  * 0.92f), top->getWidth());
    const int h = juce::jlimit (160, (int) (getHeight() * 0.90f), top->getHeight());

    top->setBounds (getLocalBounds().withSizeKeepingCentre (w, h));
}

void OverlayHost::resized()
{
    layOutTop();
}

void OverlayHost::paint (juce::Graphics& g)
{
    if (scrimAlpha > 0.0f)
        g.fillAll (juce::Colours::black.withAlpha (scrimAlpha));
    if (layers.empty())
        return;

    // Card background behind the top layer, so its content doesn't have to
    // paint its own edges/shadow to read as a sheet floating over the app.
    auto* top = layers.back().get();
    const auto offset = top->getTransform().getTranslationY();
    const auto cardBounds = top->getBounds().toFloat().translated (0.0f, offset).expanded (1.0f);
    g.setColour (theme::current().surface.withAlpha (top->getAlpha()));
    g.fillRoundedRectangle (cardBounds, 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f * top->getAlpha()));
    g.drawRoundedRectangle (cardBounds, 14.0f, 1.0f);
}

void OverlayHost::mouseUp (const juce::MouseEvent&)
{
    // Only reached when the click didn't land on the top card (children
    // intercept their own clicks first) -- tap-outside-to-dismiss, like a
    // phone bottom sheet.
    if (layers.empty())
        return;

    // A card that marked itself OutsideTapAware gets the tap first: its
    // cancel path runs (which usually pops it, and may push a further
    // card), so "tap outside" reads exactly like tapping Cancel. If that
    // same card is still on top afterwards it had no cancel path to run --
    // plain pop, as before.
    auto* top = layers.back().get();
    if (auto* aware = dynamic_cast<OutsideTapAware*> (top))
        aware->outsideTapDismissed();

    if (! layers.empty() && layers.back().get() == top)
        popOverlay();
}

} // namespace openguitarmultifx
