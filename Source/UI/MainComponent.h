#pragma once

#include "ChainContainer.h"
#include "EffectBlockComponent.h"
#include "FooterBar.h"
#include "RowEndpointBlock.h"
#include "OverlayHost.h"
#include "ParameterPanel.h"
#include "PresetBadge.h"
#include "../EffectRegistry.h"
#include "../Engine/AudioEngine.h"
#include "../Engine/TuningProfile.h"
#include "../Presets/PresetManager.h"
#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/** Plain juce::Viewport, plus a callback for when it scrolls --
    visibleAreaChanged() is a virtual on Viewport itself (not a separate
    Listener interface), so this exists purely to expose it as a
    std::function MainComponent can hook without inheriting from Viewport
    itself. Used to keep the gutter connector stubs MainComponent::paint()
    draws (absolute-coordinate lines that touch the chain's rows) in sync
    whenever the chain viewport scrolls -- see resized()'s comment on why
    it can scroll at all (drawer-open padding) and the bug that motivated
    this, fixed 2026-09-11. */
class ChainViewport : public juce::Viewport
{
public:
    std::function<void()> onScrolled;
    void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onScrolled) onScrolled(); }
};

/**
    Dev-facing chain builder: add any registered effect, in any order, as
    many as you like (Quad Cortex's Grid is the reference point, minus
    presets and split/merge -- those come later). This is the first thing
    in the project that lets you SEE the chain instead of just trusting
    that AudioEngine/SignalGraph work from test output.

    Ownership: MainComponent is the one persistent owner of every
    EffectProcessor for as long as it's in the chain (`chain`). SignalGraph
    itself only ever holds non-owning pointers into that -- see
    SignalGraph.h for why. Removing a block moves it into `graveyard`
    instead of destroying it immediately: the just-superseded SignalGraph
    might still be mid-use on the audio thread for a moment, and that old
    graph still points at this processor.
*/
class MainComponent : public juce::Component,
                       private juce::Timer,
                       private juce::ScrollBar::Listener,
                       private juce::MidiInputCallback
{
public:
    /** Four rows, fixed -- the Quad Cortex Grid reference this follows is a
        fixed grid, and every row now has its own routing endpoints, so this
        is a real capacity rather than just "how many we draw". */
    static constexpr int numRows = 4;

    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    /** targetGridSlot is a grid cell (row*chainColumns+col), not an array
        index -- see EffectBlockComponent::gridSlot's comment. < 0 (the
        default) means "no preference, append after the highest occupied
        slot". */
    void addEffect (const juce::String& registryName, int targetGridSlot = -1);
    void removeEffect (EffectProcessor* processor);
    void handleBlockDragEnded (EffectBlockComponent& block);
    void selectBlock (EffectProcessor* processor);
    void rebuildSignalGraph();
    void layoutChain();
    void showAddEffectMenu (int targetGridSlot = -1);
    void showSettingsPanel();
    void showPresetsPanel();
    void showPolyphonicTuner();
    std::unique_ptr<juce::XmlElement> buildPresetXml() const;
    void applyPresetXml (const juce::XmlElement& xml);
    /** Saves under `name`, reusing its existing number if `name` is already a
        saved preset (a resave, not a new slot) or assigning the next free
        one otherwise. Updates currentPresetName/Number and the top bar either way. */
    void savePresetAs (const juce::String& name);
    /** The one real "load this preset and make it live" path -- both the
        preset list dialog and an incoming MIDI Program Change call this,
        rather than each having its own copy of load->apply->update-display. */
    void loadPresetByName (const juce::String& name);
    void updatePresetDisplay();
    static juce::File getModelsDirectory();
    static juce::File getPresetsDirectory();

    void timerCallback() override;

    /** MIDI thread -- never touches PresetManager/the chain directly, only
        hops to the message thread first. See MainComponent.cpp for why. */
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    /** Same "outlives the object" guard Tone3000Manager uses for its own
        callAsync callbacks: set false in the destructor, captured by value
        (not `this`) in the MIDI handler's callAsync lambda, checked before
        touching `this` -- a Program Change that arrived just before
        shutdown then simply does nothing instead of touching a
        half-destroyed MainComponent. */
    std::shared_ptr<std::atomic<bool>> aliveFlag = std::make_shared<std::atomic<bool>> (true);

    /** Where one row gets its audio from and where it sends it. Rows are
        INDEPENDENT by default -- nothing is connected until you pick it on
        the row's own endpoint tiles, per user decision 2026-09-11 ("some --
        só conecta o que eu escolher"). The old behaviour, where row N
        always implicitly flowed into row N+1, is gone. */
    struct RowRouting
    {
        int inputChannel = -1;   // device input channel feeding this row; -1 = unfed

        // A row's output can go to SEVERAL places at once -- that's a split
        // (user request 2026-09-11: "out 1 pra linha 2 e 3 ao mesmo tempo,
        // sinal splitado"). Destinations are independent toggles, not one
        // choice, so "device output AND line 3" is expressible too.
        bool toDevice = false;
        int deviceOutputPair = 0;
        std::array<bool, numRows> toRows {};

        bool feedsAnything() const
        {
            if (toDevice)
                return true;
            for (const bool t : toRows)
                if (t)
                    return true;
            return false;
        }
    };

    void showRowInputMenu (int row);
    /** Every row currently sending its audio into `row` -- can be more than
        one (a merge, summed by SignalGraph; see Source/Engine/SignalGraph.h).
        Empty if nothing feeds it. */
    std::vector<int> feedersFor (int row) const;
    void showRowOutputMenu (int row);
    /** "Line 2, 3" / "Out 1/2 + Line 3" -- what a row's output tile shows. */
    juce::String describeRowDestinations (int row) const;
    /** "Line 1 + Line 2" -- what a row's input tile shows when fed by more
        than one other row (a merge). */
    juce::String describeRowFeeders (const std::vector<int>& feeders) const;
    void refreshRowEndpoints();

    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;
    /** Pushes the viewport's current scroll state into chainScrollBar (and
        hides it when there's nothing to scroll). Called whenever either the
        content size or the scroll position changes. */
    void syncChainScrollBar();

    EffectRegistry registry;
    AudioEngine audioEngine;
    Tone3000Manager tone3000;

    std::vector<std::unique_ptr<EffectProcessor>> chain;
    juce::OwnedArray<EffectBlockComponent> blocks;
    EffectProcessor* selectedProcessor = nullptr;

    // The click that dismisses the add-effect list ALSO reaches the grid
    // underneath it, and JUCE runs the menu's own callback first -- so by
    // the time the grid sees the click, any "is the menu open?" flag has
    // already been cleared (that's why the first attempt at this didn't
    // work). Remembering WHEN the list closed lets the grid tell "this is
    // the click that just dismissed it" from "this is a new click".
    bool addEffectMenuOpen = false;
    juce::uint32 addEffectMenuClosedAtMs = 0;

    struct RetiredProcessor
    {
        std::unique_ptr<EffectProcessor> processor;
        juce::uint32 retiredAtMs;
    };
    std::vector<RetiredProcessor> graveyard;

    // MIDI CC -> parameter mapping ("MIDI Learn", covers the expression
    // pedal roadmap item too -- an expression pedal is just a continuous
    // CC source from here). In-memory only, not saved into presets yet
    // (see Source/UI/AGENTS.md decision entry). Cleared per-parameter in
    // removeEffect() so a removed block can never leave a dangling
    // AudioParameterFloat* behind in either map.
    std::map<int, juce::AudioParameterFloat*> midiCcBindings;
    juce::AudioParameterFloat* midiLearnArmedParam = nullptr;
    void handleMidiCc (int ccNumber, int ccValue);
    void clearMidiBindingsFor (EffectProcessor* processor);

    // Chain tile size -- SQUARE (blockWidth == blockHeight always) and
    // derived fresh in every resized() call from the available width so
    // exactly chainColumns (8) fit across, not a fixed constant. Leftover
    // VERTICAL space (there's almost always more of it than 4 square tiles
    // need) becomes rowGap between rows instead of stretching the tiles
    // into rectangles -- see resized()'s comment for the maths. Per user
    // request 2026-09-10: "ele tem q ser quadrados n retangulos, n tem
    // problema se tiver espaço entre eles".
    int blockWidth = 122, blockHeight = 122;
    int rowGap = 8;

    // How many blocks fit per row -- a fixed policy now (8), not derived
    // from the window width the way it used to be (blockWidth is derived
    // FROM this instead, see above) -- a row that just kept growing
    // sideways with the window stopped reading as "one pedalboard row" per
    // user request 2026-09-10.
    int chainColumns = 8;

    // How many of the up-to-maxVisibleRows rows are actually occupied right
    // now (blocks + the add-tile) -- set by resized(), used to know which
    // row gets the real IN tile (always row 0) and which gets the real OUT
    // tile (always the last occupied row, not always row 0 the way a
    // single fixed-height IO gutter used to assume). Rows in between get a
    // "continues to next row" connector instead -- see paint().
    int chainUsedRows = 1;

    // The full-height gutter columns either side of the chain, and the Y
    // where row 0 starts -- captured in resized() (only changes when the
    // window itself resizes) and reused by every layoutChain() call
    // (block add/remove/drag, not just a real window resize) to reposition
    // IN/OUT and place the inter-row connector glyphs paint() draws.
    juce::Rectangle<int> leftGutterColumn, rightGutterColumn;
    int chainRowTop = 0;

    // How much extra height the open parameter drawer is currently adding
    // to chainContainer, ON TOP of what the rows themselves need -- that
    // extra room is the only thing that makes scrolling possible while the
    // drawer overlays the bottom rows. layoutChain() has to bake this in
    // every time it sets chainContainer's height, not just once right
    // after resized() computes it: layoutChain() also runs from
    // chainViewport.onScrolled (so dragging the scrollbar re-triggers it),
    // and if it recomputed height from the rows alone it would silently
    // erase this padding on the very first scroll -- which is exactly what
    // was making the scrollbar disappear the instant resized() finished
    // (user report 2026-09-12, screenshot showing no scrollbar at all).
    int drawerScrollPadding = 0;

    ChainViewport chainViewport;
    ChainContainer chainContainer;

    // The chain's scrollbar, pinned to the WINDOW's right edge rather than
    // the viewport's own (which sits well inside the window, left of the
    // IN/OUT gutter column -- a scrollbar floating in the middle of the
    // chain area, per user correction 2026-09-11 with an annotated
    // screenshot). The viewport's built-in bars are off; this one drives it
    // through the ScrollBar::Listener callback instead.
    juce::ScrollBar chainScrollBar { true };

    // One pair of endpoint tiles PER ROW, in the gutters either side of the
    // scrolling viewport: the left one picks which device input feeds that
    // row, the right one picks where the row goes -- a device output pair,
    // or another row (which is what draws the connector between them).
    // Replaced the single IN/OUT pair that used to sit on row 0 and the
    // last occupied row, per user request 2026-09-11 (an annotated
    // screenshot putting a "+" at both ends of every row).
    std::array<RowRouting, numRows> rowRouting;
    std::array<RowEndpointBlock, numRows> rowInputBlocks;
    std::array<RowEndpointBlock, numRows> rowOutputBlocks;

    // Tuner/BPM-tap-tempo/IN-OUT-meters bar, always pinned at the bottom --
    // see FooterBar.h. Visual placeholder only until Phase 5.
    FooterBar footerBar;

    // What AudioEngine's PolyphonicPitchDetector is currently built for --
    // loaded from disk at startup (tunings::loadSavedTuning()), kept here
    // so showPolyphonicTuner() has something to hand the overlay without
    // asking AudioEngine to reconstruct a TuningProfile from its bands.
    TuningProfile currentTuning;

    ParameterPanel parameterPanel;
    PresetManager presets { getPresetsDirectory() };
    juce::String currentPresetName;   // empty = no preset loaded/saved since the last change
    int currentPresetNumber = 0;      // 0 = none yet; a real preset's number is always >= 1

    // presetBadge shows the preset NUMBER, big (see PresetBadge.h -- this
    // is a stage instrument, has to be readable from a few feet away, not
    // just a toolbar). titleLabel shows its NAME, same size class. Tapping
    // the badge opens PresetListDialog (see showPresetsPanel()). Once
    // scenes exist (a preset's own internal variations, e.g. "1A"/"1B"),
    // the badge's text becomes number+letter in that same slot -- see
    // AGENT.md's UI/UX Design Philosophy -- this is deliberately not built
    // yet, just planned for.
    PresetBadge presetBadge;
    // Saves the currently loaded preset in place, no dialog -- only visible
    // once one is actually loaded (see updatePresetDisplay()).
    juce::TextButton quickSaveButton { "Save" };
    juce::Label titleLabel { {}, "No preset loaded" };
    juce::Label cpuLabel;
    juce::TextButton settingsButton { juce::String::fromUTF8 ("\xe2\x8b\xae") }; // vertical ellipsis "..."

    // Every popup that used to be a separate OS window (TONE3000 login,
    // search, installed-model list) is a card drawn on top of this window
    // instead -- see OverlayHost and AGENT.md's UI/UX Design Philosophy.
    OverlayHost overlayHost;
};

} // namespace openguitarmultifx
