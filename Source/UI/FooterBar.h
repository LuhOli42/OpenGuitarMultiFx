#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
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

The IN/OUT meters are fed real audio: setLevels() from
    `AudioEngine::getInputLevel()`/`getOutputLevel()` (peak per block).
    The tuner is a BUTTON here rather than a live readout -- the pitch
    analysis only runs while the tuner page is open, so a gauge in the
    footer would be permanently dead. This class knows nothing about
    audio; it only draws whatever numbers it's given.

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

    /** Current tap-tempo estimate. For future tempo-synced effects -- nothing reads this yet. */
    double getBpm() const noexcept { return bpm; }

    /** The TUNER button opens the tuner page. It replaced a live note letter + gauge that used to sit here: the pitch
        analysis only runs while that page is open now (AudioEngine::setTunerActive(), for the CPU it costs), so a footer
        readout would be permanently dead. User request 2026-09-22, "coloca um botao pra ativar o afinador em baixo". */
    std::function<void()> onTunerTapped;

private:
    void tapTempo();
    juce::TextButton tunerButton { "TUNER" };
    juce::Label bpmValueLabel { {}, "120" };
    juce::Label bpmUnitLabel { {}, "BPM" };
    juce::TextButton tapButton { "TAP" };

    float inLevel = 0.0f, outLevel = 0.0f;

    double bpm = 120.0;
    double lastTapMs = 0.0;
    std::vector<double> recentTapIntervalsMs;

    juce::Rectangle<float> inMeterBounds, outMeterBounds;

    void drawHorizontalMeter (juce::Graphics& g, juce::Rectangle<float> bounds, float level, const juce::String& label) const;
};

} // namespace openguitarmultifx
