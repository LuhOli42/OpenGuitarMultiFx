#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    The tile at either end of one chain row: what feeds that row (left) or
    where that row goes (right) -- e.g. Quad Cortex's "In 1" / "Out 1/2"
    tiles, except the right-hand one can also say "LINE 2", because a row's
    output can be another row rather than the device.

    Deliberately dumb: it shows two lines of text (or a "+" when nothing is
    chosen yet) and reports clicks. MainComponent owns the menu, because
    what's offered depends on the whole routing picture -- which rows would
    make a loop, which device channels exist -- not on anything this tile
    could know by itself. Was IOSelectorBlock, when a row's only possible
    endpoints were physical device channels.
*/
class RowEndpointBlock : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Both empty = nothing routed here yet, drawn as a "+" placeholder. */
    void setDisplay (juce::String labelText, juce::String detailText);

    std::function<void()> onClicked;

    /** Second line under the "--" of an unrouted tile: "Livre" on a row's input, nothing on its output (prototype). */
    juce::String unroutedDetail { "Livre" };

private:
    juce::String label, detail;
};

} // namespace openguitarmultifx
