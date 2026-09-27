#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    A chromatic tuner: one note at a time, whatever note it is.

    This replaced a six-string polyphonic overlay (2026-09-21, user request: "deixar só o q identifica uma nota, mas ele
    ser bom pra pegar de qualquer frequência"). Chromatic means there is no tuning profile to pick or keep in sync -- the
    detector reports a frequency and this names the nearest note, so it tunes any instrument, any tuning, including the
    low strings the old one could not reach at all (its analysis floor was 70 Hz, above B0/E1/F#1/A1/B1).
*/
class TunerOverlay : public juce::Component,
                     private juce::Timer
{
public:
    TunerOverlay();
    ~TunerOverlay() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Polled at 20 Hz while this overlay is open. Wired to AudioEngine::getDetectedFrequencyHz(); 0 means no pitch. */
    std::function<float()> getFrequencyHz;

    /** Wired by whoever hosts this to OverlayHost::popOverlay. */
    std::function<void()> onPopOverlay;

    /** Fired from the destructor, however the overlay was dismissed -- MainComponent turns the pitch analysis back off
        with it (AudioEngine::setTunerActive()). */
    std::function<void()> onDestroyed;

private:
    void timerCallback() override;

    juce::Label titleLabel, hintLabel;
    juce::TextButton closeButton { "Close" };
    juce::Rectangle<float> gaugeBounds, noteBounds, readoutBounds;

    bool active = false;
    juce::String noteLetter, noteAccidental;
    int noteOctave = 0;
    float frequencyHz = 0.0f;
    float cents = 0.0f;         // smoothed, -50..+50
    float smoothedCents = 0.0f;
    float activityLevel = 0.0f; // fast attack / slow release, so the readout does not flicker between updates
};

} // namespace openguitarmultifx
