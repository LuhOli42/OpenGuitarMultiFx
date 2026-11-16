#pragma once

#include "OverlayChrome.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace openguitarmultifx
{

/**
    A chromatic tuner: one note at a time, whatever note it is.

    Two meter styles, picked from the header's segmented control: a radial cents fan
    (needle) and a strobe ring whose lit wedge drifts while the note is off. The MIDI
    Learn button (a footswitch that opens and closes the tuner) and the Mute toggle live
    in the header; MainComponent owns the state and the learning.
*/
class TunerOverlay : public juce::Component,
                     private juce::Timer
{
public:
    TunerOverlay();
    ~TunerOverlay() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Polled at 30 Hz while this overlay is open. Wired to AudioEngine::getDetectedFrequencyHz(); 0 means no pitch. */
    std::function<float()> getFrequencyHz;

    /** Wired by whoever hosts this to OverlayHost::popOverlay. */
    std::function<void()> onPopOverlay;

    std::function<void (bool)> onMuteToggled;
    std::function<void()> onMidiLearnTapped;
    std::function<void (bool)> onStrobeModeChanged;
    void setMuteState (bool muted);
    void setMidiLabel (const juce::String& text, bool armed, bool bound = false);
    void setStrobeMode (bool strobe);

    /** Fired from the destructor, however the overlay was dismissed -- MainComponent turns the pitch analysis back off
        with it (AudioEngine::setTunerActive()). */
    std::function<void()> onDestroyed;

private:
    void timerCallback() override;
    void paintNoteBlock (juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintNeedleMeter (juce::Graphics& g, juce::Rectangle<float> bounds);
    void paintStrobeMeter (juce::Graphics& g, juce::Rectangle<float> bounds);

    juce::Label titleLabel, referenceLabel;
    overlaychrome::CloseButton closeButton;
    overlaychrome::ActionButton muteButton, midiButton, needleButton, strobeButton;
    juce::Rectangle<float> noteBounds, readoutBounds, meterBounds, segBounds;

    bool active = false;
    bool strobeMode = false;
    juce::String noteLetter, noteAccidental;
    int noteOctave = 0;
    float frequencyHz = 0.0f;
    float cents = 0.0f;         // smoothed, -50..+50
    float smoothedCents = 0.0f;
    float activityLevel = 0.0f; // fast attack / slow release, so the readout does not flicker between updates
};

} // namespace openguitarmultifx
