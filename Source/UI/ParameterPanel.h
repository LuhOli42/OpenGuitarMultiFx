#pragma once

#include "../Effects/EffectProcessor.h"
#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Generic parameter editor: whatever processor is selected, this walks
    its getParameters() group and builds one knob per
    juce::AudioParameterFloat, live. No per-effect UI code anywhere --
    every Phase 1 pedal's state is fully described by float parameters
    (see EffectProcessor's default getState()/setState()), so a generic
    editor covers all of them for free, including ones added later.

    Also offers TONE3000 search for whichever block is selected, via a
    popup (Tone3000SearchDialog) pre-filtered to that block's gear category
    -- a Neural Amp block only ever searches amps, a Cab block only ever
    searches cabs (see GearRouting.h::gearFilterForProcessorName).
    Tone3000Manager is injected and optional (setTone3000Manager()); with
    none set, the search button just never appears -- the integration
    stays optional end to end, per ARCHITECTURE.md.
*/
class ParameterPanel : public juce::Component
{
public:
    ParameterPanel();

    /** Base models directory (see MainComponent::getModelsDirectory) -- every
        block only ever browses/saves into its OWN category subfolder under
        here (amps/pedals/cabs/reverbs -- see GearRouting.h), so a Neural
        Pedal block never lists an amp capture and vice versa. */
    void setModelsDirectory (juce::File directory) { modelsDir = std::move (directory); }

    void setTone3000Manager (Tone3000Manager& managerToUse) { tone3000 = &managerToUse; }

    void setProcessor (EffectProcessor* processorToEdit);
    void refresh(); // call periodically -- picks up status text changes (e.g. after a model load)

    /** How tall this panel would need to be, at the given width, to show
        everything for the current processor without the knob grid having
        to scroll. MainComponent uses this to size the drawer adaptively
        (still capped -- see MainComponent::resized()) instead of always
        taking a fixed fraction of the window regardless of content. 0 with
        nothing selected. Must mirror resized()'s own layout math. */
    int getPreferredContentHeight (int availableWidth) const;

    void resized() override;
    void paint (juce::Graphics& g) override;

    std::function<void (EffectProcessor*)> onRemoveRequested;

    /** Wired by MainComponent to OverlayHost::pushOverlay/popOverlay -- see
        AGENT.md's UI/UX Design Philosophy. Both the installed-models list
        and the TONE3000 search go through these instead of opening a
        separate OS window. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** Tapped a knob's MIDI-learn button. MainComponent owns what this
        actually means (arm/cancel/clear) -- see its wiring. */
    std::function<void (juce::AudioParameterFloat*)> onMidiLearnRequested;
    /** -1 if unbound, else the bound CC number -- queried on every
        refresh() to keep each knob's MIDI button text current. */
    std::function<int (juce::AudioParameterFloat*)> getMidiCcForParam;
    /** True while this exact param is the one currently listening for the
        next CC message. */
    std::function<bool (juce::AudioParameterFloat*)> isMidiLearnArmedForParam;

private:
    void rebuildForCurrentProcessor();
    void browseInstalledModels();
    void openTone3000Search();

    EffectProcessor* current = nullptr;
    juce::File modelsDir;
    Tone3000Manager* tone3000 = nullptr;

    juce::Label titleLabel, statusLabel;
    juce::ToggleButton bypassToggle { "Bypassed" };
    juce::TextButton browseInstalledButton { "Browse installed..." };
    juce::TextButton searchTone3000Button { "Search TONE3000..." };
    juce::TextButton removeButton { "Remove" };

    struct SliderRow
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::TextButton> midiButton;
        juce::AudioParameterFloat* param = nullptr;
    };
    std::vector<SliderRow> sliders;

    // The knob grid scrolls instead of clipping -- this drawer is capped at
    // 1/4 of the window (see MainComponent), which a processor with a lot
    // of parameters can easily be taller than. knobGridHost is sized to fit
    // every row of knobs and lives inside knobViewport, which is what's
    // actually capped to the drawer's bounds.
    juce::Viewport knobViewport;
    juce::Component knobGridHost;

    // The strip between the header row and the knob grid (where statusLabel
    // sits) is filled in the selected effect's accent colour in paint() --
    // per user request 2026-09-10, the background BEHIND the knobs stays
    // the plain panel grey; only this one band reads as "belongs to this
    // effect". Full width (not inset to the 10px margin the rest of the
    // panel uses) so it reads as a solid banner. Computed in resized(),
    // consumed in paint().
    juce::Rectangle<int> accentBandBounds;
};

} // namespace openguitarmultifx
