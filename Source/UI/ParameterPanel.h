#pragma once

#include "../Effects/EffectProcessor.h"
#include "Theme.h"
#include "SelectorSwitch.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "../Tone3000/Tone3000Manager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <cmath>

namespace openguitarmultifx
{

class EffectRegistry;

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

    /** Registry metadata for the drawer subtitle (category/subcategory) and
        the prototype's category-coloured drawer chrome. Optional: without
        it the panel falls back to the processor's own accent colour. */
    void setEffectRegistry (const EffectRegistry& registryToUse) noexcept { effectRegistry = &registryToUse; }

    /** Knob screens for the current effect (after splitting long pages, see rebuildForCurrentProcessor()). */
    int getPageCount() const noexcept { return pageCount; }
    void selectPage (int page) { showPage (page); }

    /** MainComponent supplies "Line x, slot y" for the selected block; the
        drawer has no way to know grid placement on its own. */
    void setBlockLocationProvider (std::function<juce::String (EffectProcessor*)> provider)
    {
        blockLocationProvider = std::move (provider);
    }

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
    /** Title/subtitle colours cache theme tokens -- re-read them on a theme switch. */
    void lookAndFeelChanged() override;

    std::function<void (EffectProcessor*)> onRemoveRequested;
    std::function<void()> onCloseRequested;

    /** The knob page changed, so the height the drawer wants may have changed with it. */
    std::function<void()> onPreferredHeightChanged;

    /** Wired by MainComponent to OverlayHost::pushOverlay/popOverlay -- see
        AGENT.md's UI/UX Design Philosophy. Both the installed-models list
        and the TONE3000 search go through these instead of opening a
        separate OS window. */
    std::function<void (std::unique_ptr<juce::Component>)> onPushOverlay;
    std::function<void()> onPopOverlay;

    /** "Learn MIDI CC..." was chosen from a knob's long-press menu or the
        drawer's MIDI button. MainComponent owns what this actually means
        (arm/cancel/clear) -- see its wiring. */
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
    void showMidiLearnMenuForProcessor();
    void showPage (int page);
    void refreshLabelStyles();
    void updateSubtitle();
    struct SliderRow;
    std::vector<size_t> visibleSliders() const;
    int contentHeightForRow (const SliderRow& row) const;
    void cycleTempoSync (juce::AudioParameterFloat* param);
    void showValueKeypadFor (juce::Slider* knob);

    EffectProcessor* current = nullptr;
    juce::File modelsDir;
    Tone3000Manager* tone3000 = nullptr;
    const EffectRegistry* effectRegistry = nullptr;
    std::function<juce::String (EffectProcessor*)> blockLocationProvider;

    /** The prototype's 50px category-tinted icon tile in the drawer header. */
    class HeaderIcon : public juce::Component
    {
    public:
        void setProcessor (EffectProcessor* processorToUse) noexcept { processor = processorToUse; repaint(); }
        void paint (juce::Graphics& g) override;
    private:
        EffectProcessor* processor = nullptr;
        std::unique_ptr<juce::Drawable> ampHeadIcon;
    };

    HeaderIcon headerIcon;
    juce::Label titleLabel, subtitleLabel;
    juce::String subtitleBase;

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

    /** One numbered page tab, painted as the prototype's segmented control:
        raised container, lit active tab, accent underline (or solid accent
        fill in Bold Blocks). */
    class PageTabButton : public juce::TextButton
    {
    public:
        PageTabButton (const juce::String& name) : juce::TextButton (name) {}
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    /** The prototype's "Ligado" rocker: a 66x36 visual switch inside a
        48px-high touch target (the visual is shorter than the hit area,
        never the other way around). `on` means the effect is LIVE, so the
        panel writes bypassed = !on. */
    class BypassSwitch : public juce::Button
    {
    public:
        BypassSwitch() : juce::Button ({}) {}
        void setOn (bool shouldBeOn) { if (on != shouldBeOn) { on = shouldBeOn; repaint(); } } // refresh() calls this every 50 ms
        bool isOn() const noexcept { return on; }
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
        std::function<void()> onChange;
    private:
        bool on = true;
    };

    // Public only so the file-local glyph painter can name PillButton::Glyph.
public:
    class PillButton : public juce::Button
    {
    public:
        enum class Glyph { none, switchModel, midi, cloud, trash, close };
        PillButton (const juce::String& text, Glyph glyphIn) : juce::Button (text), glyph (glyphIn) {}
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
        void setGlyph (Glyph glyphIn) { glyph = glyphIn; repaint(); }
    private:
        Glyph glyph;
    };

private:
    /** One tab per knob page, only for an effect with more than one page of
        knobs (EffectProcessor::getParameterPages()) -- replaces the old
        single cycling "Page 2 >" button (redesign card 3). */
    std::vector<std::unique_ptr<PageTabButton>> pageTabs;
    std::vector<juce::String> pageLabels;
    int currentPage = 0, pageCount = 1;

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

        void paint (juce::Graphics& g) override;

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

        bool fineDragArmed = false;
        int coarseSensitivity = 250;
        std::optional<juce::MouseEvent> lastDrag;
        static constexpr int finePressMs = 550; // same duration as LongPressLabel / SnapshotBar
        static constexpr int fineFactor = 8;
    };

    /** The value readout under a knob, drawn like the prototype's .kv
        button (rounded field + centred value) but tap-to-open the
        on-screen keypad -- the target device has no OS keyboard for the
        original editable box (audit #93 item 1). The visual is 36px high
        centred in a 48px touch target. */
    class ValueFieldButton : public juce::Button
    {
    public:
        ValueFieldButton() : juce::Button ({}) {}
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
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
        int selectorSteps = 0;
        bool active = true; // the processor's isParameterActive() at the last layout: false = hidden
        bool fader = false; // drawn as a vertical fader (prefersFader)

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
    // 45% of the window (see MainComponent), which a processor with a lot
    // of parameters can easily be taller than. knobGridHost is sized to fit
    // every row of knobs and lives inside knobViewport, which is what's
    // actually capped to the drawer's bounds.
    juce::Viewport knobViewport;
    juce::Component knobGridHost;

    juce::Rectangle<int> pageTabsBounds;
    juce::Colour effectAccent = juce::Colours::transparentBlack;
    juce::Image shadowCache;               // stage-style panel shadow, see paint()
    juce::Point<int> shadowCacheSize;
    BypassSwitch bypassSwitch;
    PillButton modelButton { "Trocar modelo", PillButton::Glyph::switchModel };
    PillButton midiButton { "MIDI", PillButton::Glyph::midi };
    PillButton tone3000Button { "TONE3000", PillButton::Glyph::cloud };
    PillButton removeButton { {}, PillButton::Glyph::trash };
    PillButton closeButton { {}, PillButton::Glyph::close };
};

} // namespace openguitarmultifx
