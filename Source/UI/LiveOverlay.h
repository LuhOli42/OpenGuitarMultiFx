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
    /** A big button was tapped (fires its action) or, in EDIT mode, picked for re-assignment. */
    std::function<void (int slot)> onTileTapped;
    std::function<void (int slot)> onTileEdit;
    /** Fires from the destructor (OverlayHost owns the card) so MainComponent
        can drop its non-owning pointer. */
    std::function<void()> onDestroyed;

    /** The preset tile: `badge` is the compact identity ("12C" -- preset
        number + live scene letter), `title` the preset name, `subtitle` the
        scene detail line. */
    void setPresetInfo (juce::String badge, juce::String title, juce::String subtitle);
    /** What one big button shows: the small `letter` chip (scene letter, or "FX"), the `name`, its accent colour, whether
        it is lit (live scene / effect on) and whether it answers taps. */
    struct TileInfo
    {
        juce::String letter, name;
        juce::Colour colour;
        bool active = false;
        bool enabled = true;
    };

    void setTiles (const std::array<TileInfo, 8>& newTiles);
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
        bool editing = false; // EDIT mode: dashed outline + "tap to assign"

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    void refreshTiles();
    juce::Font displayFont (float size) const;

    juce::Typeface::Ptr displayTypeface; // Sora ExtraBold, captured from the app LookAndFeel

    overlaychrome::CloseButton closeButton;
    overlaychrome::ActionButton tunerButton, editButton;
    juce::Label bpmValueLabel, bpmUnitLabel, captionLabel;

    juce::Rectangle<float> presetArea; // computed in resized(), painted in paint()

    juce::String presetBadge, presetTitle, presetSubtitle;
    std::array<TileInfo, 8> tileInfos;
    bool editMode = false;
    double bpm = 120.0;

    std::array<std::unique_ptr<SceneTile>, 8> tiles;
};

} // namespace openguitarmultifx
