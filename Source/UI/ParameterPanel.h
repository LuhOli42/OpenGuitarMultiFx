#pragma once

#include "../Effects/EffectProcessor.h"
#include "SelectorSwitch.h"
#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
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

    /** The knob page changed, so the height the drawer wants may have changed with it. */
    std::function<void()> onPreferredHeightChanged;

    /** Wired by MainComponent to OverlayHost::pushOverlay/popOverlay -- see
        AGENT.md's UI/UX Design Philosophy. Both the installed-models list
        and the TONE3000 search go through these instead of opening a
        separate OS window. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** "Learn MIDI CC..." was chosen from a knob's long-press menu.
        MainComponent owns what this actually means (arm/cancel/clear) --
        see its wiring. */
    std::function<void (juce::AudioParameterFloat*)> onMidiLearnRequested;
    /** -1 if unbound, else the bound CC number -- queried only when the
        long-press menu is about to open, to word its "Clear MIDI Mapping
        (CC n)" item. */
    std::function<int (juce::AudioParameterFloat*)> getMidiCcForParam;
    /** The colour a knob's name is drawn in when its value differs between the preset's snapshots (the snapshot
        marker of docs/PresetsAndSnapshots.md), transparent when it doesn't. Polled on refresh(), like the MIDI one. */
    std::function<juce::Colour (EffectProcessor*, juce::AudioParameterFloat*)> getSnapshotMarkerColour;

private:
    void rebuildForCurrentProcessor();
    void browseInstalledModels();
    void browseInstalledModelsForSlot (int slot);
    void openTone3000Search();
    void openTone3000SearchForSlot (int slot);
    void showMidiLearnMenu (juce::AudioParameterFloat* param);
    void showPage (int page);
    std::vector<size_t> visibleSliders() const;
    void cycleTempoSync (juce::AudioParameterFloat* param);
    void showValueKeypadFor (juce::Slider* knob);

    EffectProcessor* current = nullptr;
    juce::File modelsDir;
    Tone3000Manager* tone3000 = nullptr;

    juce::Label titleLabel, statusLabel;
    juce::ToggleButton bypassToggle { "Bypassed" };
    juce::TextButton browseInstalledButton { "Browse installed..." };
    juce::TextButton searchTone3000Button { "Search TONE3000..." };
    juce::TextButton removeButton { "Remove" };
    /** Only shown for an effect with more than one page of knobs (EffectProcessor::getParameterPages()). */
    juce::TextButton pageButton { "Page 2 >" };
    int currentPage = 0, pageCount = 1;

    /** A knob's parameter name label, long-pressable to reach MIDI Learn --
        completely invisible until pressed and held, per explicit user
        request ("deixa escondido essa funcao"): no button, no persistent
        indicator even when a CC is already bound. Owns its own
        press-and-hold detection (juce::Label has no such gesture built in)
        via mouseDown/mouseUp/timerCallback. */
    class LongPressLabel : public juce::Label,
                            private juce::Timer
    {
    public:
        std::function<void()> onLongPress;

        void mouseDown (const juce::MouseEvent&) override { startTimer (550); }
        void mouseUp (const juce::MouseEvent&) override { stopTimer(); }
        void mouseDrag (const juce::MouseEvent&) override { stopTimer(); } // a drag is not a press-and-hold

    private:
        void timerCallback() override
        {
            stopTimer();
            if (onLongPress)
                onLongPress();
        }
    };

    /** A rotary knob with a hidden fine-tune drag (audit #93 item 1): hold
        it for the app's usual 550 ms and the drag drops to ~1/8 speed for
        the rest of that gesture -- a fingertip can land an exact value
        without a keyboard. A brighter ring is the only cue; like MIDI
        Learn it's a deliberate gesture, not persistent chrome. When armed,
        the drag re-bases at the current pointer and value so
        fine motion continues from the coarse-drag position without a jump. */
    class FineTuneSlider : public juce::Slider,
                           private juce::Timer
    {
    public:
        using juce::Slider::Slider;

        void mouseDown (const juce::MouseEvent& event) override
        {
            lastDrag.reset();
            juce::Slider::mouseDown (event);
            startTimer (finePressMs);
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            lastDrag.emplace (event);
            juce::Slider::mouseDrag (event);
        }

        void mouseUp (const juce::MouseEvent& event) override
        {
            lastDrag.reset();
            endFineDrag();
            juce::Slider::mouseUp (event);
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            if (! isMouseButtonDown())
                return;
            coarseSensitivity = getMouseDragSensitivity();
            setMouseDragSensitivity (coarseSensitivity * fineFactor);
            fineDragArmed = true;
            if (lastDrag)
                juce::Slider::mouseDown (*lastDrag);
            repaint();
        }

        void endFineDrag()
        {
            stopTimer();
            if (fineDragArmed)
            {
                fineDragArmed = false;
                setMouseDragSensitivity (coarseSensitivity);
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            juce::Slider::paint (g);
            if (fineDragArmed)
            {
                g.setColour (findColour (juce::Slider::rotarySliderFillColourId).brighter (0.6f));
                g.drawEllipse (getLocalBounds().toFloat().reduced (1.5f), 2.0f);
            }
        }

        bool fineDragArmed = false;
        int coarseSensitivity = 250;
        std::optional<juce::MouseEvent> lastDrag;
        static constexpr int finePressMs = 550; // same duration as LongPressLabel / SnapshotBar
        static constexpr int fineFactor = 8;
    };

    /** The value readout under a knob, drawn like the JUCE text box it
        replaced (dark field + centred value) but tap-to-open the on-screen
        keypad -- the target device has no OS keyboard for the original
        editable box (audit #93 item 1). */
    class ValueFieldButton : public juce::Button
    {
    public:
        ValueFieldButton() : juce::Button ({}) {}

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff0f0f0f));
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (down ? 0.45f : highlighted ? 0.35f : 0.22f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (down ? 1.0f : 0.9f));
            g.setFont (juce::Font (15.0f));
            g.drawText (getButtonText(), bounds.toNearestInt(), juce::Justification::centred);
        }
    };

    struct SliderRow
    {
        /** The value/param model, ALWAYS built and wired exactly as before (setRange/setValue/onValueChange) --
            for a discrete-selector param (see `selector` below) it is never added to the component tree or shown;
            it exists purely so every existing param-write/param-read path keeps working unchanged. */
        std::unique_ptr<juce::Slider> slider;

        /** Non-null for a param with a handful of discrete, labelled steps (4/8/16 ohm, Normal/Jumped/Bright) --
            shown INSTEAD of `slider` (see widget()), a real multi-position switch rather than a knob that happens
            to snap. Its onChange writes through `slider->setValue()`, reusing the same param-write lambda. */
        std::unique_ptr<SelectorSwitch> selector;

        std::unique_ptr<LongPressLabel> label;
        /** The tap-to-type value readout under the knob (see ValueFieldButton);
            only on rows showing a real knob -- selector rows draw their own labels. */
        std::unique_ptr<ValueFieldButton> valueBox;
        juce::AudioParameterFloat* param = nullptr;
        int page = 0;
        bool active = true; // the processor's isParameterActive() at the last layout: false = hidden

        /** Only non-null for a param the processor registered via
            registerTempoSyncParam() -- an always-visible small inline "ms"/
            subdivision switch, per explicit user request (contrast with
            MIDI Learn's long-press: this one was asked to stay visible,
            "like a real pedal's mini-switch", not hidden). Tapping it
            cycles ms -> 1/32 -> ... -> 1/1 -> ms, matching how a physical
            multi-position selector switch steps -- no popup needed. */
        std::unique_ptr<juce::TextButton> syncToggle;

        /** The Component actually shown/laid out for this row: the switch if there is one, else the slider. */
        juce::Component* widget() const noexcept
        {
            return selector != nullptr ? static_cast<juce::Component*> (selector.get()) : static_cast<juce::Component*> (slider.get());
        }
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
