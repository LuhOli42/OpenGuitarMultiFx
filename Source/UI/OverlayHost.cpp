#include "OverlayHost.h"

namespace openguitarmultifx
{

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

    setVisible (true);
    toFront (false);
    layOutTop();
}

void OverlayHost::popOverlay()
{
    if (layers.empty())
        return;

    removeChildComponent (layers.back().get());
    layers.pop_back();

    if (layers.empty())
        setVisible (false);
    else
    {
        layers.back()->setVisible (true);
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
    if (layers.empty())
        return;

    g.fillAll (juce::Colours::black.withAlpha (0.65f));

    // Card background behind the top layer, so its content doesn't have to
    // paint its own edges/shadow to read as a sheet floating over the app.
    auto* top = layers.back().get();
    g.setColour (juce::Colour (0xff1a1a1a));
    g.fillRoundedRectangle (top->getBounds().toFloat().expanded (1.0f), 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (top->getBounds().toFloat().expanded (1.0f), 14.0f, 1.0f);
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
