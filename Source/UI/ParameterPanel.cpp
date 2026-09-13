#include "ParameterPanel.h"

#include "ModelListDialog.h"
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

    // Extra row height ONLY for a knob whose param registered for tempo
    // sync (see EffectProcessor::registerTempoSyncParam()) -- most knobs
    // don't have this, so cell height is computed per VISUAL grid row (the
    // max over whichever knobs land in it), not a single global constant,
    // to avoid reintroducing the "wasted space under every knob" MIDI
    // Learn was corrected for (this toggle is deliberately visible by
    // request, but only on the knobs that actually have it).
    constexpr int syncToggleHeight = 22;
}

ParameterPanel::ParameterPanel()
{
    addAndMakeVisible (titleLabel);
    titleLabel.setFont (juce::Font (22.0f, juce::Font::bold));

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::Font (15.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

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
    for (auto& row : sliders)
    {
        knobGridHost.removeChildComponent (row.slider.get());
        knobGridHost.removeChildComponent (row.label.get());
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

    // The drawer's buttons, knobs, and the band below the header all pick up
    // the selected effect's own accent colour (EffectBlockComponent already
    // outlines the block itself in this colour) so the whole drawer reads
    // as "this effect's controls" rather than generic chrome -- per user
    // request 2026-09-10. The title stays plain white (an earlier version
    // coloured the title text instead of the band -- corrected per
    // follow-up feedback the same day: the band reads better than coloured
    // text at this size, and the knob-area background behind the knobs
    // stays the ordinary panel grey, only the header/knob strip is tinted).
    const auto accent = current->getAccentColour();
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    bypassToggle.setColour (juce::ToggleButton::tickColourId, accent);
    for (auto* b : { &browseInstalledButton, &searchTone3000Button, &removeButton })
        b->setColour (OpenGuitarMultiFxLookAndFeel::accentColourId, accent);

    if (auto* group = current->getParameters())
    {
        for (auto* p : group->getParameters (true))
        {
            if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*> (p))
            {
                SliderRow row;
                row.param = floatParam;
                row.slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                               juce::Slider::TextBoxBelow);
                row.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 26);
                row.slider->setColour (juce::Slider::rotarySliderFillColourId, accent);
                row.slider->setColour (juce::Slider::thumbColourId, accent);

                row.label = std::make_unique<LongPressLabel>();
                row.label->setText (floatParam->getName (64), juce::dontSendNotification);
                row.label->setJustificationType (juce::Justification::centred);
                row.label->setFont (14.0f);
                row.label->onLongPress = [this, floatParam] { showMidiLearnMenu (floatParam); };

                const auto range = floatParam->getNormalisableRange();
                row.slider->setRange (range.start, range.end, range.interval > 0.0f ? range.interval : 0.01);
                row.slider->setValue (floatParam->get(), juce::dontSendNotification);

                auto* rawSlider = row.slider.get();
                row.slider->onValueChange = [floatParam, rawSlider]
                {
                    *floatParam = (float) rawSlider->getValue();
                };

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
                knobGridHost.addAndMakeVisible (*row.slider);
                sliders.push_back (std::move (row));
            }
        }
    }

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
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Failed to load model", e.what());
        }
    };
    dialog->onPopOverlay = [this] { if (onPopOverlay) onPopOverlay(); };

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
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon, "Failed to load model", e.what());
        }
    };
    dialog->onPopOverlay = [this] { if (onPopOverlay) onPopOverlay(); };

    if (onPushOverlay)
        onPushOverlay (std::move (dialog));
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
    for (size_t start = 0; start < sliders.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (sliders.size(), start + (size_t) columns);
        int rowHeight = knobCellHeight;
        for (size_t idx = start; idx < end; ++idx)
            if (sliders[idx].syncToggle != nullptr)
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
    titleLabel.setBounds (top);

    // Gap before the accent band -- without it the header buttons sat flush
    // against the band's top edge, per user feedback 2026-09-10.
    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    knobViewport.setBounds (area);

    // Full-width band covering the status-label strip and the gap after
    // it -- everything between the header row and where the knob grid
    // starts -- painted in the effect's accent colour by paint(). Not
    // inset to the panel's 10px margin (unlike everything above), so it
    // reads as a solid banner edge-to-edge rather than an inset chip.
    accentBandBounds = { 0, statusLabel.getY(), getWidth(), knobViewport.getY() - statusLabel.getY() };

    // A pedal-panel-style row of knobs, wrapping to a new row if the panel
    // is narrow. Laid out inside knobGridHost (not this panel directly) so
    // a processor with more knobs than fit in the drawer's capped height
    // scrolls instead of getting clipped off -- see ParameterPanel.h.
    const int hostWidth = juce::jmax (knobCellWidth, knobViewport.getWidth());
    const int columns = juce::jmax (1, hostWidth / knobCellWidth);

    int y = 0;

    for (size_t start = 0; start < sliders.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (sliders.size(), start + (size_t) columns);

        // This visual row's height is the max over whichever knobs land in
        // it -- see getPreferredContentHeight()'s matching comment on why
        // this can't just be a flat knobCellHeight per row.
        int rowHeight = knobCellHeight;
        for (size_t idx = start; idx < end; ++idx)
            if (sliders[idx].syncToggle != nullptr)
                rowHeight = juce::jmax (rowHeight, knobCellHeight + syncToggleHeight);

        int x = 0;
        for (size_t idx = start; idx < end; ++idx)
        {
            auto& row = sliders[idx];
            row.label->setBounds (x, y, knobCellWidth, 22);
            row.slider->setBounds (x + (knobCellWidth - knobDiameter) / 2, y + 22, knobDiameter, knobDiameter + 28);
            if (row.syncToggle != nullptr)
                row.syncToggle->setBounds (x + (knobCellWidth - 72) / 2, y + 22 + knobDiameter + 28, 72, syncToggleHeight);
            x += knobCellWidth;
        }

        y += rowHeight;
    }

    knobGridHost.setSize (hostWidth, y);
}

void ParameterPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    // The band between the header and the knob grid reads as "this effect's
    // controls" -- see accentBandBounds's comment in the header. Everything
    // below it (the knob area itself) stays the plain panel grey above, per
    // user request 2026-09-10: colouring the band, not the knob background.
    if (current != nullptr)
    {
        // More saturated/vivid than the raw accent (which is tuned to read
        // well as a thin block OUTLINE against black, not as a fill this
        // large) -- per user feedback 2026-09-10 ("mais acentuado").
        g.setColour (current->getAccentColour().withMultipliedSaturation (1.5f).withMultipliedBrightness (1.1f));
        g.fillRect (accentBandBounds);
    }
}

} // namespace openguitarmultifx
