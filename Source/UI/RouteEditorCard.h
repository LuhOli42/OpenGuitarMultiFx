#pragma once

#include "OverlayHost.h"
#include "../Engine/OutputRoute.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace openguitarmultifx
{

/**
    Per-destination mix of one row (docs/SignalRoutingAndOutputSplitPlan.md,
    phases 3-4): one line per place the row sends to -- each physical output and
    each row link -- with its level, polarity, crossover and, where it means
    something, balance (stereo outputs) or channel map (row links). Hosted on OverlayHost
    like every other card. Knob moves are applied when the drag ends (each
    apply swaps the graph; the swap fade covers the change), toggles at once.
*/
class RouteEditorCard : public juce::Component,
                        public OutsideTapAware
{
public:
    struct Entry
    {
        enum class Kind { stereoOutput, monoOutput, rowLink };

        juce::String name;
        Kind kind = Kind::stereoOutput;
        float gainDb = 0.0f;
        float pan = 0.0f;   // -1 .. 1, stereo outputs
        bool invert = false;
        LaneLink::ChannelMap map = LaneLink::ChannelMap::stereo;   // row links
        RouteFilter filter;   // crossover, every kind
    };

    static constexpr float minGainDb = -40.0f;   // the bottom of the fader is silence
    static constexpr float maxGainDb = 12.0f;

    RouteEditorCard (const juce::String& title, std::vector<Entry> entries,
                     std::function<void (int index, const Entry&)> onChange,
                     std::function<void()> onDone);
    ~RouteEditorCard() override;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void outsideTapDismissed() override {}

    /** dB as stored/shown -> the linear gain the engine runs (the fader's bottom is 0). */
    static float gainFromDb (float db) noexcept { return db <= minGainDb ? 0.0f : juce::Decibels::decibelsToGain (db); }
    static float dbFromGain (float gain) noexcept { return gain <= 0.0f ? minGainDb : juce::jlimit (minGainDb, maxGainDb, juce::Decibels::gainToDecibels (gain)); }

private:
    class Line;

    juce::Label titleLabel, levelHeader, extraHeader, invertHeader;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Line> lines;
    juce::TextButton doneButton { "Done" };
};

} // namespace openguitarmultifx
