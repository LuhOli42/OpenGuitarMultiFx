#include "ParameterPanel.h"
#include "Theme.h"

#include "ModelListDialog.h"
#include "PromptCards.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Tone3000SearchDialog.h"
#include "TouchSizing.h"
#include "../Tone3000/GearRouting.h"

#include <algorithm>

namespace openguitarmultifx
{

namespace
{
    // Shared between getPreferredContentHeight() and resized() -- they
    // have to agree exactly, or the drawer sizes itself for a knob grid
    // different from the one actually laid out (that mismatch is exactly
    // what caused clipping/pointless-scrolling before, see AGENT.md).
    // Sized generously rather than at TouchSizing.h's bare minimum: a
    // rotary knob needs real drag travel to feel controllable by finger,
    // not just be technically tappable. Bumped 2026-09-10 alongside the
    // label/value fonts below (stage-readable from a couple of metres, per
    // user request) -- see Source/UI/AGENTS.md's decision entry on why this
    // no longer guarantees two full knob rows fit without scrolling at the
    // dev window's default size (it did before this bump; still true for
    // every processor that exists today, since none has more than 5
    // params and even one row comfortably holds ~10 at this width).
    constexpr int knobCellWidth = 118;
    constexpr int knobCellHeight = 140;
    constexpr int knobDiameter = 90;
    constexpr int knobLabelHeight = 20;
    constexpr int knobValueHeight = 26;

    // Extra row height ONLY for a knob whose param registered for tempo
    // sync (see EffectProcessor::registerTempoSyncParam()) -- most knobs
    // don't have this, so cell height is computed per VISUAL grid row (the
    // max over whichever knobs land in it), not a single global constant,
    // to avoid reintroducing the "wasted space under every knob" MIDI
    // Learn was corrected for (this toggle is deliberately visible by
    // request, but only on the knobs that actually have it).
    constexpr int syncToggleHeight = 48; // touch::minTapTarget -- audit #93 item 5 flagged the old 22px row
}

ParameterPanel::ParameterPanel()
{
    // The stage drawer's drop shadow projects above the panel's bounds;
    // paint() must be allowed to draw that sliver over the chain.
    setPaintingIsUnclipped (true);

    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (22.0f, juce::Font::bold));

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::Font (15.0f));
    statusLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

    addAndMakeVisible (bypassToggle);
    bypassToggle.onClick = [this] { if (current != nullptr) current->setBypassed (bypassToggle.getToggleState()); };

    addAndMakeVisible (browseInstalledButton);
    browseInstalledButton.onClick = [this] { browseInstalledModels(); };

    addAndMakeVisible (searchTone3000Button);
    searchTone3000Button.onClick = [this] { openTone3000Search(); };

    addAndMakeVisible (removeButton);
    removeButton.onClick = [this] { if (current != nullptr && onRemoveRequested) onRemoveRequested (current); };

    addAndMakeVisible (knobViewport);
    knobViewport.setViewedComponent (&knobGridHost, false);
    knobViewport.setScrollBarsShown (true, false);

    rebuildForCurrentProcessor();
}

void ParameterPanel::lookAndFeelChanged()
{
    statusLabel.setColour (juce::Label::textColourId, theme::current().textMuted);
    if (current != nullptr)
        titleLabel.setColour (juce::Label::textColourId, theme::current().text);
    refreshLabelStyles();
    juce::Component::lookAndFeelChanged();
}

void ParameterPanel::refreshLabelStyles()
{
    const auto& t = theme::current();
    for (auto& row : sliders)
    {
        if (row.label == nullptr || row.param == nullptr)
            continue;

        auto text = juce::String (row.param->getName (64));
        if (t.uppercaseLabels)
            text = text.toUpperCase();
        row.label->setText (text, juce::dontSendNotification);
        row.label->setFont (juce::Font (12.0f, juce::Font::bold));
        row.label->setColour (juce::Label::textColourId, t.textMuted);
    }
}

void ParameterPanel::setProcessor (EffectProcessor* processorToEdit)
{
    current = processorToEdit;
    rebuildForCurrentProcessor();
}

void ParameterPanel::refresh()
{
    if (current != nullptr)
    {
        statusLabel.setText (current->getStatusText(), juce::dontSendNotification);
        bypassToggle.setToggleState (current->isBypassed(), juce::dontSendNotification);

        // A channel switch (from the panel, MIDI or a preset) can make controls appear or disappear: re-lay out the page.
        for (const auto& row : sliders)
            if (row.param != nullptr && current->isParameterActive (*row.param) != row.active)
            {
                showPage (currentPage);
                break;
            }

        // Parameters can change from outside the knob (MIDI CC, a multi-channel amp loading the selected channel's
        // stored knobs): keep what is drawn in step with the real value.
        for (auto& row : sliders)
        {
            if (row.param != nullptr && row.label != nullptr && getSnapshotMarkerColour)
            {
                const auto marker = getSnapshotMarkerColour (current, row.param);
                const auto colour = marker.isTransparent() ? theme::current().textMuted : marker;
                if (row.label->findColour (juce::Label::textColourId) != colour)
                    row.label->setColour (juce::Label::textColourId, colour);
            }

            if (row.param == nullptr || row.slider == nullptr || row.slider->isMouseButtonDown())
                continue;
            const float v = row.param->get();
            if (std::abs ((double) v - row.slider->getValue()) > 1.0e-6)
                row.slider->setValue (v, juce::dontSendNotification);
            if (row.valueBox != nullptr)
                row.valueBox->setButtonText (row.slider->getTextFromValue (row.slider->getValue()));
            if (row.selector != nullptr)
            {
                const auto range = row.param->getNormalisableRange();
                row.selector->setSelectedIndex ((int) std::lround ((v - range.start) / range.interval), juce::dontSendNotification);
            }
        }

        const auto& bindings = current->getTempoSyncBindings();
        for (auto& row : sliders)
        {
            if (row.syncToggle == nullptr)
                continue;

            const auto it = std::find_if (bindings.begin(), bindings.end(),
                [&row] (const TempoSyncBinding& b) { return b.param == row.param; });
            if (it == bindings.end())
                continue;

            row.syncToggle->setButtonText (it->synced
                ? juce::String (tempoSync::subdivisions[(size_t) it->subdivisionIndex].label)
                : juce::String ("ms"));

            // Synced: the knob just displays the BPM-computed ms value
            // (MainComponent's timer keeps pushing it) -- disabled so a
            // stray drag doesn't fight that every 50ms.
            row.slider->setEnabled (! it->synced);
            if (it->synced)
                row.slider->setValue (row.param->get(), juce::dontSendNotification);
        }
    }
}

void ParameterPanel::rebuildForCurrentProcessor()
{
    for (auto& tab : pageTabs)
        removeChildComponent (tab.get());
    pageTabs.clear();

    for (auto& row : sliders)
    {
        knobGridHost.removeChildComponent (row.widget());
        knobGridHost.removeChildComponent (row.label.get());
        if (row.valueBox != nullptr)
            knobGridHost.removeChildComponent (row.valueBox.get());
        if (row.syncToggle != nullptr)
            knobGridHost.removeChildComponent (row.syncToggle.get());
    }
    sliders.clear();

    if (current == nullptr)
    {
        // Nothing to show at all -- no title, no hint text, just the flat
        // black background from paint(). Per AGENT.md's UI/UX Design
        // Philosophy: this panel is a detail drawer that only exists once
        // you've tapped a block, not a permanent toolbar with an idle state.
        titleLabel.setText ({}, juce::dontSendNotification);
        statusLabel.setText ({}, juce::dontSendNotification);
        bypassToggle.setVisible (false);
        browseInstalledButton.setVisible (false);
        searchTone3000Button.setVisible (false);
        removeButton.setVisible (false);
        knobViewport.setVisible (false);
        resized();
        return;
    }

    titleLabel.setText (current->getName(), juce::dontSendNotification);
    knobViewport.setVisible (true);
    bypassToggle.setVisible (true);
    bypassToggle.setToggleState (current->isBypassed(), juce::dontSendNotification);
    browseInstalledButton.setVisible (current->wantsModelFile());
    searchTone3000Button.setVisible (current->wantsModelFile() && tone3000 != nullptr);
    removeButton.setVisible (true);

    // The drawer's buttons, knobs, tabs and Bold Blocks' top edge pick up
    // the selected effect's own accent colour (EffectBlockComponent already
    // outlines the block itself in this colour) so the whole drawer reads
    // as "this effect's controls" rather than generic chrome.
    // Knobs, selectors and toggles draw in the effect's accent colour on a near-black panel; an amp whose accent is
    // itself near black (the 5150's dark grey, the Rectifier's navy) left its knob arcs and switches unreadable.
    // Keep the hue, lift the brightness until it reads.
    const auto rawAccent = current->getAccentColour();
    const auto accent = rawAccent.getPerceivedBrightness() < 0.45f
                            ? rawAccent.withBrightness (juce::jmax (0.8f, rawAccent.getBrightness()))
                                       .withSaturation (juce::jmin (0.6f, rawAccent.getSaturation()))
                            : rawAccent;
    titleLabel.setColour (juce::Label::textColourId, theme::current().text);
    effectAccent = accent;
    bypassToggle.setColour (juce::ToggleButton::tickColourId, accent);
    for (auto* b : { &browseInstalledButton, &searchTone3000Button, &removeButton })
        b->setColour (OpenGuitarMultiFxLookAndFeel::accentColourId, accent);

    const auto pages = current->getParameterPages();
    pageCount = (int) pages.size();
    currentPage = 0;

    // One numbered tab per knob page (redesign card 3) -- the pages are
    // unnamed sub-groups (EffectProcessor::getParameterPages()), so the tabs
    // read "1", "2", ... with PageTabButton painting the active state.
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        auto tab = std::make_unique<PageTabButton> (juce::String (pageIndex + 1));
        tab->setClickingTogglesState (true);
        tab->setColour (OpenGuitarMultiFxLookAndFeel::accentColourId, accent);
        tab->onClick = [this, pageIndex] { showPage (pageIndex); };
        addChildComponent (*tab);
        tab->setVisible (true);
        pageTabs.push_back (std::move (tab));
    }

    {
        for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            for (auto* floatParam : pages[(size_t) pageIndex])
            {
                SliderRow row;
                row.param = floatParam;
                row.page = pageIndex;
                row.slider = std::make_unique<FineTuneSlider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                               juce::Slider::NoTextBox);
                row.slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                                 juce::MathConstants<float>::pi * 2.75f, true);
                // No editable text box: on the device it would summon an OS
                // keyboard that doesn't exist -- the tap-to-open keypad
                // (valueBox below) replaces it entirely.
                row.slider->setColour (juce::Slider::rotarySliderFillColourId, accent);
                row.slider->setColour (juce::Slider::thumbColourId, accent);

                row.label = std::make_unique<LongPressLabel>();
                row.label->setJustificationType (juce::Justification::centred);
                row.label->onLongPress = [this, floatParam] { showMidiLearnMenu (floatParam); };

                const auto range = floatParam->getNormalisableRange();
                row.slider->setRange (range.start, range.end, range.interval > 0.0f ? range.interval : 0.01);
                row.slider->setValue (floatParam->get(), juce::dontSendNotification);
                const bool isSelector = range.interval >= 1.0f && (range.end - range.start) / range.interval <= 8.0f // 4/8/16 ohm, Normal/Jumped/Bright...
                                     && ! current->prefersSteppedKnob (*floatParam);
                if (! isSelector)
                    // Double-tap centres the knob at noon (0.5), not its factory
                    // default -- a player zeroing a knob expects dead centre.
                    // Selectors are exempt: their option stack eats the taps.
                    row.slider->setDoubleClickReturnValue (true, range.convertFrom0to1 (0.5f));
                if (isSelector || range.interval >= 1.0f)
                {
                    row.slider->setNumDecimalPlacesToDisplay (0);
                    row.slider->textFromValueFunction = [floatParam] (double v)
                    {
                        return static_cast<juce::AudioProcessorParameter*> (floatParam)->getText (floatParam->convertTo0to1 ((float) v), 32);
                    };
                    row.slider->updateText();
                }
                else if (floatParam->paramID == "smooth") // amp Smooth reads Off / -x.x dB, not 0..1
                {
                    row.slider->textFromValueFunction = [floatParam] (double v)
                    {
                        return static_cast<juce::AudioProcessorParameter*> (floatParam)->getText (floatParam->convertTo0to1 ((float) v), 32);
                    };
                    row.slider->updateText();
                }

                if (current->prefersFader (*floatParam))
                {
                    row.slider->setSliderStyle (juce::Slider::LinearVertical);
                    row.fader = true;
                }

                auto* rawSlider = row.slider.get();

                if (! isSelector)
                {
                    row.valueBox = std::make_unique<ValueFieldButton>();
                    row.valueBox->setButtonText (rawSlider->getTextFromValue (rawSlider->getValue()));
                    row.valueBox->onClick = [this, rawSlider] { showValueKeypadFor (rawSlider); };
                    knobGridHost.addAndMakeVisible (*row.valueBox);
                }

                auto* rawValueBox = row.valueBox.get();
                row.slider->onValueChange = [floatParam, rawSlider, rawValueBox]
                {
                    *floatParam = (float) rawSlider->getValue();
                    if (rawValueBox != nullptr)
                        rawValueBox->setButtonText (rawSlider->getTextFromValue (rawSlider->getValue()));
                };

                if (isSelector)
                {
                    // A real multi-position switch instead of a knob that happens to snap to a few steps (user
                    // request 2026-09-22). Options in ascending param-value order; onChange writes through the
                    // (hidden, never-shown) slider above, reusing its existing param-write lambda.
                    row.selector = std::make_unique<SelectorSwitch>();
                    juce::StringArray options;
                    const int steps = (int) std::lround ((range.end - range.start) / range.interval) + 1;
                    for (int step = 0; step < steps; ++step)
                    {
                        const float v = range.start + (float) step * range.interval;
                        options.add (static_cast<juce::AudioProcessorParameter*> (floatParam)->getText (floatParam->convertTo0to1 (v), 32));
                    }
                    row.selector->setOptions (options);
                    row.selector->setAccentColour (accent);
                    row.selector->setSelectedIndex ((int) std::lround ((floatParam->get() - range.start) / range.interval), juce::dontSendNotification);
                    const double selectorStart = (double) range.start, selectorInterval = (double) range.interval;
                    row.selector->onChange = [rawSlider, selectorStart, selectorInterval] (int index)
                    {
                        rawSlider->setValue (selectorStart + (double) index * selectorInterval);
                    };
                }

                for (auto& binding : current->getTempoSyncBindings())
                {
                    if (binding.param != floatParam)
                        continue;

                    row.syncToggle = std::make_unique<juce::TextButton>();
                    row.syncToggle->setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.35f));
                    row.syncToggle->onClick = [this, floatParam] { cycleTempoSync (floatParam); };
                    knobGridHost.addAndMakeVisible (*row.syncToggle);
                    break;
                }

                knobGridHost.addAndMakeVisible (*row.label);
                knobGridHost.addAndMakeVisible (*row.widget());
                sliders.push_back (std::move (row));
            }
        }
    }

    refreshLabelStyles();
    showPage (0);
    refresh();
    resized();

    // resized() alone doesn't repaint -- and when switching between two
    // processors whose drawer ends up the same pixel size (e.g. both have
    // one row of knobs), MainComponent::setBounds() on this panel is a
    // no-op (bounds unchanged), so nothing else triggers a repaint either.
    // Without this, the accent band in paint() kept showing the PREVIOUS
    // effect's colour after switching selection -- per user report
    // 2026-09-10 ("quando muda não tá preenchendo tudo").
    repaint();
}

void ParameterPanel::browseInstalledModels()
{
    if (current == nullptr)
        return;

    if (current->getModelSlotCount() > 1)
    {
        // Ask which slot before opening the browser at all -- a two-IR
        // processor has no single obvious target, unlike every existing
        // one-file processor this same button already serves.
        juce::PopupMenu menu;
        for (int i = 0; i < current->getModelSlotCount(); ++i)
            menu.addItem (i + 1, "Load into: " + current->getModelSlotName (i));

        menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
            [this] (int result)
            {
                if (result > 0)
                    browseInstalledModelsForSlot (result - 1);
            });
        return;
    }

    browseInstalledModelsForSlot (0);
}

void ParameterPanel::browseInstalledModelsForSlot (int slot)
{
    if (current == nullptr)
        return;

    const juce::String processorName (current->getName());

    // Every block only ever sees its OWN category subfolder -- a Neural
    // Pedal block's list can never show an amp capture, because it never
    // looks in the amps/ folder at all. BUG FIXED HERE: this directory used
    // to only be used if it already existed on disk -- never true before
    // the first download of that category, so every block silently fell
    // back to sharing one flat folder and categories looked unseparated.
    const auto subfolder = tone3000routing::subfolderForProcessorName (processorName);
    const auto folder = subfolder.isNotEmpty() ? modelsDir.getChildFile (subfolder) : modelsDir;
    folder.createDirectory();

    const auto wildcard = tone3000routing::fileWildcardForProcessorName (processorName);
    juce::Array<juce::File> files;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, wildcard))
        files.add (f);

    const auto dialogTitle = current->getModelSlotCount() > 1
        ? processorName + " -- " + current->getModelSlotName (slot) + " -- installed"
        : processorName + " -- installed";
    auto dialog = std::make_unique<ModelListDialog> (dialogTitle, files, folder, wildcard);

    dialog->onFileChosen = [this, slot] (juce::File file)
    {
        if (current == nullptr)
            return;

        try
        {
            current->loadModelFile (file, slot);
            refresh();
        }
        catch (const std::exception& e)
        {
            // OverlayHost card, not juce::AlertWindow -- the target device has no window
            // manager for a native popup (see OverlayHost / AGENTS.md's no-DialogWindow rule).
            if (onPushOverlay)
                onPushOverlay (std::make_unique<ChoiceCard> (
                    "Failed to load model", e.what(), juce::StringArray { "OK" },
                    [this] (int) { if (onPopOverlay) onPopOverlay(); }));
        }
    };
    dialog->onPopOverlay = [this] { if (onPopOverlay) onPopOverlay(); };

    if (tone3000 != nullptr)
        dialog->onSearchRequested = [this, slot] { openTone3000SearchForSlot (slot); };

    if (onPushOverlay)
        onPushOverlay (std::move (dialog));
}

void ParameterPanel::cycleTempoSync (juce::AudioParameterFloat* param)
{
    if (current == nullptr)
        return;

    const auto& bindings = current->getTempoSyncBindings();
    const auto it = std::find_if (bindings.begin(), bindings.end(),
        [param] (const TempoSyncBinding& b) { return b.param == param; });
    if (it == bindings.end())
        return;

    if (! it->synced)
    {
        current->setTempoSyncSubdivision (param, 0);
        current->setTempoSyncEnabled (param, true);
    }
    else if (it->subdivisionIndex + 1 < tempoSync::numSubdivisions)
    {
        current->setTempoSyncSubdivision (param, it->subdivisionIndex + 1);
    }
    else
    {
        current->setTempoSyncEnabled (param, false);
    }

    refresh();
}

void ParameterPanel::showValueKeypadFor (juce::Slider* knob)
{
    if (knob == nullptr)
        return;

    const auto it = std::find_if (sliders.begin(), sliders.end(),
        [knob] (const SliderRow& row) { return row.slider.get() == knob; });
    if (it == sliders.end() || it->param == nullptr)
        return;

    auto* param = it->param;
    auto* box = it->valueBox.get();

    if (onPushOverlay)
        onPushOverlay (std::make_unique<NumPadCard> (
            param->getName (100), knob->getTextFromValue (knob->getValue()),
            [this, param, box, knob] (juce::String typed)
            {
                // getValueForText parses through getFloatValue (tolerant
                // of the unit suffix getText appends) and returns the
                // NORMALISED value -- convertFrom0to1 lands it back on the
                // param's own units before operator= writes it.
                const double v = param->getNormalisableRange().convertFrom0to1 (
                    static_cast<juce::AudioProcessorParameter*> (param)->getValueForText (typed));
                *param = (float) v;
                if (box != nullptr)
                    box->setButtonText (knob->getTextFromValue ((double) param->get()));
                refresh();
                if (onPopOverlay) onPopOverlay();
            },
            [this] { if (onPopOverlay) onPopOverlay(); }));
}

void ParameterPanel::showMidiLearnMenu (juce::AudioParameterFloat* param)
{
    const int currentCc = getMidiCcForParam ? getMidiCcForParam (param) : -1;

    juce::PopupMenu menu;
    if (currentCc >= 0)
        menu.addItem (1, "Clear MIDI Mapping (CC " + juce::String (currentCc) + ")");
    else
        menu.addItem (1, "Learn MIDI CC...");

    menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
        [this, param] (int result)
        {
            if (result == 1 && onMidiLearnRequested)
                onMidiLearnRequested (param);
        });
}

void ParameterPanel::openTone3000Search()
{
    if (current == nullptr || tone3000 == nullptr)
        return;

    if (current->getModelSlotCount() > 1)
    {
        juce::PopupMenu menu;
        for (int i = 0; i < current->getModelSlotCount(); ++i)
            menu.addItem (i + 1, "Search for: " + current->getModelSlotName (i));

        menu.showMenuAsync (juce::PopupMenu::Options().withStandardItemHeight (touch::minTapTarget),
            [this] (int result)
            {
                if (result > 0)
                    openTone3000SearchForSlot (result - 1);
            });
        return;
    }

    openTone3000SearchForSlot (0);
}

void ParameterPanel::openTone3000SearchForSlot (int slot)
{
    if (current == nullptr || tone3000 == nullptr)
        return;

    const juce::String processorName (current->getName());
    const auto gearFilter = tone3000routing::gearFilterForProcessorName (processorName);
    const auto subfolder = tone3000routing::subfolderForProcessorName (processorName);
    const auto destinationFolder = subfolder.isNotEmpty() ? modelsDir.getChildFile (subfolder) : modelsDir;

    auto dialog = std::make_unique<Tone3000SearchDialog> (*tone3000, gearFilter, destinationFolder);

    dialog->onFileReady = [this, slot] (juce::File file)
    {
        if (current == nullptr)
            return;

        try
        {
            current->loadModelFile (file, slot);
            refresh();
        }
        catch (const std::exception& e)
        {
            // OverlayHost card, not juce::AlertWindow -- see the same handler in
            // browseInstalledModelsForSlot(). The search dialog stays open under
            // this card, so dismissing the error lands back on the result list.
            if (onPushOverlay)
                onPushOverlay (std::make_unique<ChoiceCard> (
                    "Failed to load model", e.what(), juce::StringArray { "OK" },
                    [this] (int) { if (onPopOverlay) onPopOverlay(); }));
        }
    };
    dialog->onPopOverlay = [this] { if (onPopOverlay) onPopOverlay(); };

    if (onPushOverlay)
        onPushOverlay (std::move (dialog));
}

std::vector<size_t> ParameterPanel::visibleSliders() const
{
    std::vector<size_t> shown;
    for (size_t i = 0; i < sliders.size(); ++i)
        if (sliders[i].page == currentPage && sliders[i].active)
            shown.push_back (i);
    return shown;
}

void ParameterPanel::showPage (int page)
{
    currentPage = juce::jlimit (0, juce::jmax (0, pageCount - 1), page);
    for (auto& row : sliders)
    {
        row.active = current == nullptr || row.param == nullptr || current->isParameterActive (*row.param);
        const bool shown = row.page == currentPage && row.active;
        row.widget()->setVisible (shown);
        row.label->setVisible (shown);
        if (row.valueBox != nullptr)
            row.valueBox->setVisible (shown); // a previous page's value boxes stayed on screen, unlabelled
        if (row.syncToggle != nullptr)
            row.syncToggle->setVisible (shown);
    }
    for (size_t i = 0; i < pageTabs.size(); ++i)
        pageTabs[i]->setToggleState ((int) i == currentPage, juce::dontSendNotification);
    resized();
    if (onPreferredHeightChanged)
        onPreferredHeightChanged();
}

int ParameterPanel::getPreferredContentHeight (int availableWidth) const
{
    if (current == nullptr)
        return 0;

    // Mirrors resized()'s own layout math -- see its comments for what each
    // number is. A few px of slack at the end guards against this ever
    // being exactly equal to the actual laid-out height: an exact match
    // is fragile (any future 1px rounding difference between the two
    // would silently bring the scrollbar back), a few spare px isn't.
    int height = 20;                    // getLocalBounds().reduced (10) -- top + bottom
    height += touch::minTapTarget;      // the one unified button row (title/bypass/browse/search/remove)
    height += 8;                        // gap before the accent band/status label
    height += 24;                       // status label
    height += 8;                        // gap before the knob grid

    const int knobAreaWidth = juce::jmax (knobCellWidth, availableWidth - 20);
    const int columns = juce::jmax (1, knobAreaWidth / knobCellWidth);

    // Per VISUAL row, not a flat rows*knobCellHeight -- a row containing
    // even one tempo-syncable knob needs the extra syncToggleHeight, and
    // different visual rows can mix syncable and non-syncable knobs.
    const auto shown = visibleSliders();
    for (size_t start = 0; start < shown.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (shown.size(), start + (size_t) columns);
        int rowHeight = knobCellHeight;
        for (size_t k = start; k < end; ++k)
            if (sliders[shown[k]].syncToggle != nullptr)
                rowHeight = juce::jmax (rowHeight, knobCellHeight + syncToggleHeight);
        height += rowHeight;
    }
    height += 6; // slack -- see above

    return height;
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    // Every button lives in ONE row, grouped together -- splitting
    // bypass/remove from browse/search across two separate rows didn't
    // read as one coherent toolbar, per user feedback.
    auto top = area.removeFromTop (touch::minTapTarget);
    removeButton.setBounds (top.removeFromRight (80));
    if (searchTone3000Button.isVisible())
    {
        top.removeFromRight (6);
        searchTone3000Button.setBounds (top.removeFromRight (170));
    }
    if (browseInstalledButton.isVisible())
    {
        top.removeFromRight (6);
        browseInstalledButton.setBounds (top.removeFromRight (170));
    }
    top.removeFromRight (6);
    bypassToggle.setBounds (top.removeFromRight (110));
    if (! pageTabs.empty())
    {
        // Numbered page tabs, right-aligned before the bypass toggle;
        // PageTabButton paints the active state itself.
        for (int i = (int) pageTabs.size() - 1; i >= 0; --i)
        {
            top.removeFromRight (4);
            pageTabs[(size_t) i]->setBounds (top.removeFromRight (touch::minTapTarget));
        }
        top.removeFromRight (6);
    }
    titleLabel.setBounds (top);

    pageTabsBounds = juce::Rectangle<int>();
    if (! pageTabs.empty())
    {
        juce::Rectangle<int> tabs;
        for (auto& tab : pageTabs)
            tabs = tabs.isEmpty() ? tab->getBounds() : tabs.getUnion (tab->getBounds());
        pageTabsBounds = tabs.withSizeKeepingCentre (tabs.getWidth() + 8, tabs.getHeight());
    }

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    knobViewport.setBounds (area);

    // A pedal-panel-style row of knobs, wrapping to a new row if the panel
    // is narrow. Laid out inside knobGridHost (not this panel directly) so
    // a processor with more knobs than fit in the drawer's capped height
    // scrolls instead of getting clipped off -- see ParameterPanel.h.
    const int hostWidth = juce::jmax (knobCellWidth, knobViewport.getWidth());
    const int columns = juce::jmax (1, hostWidth / knobCellWidth);

    int y = 0;

    const auto shown = visibleSliders();
    for (size_t start = 0; start < shown.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (shown.size(), start + (size_t) columns);

        // This visual row's height is the max over whichever knobs land in
        // it -- see getPreferredContentHeight()'s matching comment on why
        // this can't just be a flat knobCellHeight per row.
        int rowHeight = knobCellHeight;
        for (size_t k = start; k < end; ++k)
            if (sliders[shown[k]].syncToggle != nullptr)
                rowHeight = juce::jmax (rowHeight, knobCellHeight + syncToggleHeight);

        int x = 0;
        for (size_t k = start; k < end; ++k)
        {
            auto& row = sliders[shown[k]];
            const int labelY = row.valueBox != nullptr ? y + knobDiameter + 2
                                                       : y + knobCellHeight - knobLabelHeight;
            row.label->setBounds (x, labelY, knobCellWidth, knobLabelHeight);
            if (row.valueBox != nullptr)
            {
                if (row.fader) // a vertical fader keeps the knob's height, at the 48px touch width
                    row.widget()->setBounds (x + (knobCellWidth - touch::minTapTarget) / 2, y, touch::minTapTarget, knobDiameter);
                else
                    row.widget()->setBounds (x + (knobCellWidth - knobDiameter) / 2, y, knobDiameter, knobDiameter);
                row.valueBox->setBounds (x + (knobCellWidth - knobDiameter) / 2,
                                         y + knobDiameter + 2 + knobLabelHeight + 2, knobDiameter, knobValueHeight);
            }
            else
            {
                row.widget()->setBounds (x + (knobCellWidth - knobDiameter) / 2, y, knobDiameter,
                                         knobCellHeight - knobLabelHeight - 2);
            }
            if (row.syncToggle != nullptr)
                row.syncToggle->setBounds (x + (knobCellWidth - 90) / 2, y + knobCellHeight, 90, syncToggleHeight);
            x += knobCellWidth;
        }

        y += rowHeight;
    }

    knobGridHost.setSize (hostWidth, y);
}

void ParameterPanel::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const bool stageStyle = t.blockStyle == theme::BlockStyle::glow;
    const bool boldStyle = t.blockStyle == theme::BlockStyle::filled;

    if (stageStyle)
    {
        const float w = bounds.getWidth(), h = bounds.getHeight();
        const float radius = 24.0f, k = 0.552284f * radius;
        auto makePath = [&] (bool closed)
        {
            juce::Path path;
            path.startNewSubPath (0.0f, h);
            path.lineTo (0.0f, radius);
            path.cubicTo (0.0f, radius - k, radius - k, 0.0f, radius, 0.0f);
            path.lineTo (w - radius, 0.0f);
            path.cubicTo (w - radius + k, 0.0f, w, radius - k, w, radius);
            path.lineTo (w, h);
            if (closed)
            {
                path.lineTo (0.0f, h);
                path.closeSubPath();
            }
            return path;
        };

        const auto fillPath = makePath (true);
        const auto borderPath = makePath (false);
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.55f), 30, { 0, -15 });
        shadow.drawForPath (g, fillPath);
        g.setGradientFill (juce::ColourGradient (t.raised.withMultipliedBrightness (0.92f), 0.0f, 0.0f,
                                                 t.surface.withMultipliedBrightness (0.95f), 0.0f, h, false));
        g.fillPath (fillPath);
        g.setColour (t.lineStrong);
        g.strokePath (borderPath, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::butt));
    }
    else
    {
        g.fillRect (bounds.toNearestInt());
        if (boldStyle)
        {
            g.setColour (effectAccent.isTransparent() ? t.raisedHigh : effectAccent);
            g.fillRect (0.0f, 0.0f, bounds.getWidth(), 4.0f);
        }
        else
        {
            g.setColour (t.lineStrong);
            g.fillRect (0.0f, 0.0f, bounds.getWidth(), 1.0f);
        }
    }

    if (! pageTabsBounds.isEmpty())
    {
        const auto tabs = pageTabsBounds.toFloat();
        g.setColour (t.raised);
        g.fillRoundedRectangle (tabs, t.smallRadius);
        g.setColour (t.line);
        g.drawRoundedRectangle (tabs, t.smallRadius, 1.0f);
    }

    const auto handle = juce::Rectangle<float> (bounds.getCentreX() - 27.0f, 8.0f, 54.0f, 5.0f);
    g.setColour (t.lineStrong);
    g.fillRoundedRectangle (handle, 3.0f);
}

} // namespace openguitarmultifx
