#pragma once

#include "../Engine/PolyphonicPitchDetector.h"
#include "../Engine/TuningProfile.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Opened by tapping FooterBar's tuner gauge (per user request -- "seria
    legal abrir a tela de tuner, se clickarmos no afinador"). Shows one
    live gauge per string of the current TuningProfile (4-8, per further
    request for 7/8-string and bass instruments), each independently
    re-tunable via its own note picker -- see GetPolyphonicPitchDetector.h
    for how the underlying per-string detection actually works.

    Purely a view: owns no audio-thread state at all, polls `getReading`
    on its own juce::Timer while open (stops automatically when closed/
    destroyed), and reports every tuning change upward via
    `onTuningChanged` rather than touching AudioEngine or disk itself --
    same "view never owns the real thing" discipline as EffectBlockComponent.
*/
class PolyphonicTunerOverlay : public juce::Component,
                                private juce::Timer
{
public:
    explicit PolyphonicTunerOverlay (TuningProfile initialTuning);
    ~PolyphonicTunerOverlay() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    /** Polled at ~20Hz while this overlay is open. Wired by MainComponent
        to AudioEngine::getTuningStringReading(). */
    std::function<PolyphonicPitchDetector::StringReading (int stringIndex)> getReading;

    /** Fired whenever the tuning changes (preset picked, or one string's
        note edited) -- wired to AudioEngine::setTuningProfile() +
        tunings::saveTuning() so both the live detector and next launch
        pick it up. */
    std::function<void (const TuningProfile&)> onTuningChanged;

    /** Wired by whoever hosts this to OverlayHost::popOverlay. */
    std::function<void()> onPopOverlay;

private:
    struct StringRow
    {
        juce::Label targetLabel;   // the note this string SHOULD be, e.g. "E2"
        juce::ComboBox noteCombo;  // re-tune this string to any note
        juce::Rectangle<float> gaugeBounds;
        bool active = false;
        float cents = 0.0f;
    };

    void timerCallback() override;
    void rebuildStringRows();
    void applyPreset (int presetMenuId);
    void refreshPresetComboText();
    void drawGauge (juce::Graphics& g, juce::Rectangle<float> bounds, const StringRow& row) const;

    TuningProfile profile;
    std::vector<std::unique_ptr<StringRow>> rows;

    juce::Label titleLabel;
    juce::Label presetLabel { {}, "Tuning:" };
    juce::ComboBox presetCombo;
    juce::TextButton closeButton { "Close" };
};

} // namespace openguitarmultifx
