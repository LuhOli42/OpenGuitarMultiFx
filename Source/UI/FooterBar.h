#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace openguitarmultifx
{

/**
    Bottom bar: tuner, BPM/tap-tempo, and IN/OUT level meters -- always on
    screen alongside the chain, per user request 2026-09-10 ("o footer ali
    embaixo q vai ter o afinador, e o bpm... e um meter de entrada de audio
    e saida"). Tuner is a horizontal gauge with the note letter next to it;
    IN/OUT meters are horizontal bars stacked IN-over-OUT, each labelled
    directly on the bar -- both per follow-up user correction the same day
    (originally vertical bars/no gauge).

    The tuner and IN/OUT meters are fed real audio (Phase 5): setLevels()
    from `AudioEngine::getInputLevel()`/`getOutputLevel()` (peak per
    block), setTuning() from a YIN pitch estimate
    (`Source/Engine/PitchDetector.h`) converted to a note name + cents
    deviation by `MainComponent`'s timer -- see its `timerCallback()`.
    This class itself still knows nothing about audio; it only draws
    whatever numbers it's given, same as before.

    Tap-tempo is real too, and self-contained here -- unlike the tuner/
    meters, it needs no audio thread involvement at all, just wall-clock
    time between button presses, so there's nothing for MainComponent to
    bridge. Averages the last few tap intervals (smooths out human timing
    jitter) and resets the running average whenever a gap is too long or
    too short to plausibly be the next tap in the same sequence, rather
    than silently blending an unrelated new tempo into the old one.
    getBpm() exists for future tempo-synced effects to read -- nothing
    consumes it yet.
*/
class FooterBar : public juce::Component
{
public:
    FooterBar();

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** 0-1 peak level for the IN/OUT meter bars. */
    void setLevels (float inLevel, float outLevel);

    /** Note name text ("--" for no detected pitch) + gauge needle position
        (-1 flat .. 0 in tune .. +1 sharp, already clamped by the caller). */
    void setTuning (const juce::String& note, float deviation);

    /** Current tap-tempo estimate. For future tempo-synced effects -- nothing reads this yet. */
    double getBpm() const noexcept { return bpm; }

private:
    void tapTempo();
    juce::Label tunerNoteLabel { {}, "--" };
    juce::Label bpmValueLabel { {}, "120" };
    juce::Label bpmUnitLabel { {}, "BPM" };
    juce::TextButton tapButton { "TAP" };

    float inLevel = 0.0f, outLevel = 0.0f;
    float tuningDeviation = 0.0f;
    bool hasDetectedNote = false;

    double bpm = 120.0;
    double lastTapMs = 0.0;
    std::vector<double> recentTapIntervalsMs;

    juce::Rectangle<float> tunerGaugeBounds, inMeterBounds, outMeterBounds;

    void drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level, const juce::String& label) const;
    void drawTunerGauge (juce::Graphics& g, juce::Rectangle<float> bounds) const;
};

} // namespace openguitarmultifx
