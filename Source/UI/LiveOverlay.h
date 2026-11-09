#pragma once

#include "Theme.h"
#include "OverlayChrome.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace openguitarmultifx
{

/**
    Stage-mode screen (redesign card 6, docs/ui/REDESIGN.md): a giant preset
    tile, a TUNER shortcut and BPM readout, and the eight scene tiles --
    all readable from a couple of metres on the 10" touchscreen. The tiles
    mirror what the phase-8 footswitches will do (a learned scene CC
    recalls a snapshot -- see MainComponent::toggleScenePedal /
    selectSnapshot), so each tile shows its scene's colour, name and which
    one is live.

    The card knows nothing about presets, audio or MIDI: MainComponent feeds
    it strings/colours/numbers through the setters and receives intents
    through the callbacks. BPM is FooterBar's tap tempo (the only BPM owner),
    pushed in by MainComponent's timer; the TUNER tile opens the existing
    tuner overlay rather than running a second pitch analysis here.
*/
class LiveOverlay : public juce::Component
{
public:
    LiveOverlay();
    ~LiveOverlay() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onClose;
    std::function<void()> onTunerTapped;
    std::function<void (int sceneIndex)> onSceneSelected;
    /** Fires from the destructor (OverlayHost owns the card) so MainComponent
        can drop its non-owning pointer. */
    std::function<void()> onDestroyed;

    /** The preset tile: `badge` is the compact identity ("12C" -- preset
        number + live scene letter), `title` the preset name, `subtitle` the
        scene detail line. */
    void setPresetInfo (juce::String badge, juce::String title, juce::String subtitle);
    /** The eight scenes A-H: names (empty -> letter), resolved colours, how
        many exist, and which is live. Tiles beyond `count` draw dimmed and
        don't answer taps. */
    void setScenes (const std::array<juce::String, 8>& sceneNames,
                    const std::array<juce::Colour, 8>& sceneColours,
                    int count, int currentIndex);
    void setBpm (double bpm);

private:
    /** One scene tile: the scene's colour as its border/fill accent, a LED
        for the live scene, letter chip + name. Custom-painted (like
        EffectBlockComponent) so it follows the three themes. */
    class SceneTile : public juce::Button
    {
    public:
        SceneTile() : juce::Button ({}) {}

        juce::String letter, name;
        juce::Colour colour;
        bool active = false;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void refreshTiles();
    juce::Font displayFont (float size) const;

    juce::Typeface::Ptr displayTypeface; // Sora ExtraBold, captured from the app LookAndFeel

    overlaychrome::CloseButton closeButton;
    overlaychrome::ActionButton tunerButton;
    juce::Label bpmValueLabel, bpmUnitLabel, captionLabel;

    juce::Rectangle<float> presetArea; // computed in resized(), painted in paint()

    juce::String presetBadge, presetTitle, presetSubtitle;
    std::array<juce::String, 8> sceneNames;
    std::array<juce::Colour, 8> sceneColours {};
    int sceneCount = 0, currentScene = 0;
    double bpm = 120.0;

    std::array<std::unique_ptr<SceneTile>, 8> tiles;
};

} // namespace openguitarmultifx
