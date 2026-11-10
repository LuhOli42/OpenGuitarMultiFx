#include "ParameterPanel.h"

#include "Theme.h"
#include "ModelListDialog.h"
#include "PromptCards.h"
#include "OpenGuitarMultiFxLookAndFeel.h"
#include "Tone3000SearchDialog.h"
#include "TouchSizing.h"
#include "../EffectRegistry.h"
#include "../Effects/IconKit.h"
#include "../Tone3000/GearRouting.h"

#include <IconData.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace openguitarmultifx
{

namespace
{
    // Prototype docs/ui/prototype.html .drw/.dh/.kn/.kv/.selst, with every hit target kept at
    // touch::minTapTarget (48px) even where the prototype's visual is shorter.
    constexpr int drawerHeaderHeight = 72;
    constexpr int drawerHandleY = 8;
    constexpr int drawerControlY = 23;
    constexpr int drawerControlHeight = 48; // touch::minTapTarget
    constexpr int drawerSidePadding = 18;
    constexpr int drawerIconSize = 50;
    constexpr int drawerGap = 12;

    constexpr int knobCellWidth = 120;
    constexpr int knobColumnGap = 26;
    constexpr int knobPitch = knobCellWidth + knobColumnGap; // 146
    constexpr int knobDiameter = 110;
    constexpr int knobLabelHeight = 18;
    constexpr int knobValueHitHeight = 48;  // .kv is 36px visually, centred in the 48px target
    constexpr int knobLabelGap = 6;
    constexpr int knobContentHeight = knobDiameter + knobLabelGap + knobLabelHeight
                                   + knobLabelGap + knobValueHitHeight; // 188
    constexpr int selectorLabelGap = 6;
    constexpr int syncToggleHeight = 48; // touch::minTapTarget
    constexpr int syncToggleGap = 6;
    constexpr int knobRowGap = 26;
    constexpr int minDrawerHeight = 330; // prototype .drw height

    constexpr int accentColourId = 0x4f475543; // arbitrary private ColourId for the drawer accent

    juce::Colour liftAccent (juce::Colour accent)
    {
        if (accent.getPerceivedBrightness() >= 0.45f)
            return accent;
        return accent.withBrightness (juce::jmax (0.8f, accent.getBrightness()))
                    .withSaturation (juce::jmin (0.6f, accent.getSaturation()));
    }

    int pillWidthFor (const juce::String& text, bool hasGlyph)
    {
        if (text.isEmpty())
            return 50; // prototype icon-only pill: 16px padding + 18px glyph + 16px padding

        const float textWidth = juce::GlyphArrangement::getStringWidth (juce::Font (13.0f), text);
        return (int) std::ceil (textWidth + (hasGlyph ? 26.0f : 0.0f) + 32.0f);
    }

    juce::Point<float> knobPoint (juce::Point<float> centre, float radius, float angle)
    {
        const float a = angle - juce::MathConstants<float>::halfPi;
        return centre + juce::Point<float> ((float) (radius * std::cos (a)),
                                            (float) (radius * std::sin (a)));
    }

    juce::Path knobArc (juce::Point<float> centre, float radius, float startAngle, float endAngle)
    {
        juce::Path path;
        const float sweep = endAngle - startAngle;
        const int steps = juce::jmax (2, (int) std::lround (std::abs (sweep) / 0.08f));
        path.startNewSubPath (knobPoint (centre, radius, startAngle));
        for (int i = 1; i <= steps; ++i)
            path.lineTo (knobPoint (centre, radius, startAngle + sweep * (float) i / (float) steps));
        return path;
    }

    void strokeKnobArc (juce::Graphics& g, juce::Point<float> centre, float radius,
                        float startAngle, float endAngle, juce::Colour colour, float width)
    {
        juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (colour);
        g.strokePath (knobArc (centre, radius, startAngle, endAngle), stroke);
    }

    void drawGlyph (juce::Graphics& g, ParameterPanel::PillButton::Glyph glyph,
                    juce::Rectangle<float> bounds, juce::Colour colour)
    {
        g.setColour (colour);
        const float x = bounds.getX(), y = bounds.getY(), w = bounds.getWidth(), h = bounds.getHeight();
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;

        switch (glyph)
        {
            case ParameterPanel::PillButton::Glyph::switchModel:
            {
                // A simple swap glyph: two arrows chasing each other.
                g.drawLine (x + 2.0f, cy, x + w - 3.0f, cy, 1.6f);
                g.drawLine (x + w - 3.0f, cy, x + w - 7.0f, cy - 3.0f, 1.6f);
                g.drawLine (x + w - 3.0f, cy, x + w - 7.0f, cy + 3.0f, 1.6f);
                g.drawLine (x + w - 2.0f, cy + h * 0.45f, x + 3.0f, cy + h * 0.45f, 1.6f);
                g.drawLine (x + 3.0f, cy + h * 0.45f, x + 7.0f, cy + h * 0.45f - 3.0f, 1.6f);
                g.drawLine (x + 3.0f, cy + h * 0.45f, x + 7.0f, cy + h * 0.45f + 3.0f, 1.6f);
                break;
            }
            case ParameterPanel::PillButton::Glyph::midi:
            {
                g.drawEllipse (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.5f);
                g.drawEllipse (x + 1.0f, cy - 3.0f, 6.0f, 6.0f, 1.4f);
                g.drawEllipse (x + w - 7.0f, cy - 3.0f, 6.0f, 6.0f, 1.4f);
                g.drawLine (cx + 5.0f, cy - 1.0f, x + w - 7.0f, cy - 1.0f, 1.2f);
                break;
            }
            case ParameterPanel::PillButton::Glyph::cloud:
            {
                juce::Path cloud;
                cloud.startNewSubPath (x + 2.0f, cy + 2.0f);
                cloud.cubicTo (x - 1.0f, cy - 2.0f, x + 5.0f, y + 1.0f, cx - 1.0f, y + 3.0f);
                cloud.cubicTo (cx - 5.0f, y - 1.0f, cx + 2.0f, y - 4.0f, cx + 5.0f, y + 1.0f);
                cloud.cubicTo (x + w - 5.0f, y + 1.0f, x + w + 1.0f, cy - 2.0f, x + w - 2.0f, cy + 2.0f);
                cloud.lineTo (x + w - 2.0f, cy + h * 0.35f);
                cloud.cubicTo (x + w * 0.65f, y + h - 2.0f, x + w * 0.35f, y + h - 2.0f, x + 2.0f, cy + h * 0.35f);
                cloud.closeSubPath();
                juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
                g.strokePath (cloud, stroke);
                break;
            }
            case ParameterPanel::PillButton::Glyph::trash:
            {
                const auto body = juce::Rectangle<float> (x + w * 0.32f, y + h * 0.32f, w * 0.36f, h * 0.52f);
                g.drawRect (body, 1.6f);
                g.drawLine (x + w * 0.28f, y + h * 0.26f, x + w * 0.72f, y + h * 0.26f, 1.6f);
                g.drawLine (cx, y + h * 0.20f, cx, y + h * 0.28f, 1.6f);
                g.drawLine (body.getX() + 3.0f, body.getY() + 4.0f, body.getRight() - 3.0f, body.getY() + 4.0f, 1.2f);
                g.drawLine (body.getX() + 3.0f, body.getY() + 8.0f, body.getRight() - 3.0f, body.getY() + 8.0f, 1.2f);
                break;
            }
            case ParameterPanel::PillButton::Glyph::close:
            {
                g.drawLine (x + 4.0f, y + 4.0f, x + w - 4.0f, y + h - 4.0f, 2.0f);
                g.drawLine (x + w - 4.0f, y + 4.0f, x + 4.0f, y + h - 4.0f, 2.0f);
                break;
            }
            case ParameterPanel::PillButton::Glyph::none:
                break;
        }
    }
}

ParameterPanel::ParameterPanel()
{
    setPaintingIsUnclipped (true);

    addAndMakeVisible (headerIcon);
    addAndMakeVisible (titleLabel);
    addAndMakeVisible (subtitleLabel);
    titleLabel.setFont (juce::Font (21.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, theme::current().text);
    subtitleLabel.setFont (juce::Font (11.0f));
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    subtitleLabel.setColour (juce::Label::textColourId, theme::current().textMuted);

    addAndMakeVisible (bypassSwitch);
    bypassSwitch.onClick = [this]
    {
        if (current == nullptr)
            return;
        const bool nowOn = ! bypassSwitch.isOn();
        bypassSwitch.setOn (nowOn);
        current->setBypassed (! nowOn);
    };

    addAndMakeVisible (modelButton);
    modelButton.onClick = [this] { browseInstalledModels(); };

    addAndMakeVisible (midiButton);
    midiButton.onClick = [this] { showMidiLearnMenuForProcessor(); };

    addAndMakeVisible (tone3000Button);
    tone3000Button.onClick = [this] { openTone3000Search(); };

    addAndMakeVisible (removeButton);
    removeButton.onClick = [this] { if (current != nullptr && onRemoveRequested) onRemoveRequested (current); };

    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };

    addAndMakeVisible (knobViewport);
    knobViewport.setViewedComponent (&knobGridHost, false);
    knobViewport.setScrollBarsShown (true, false);

    rebuildForCurrentProcessor();
}

void ParameterPanel::lookAndFeelChanged()
{
    const auto& t = theme::current();
    titleLabel.setColour (juce::Label::textColourId, t.text);
    subtitleLabel.setColour (juce::Label::textColourId, t.textMuted);
    refreshLabelStyles();
    repaint();
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
        row.label->setJustificationType (juce::Justification::centred);
    }
}

void ParameterPanel::setProcessor (EffectProcessor* processorToEdit)
{
    current = processorToEdit;
    rebuildForCurrentProcessor();
}

void ParameterPanel::updateSubtitle()
{
    juce::String text = subtitleBase;
    if (current != nullptr)
    {
        const auto status = current->getStatusText();
        if (status.isNotEmpty())
            text += (text.isEmpty() ? juce::String() : juce::String (" - ")) + status;
    }
    subtitleLabel.setText (text, juce::dontSendNotification);
}

void ParameterPanel::refresh()
{
    if (current != nullptr)
    {
        updateSubtitle();
        bypassSwitch.setOn (! current->isBypassed());

        for (const auto& row : sliders)
            if (row.param != nullptr && current->isParameterActive (*row.param) != row.active)
            {
                showPage (currentPage);
                break;
            }

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
    pageLabels.clear();

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
        titleLabel.setText ({}, juce::dontSendNotification);
        subtitleLabel.setText ({}, juce::dontSendNotification);
        subtitleBase.clear();
        headerIcon.setProcessor (nullptr);
        bypassSwitch.setVisible (false);
        modelButton.setVisible (false);
        midiButton.setVisible (false);
        tone3000Button.setVisible (false);
        removeButton.setVisible (false);
        closeButton.setVisible (false);
        knobViewport.setVisible (false);
        resized();
        return;
    }

    titleLabel.setText (current->getName(), juce::dontSendNotification);
    headerIcon.setProcessor (current);
    knobViewport.setVisible (true);

    juce::String registryKey;
    EffectRegistry::Category category = EffectRegistry::Category::utility;
    bool haveRegistryCategory = false;
    if (effectRegistry != nullptr)
    {
        registryKey = effectRegistry->keyForDisplayName (current->getName());
        if (registryKey.isNotEmpty())
        {
            category = effectRegistry->categoryForKey (registryKey);
            haveRegistryCategory = true;
        }
    }

    const auto rawAccent = haveRegistryCategory ? EffectRegistry::categoryColour (category)
                                                : current->getAccentColour();
    effectAccent = liftAccent (rawAccent.withAlpha (1.0f));
    headerIcon.setColour (accentColourId, effectAccent);
    bypassSwitch.setColour (accentColourId, effectAccent);

    juce::String subtitle;
    if (haveRegistryCategory)
    {
        juce::StringArray parts;
        parts.add (EffectRegistry::categoryDisplayName (category));
        const auto subcategory = effectRegistry->subcategoryForKey (registryKey);
        if (subcategory.isNotEmpty())
            parts.add (subcategory);
        if (blockLocationProvider)
        {
            const auto location = blockLocationProvider (current);
            if (location.isNotEmpty())
                parts.add (location);
        }
        subtitle = parts.joinIntoString (" · ");
    }
    subtitleBase = subtitle;
    updateSubtitle();

    const bool wantsModelFile = current->wantsModelFile();
    modelButton.setVisible (wantsModelFile);
    tone3000Button.setVisible (wantsModelFile && tone3000 != nullptr);
    midiButton.setVisible (true);
    removeButton.setVisible (true);
    closeButton.setVisible (true);
    bypassSwitch.setVisible (true);
    bypassSwitch.setOn (! current->isBypassed());

    const auto pages = current->getParameterPages();
    pageCount = (int) pages.size();
    currentPage = 0;

    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        pageLabels.push_back ([&]
        {
            if (pageCount <= 1)
                return juce::String ("Main");
            if (haveRegistryCategory && category == EffectRegistry::Category::amps)
            {
                if (pageIndex == 0)
                    return juce::String ("Amp");
                if (pageIndex == 1)
                    return juce::String ("Power");
                bool hasCab = false;
                for (auto* p : pages[(size_t) pageIndex])
                    if (juce::String (p->paramID).endsWith ("_cab"))
                        hasCab = true;
                return hasCab ? juce::String ("Cab") : juce::String ("Page ") + juce::String (pageIndex + 1);
            }
            return pageIndex == 0 ? juce::String ("Main") : juce::String ("Page ") + juce::String (pageIndex + 1);
        }());

    if (pageCount > 1)
    {
        for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            auto tab = std::make_unique<PageTabButton> (pageLabels[(size_t) pageIndex]);
            tab->setClickingTogglesState (true);
            tab->setToggleState (pageIndex == 0, juce::dontSendNotification);
            tab->setColour (accentColourId, effectAccent);
            tab->onClick = [this, pageIndex] { showPage (pageIndex); };
            addChildComponent (*tab);
            tab->setVisible (true);
            pageTabs.push_back (std::move (tab));
        }
    }

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
            row.slider->setColour (accentColourId, effectAccent);
            row.slider->setColour (juce::Slider::rotarySliderFillColourId, effectAccent);
            row.slider->setColour (juce::Slider::thumbColourId, effectAccent);

            row.label = std::make_unique<LongPressLabel>();
            row.label->setJustificationType (juce::Justification::centred);
            row.label->onLongPress = [this, floatParam] { showMidiLearnMenu (floatParam); };

            const auto range = floatParam->getNormalisableRange();
            row.slider->setRange (range.start, range.end, range.interval > 0.0f ? range.interval : 0.01);
            row.slider->setValue (floatParam->get(), juce::dontSendNotification);
            const bool isSelector = range.interval >= 1.0f && (range.end - range.start) / range.interval <= 8.0f
                                     && ! current->prefersSteppedKnob (*floatParam);
            if (! isSelector)
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
            else if (floatParam->paramID == "smooth")
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
                row.selector = std::make_unique<SelectorSwitch>();
                juce::StringArray options;
                const int steps = (int) std::lround ((range.end - range.start) / range.interval) + 1;
                row.selectorSteps = steps;
                for (int step = 0; step < steps; ++step)
                {
                    const float v = range.start + (float) step * range.interval;
                    options.add (static_cast<juce::AudioProcessorParameter*> (floatParam)->getText (floatParam->convertTo0to1 (v), 32));
                }
                row.selector->setOptions (options);
                row.selector->setAccentColour (effectAccent);
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
                row.syncToggle->setColour (juce::TextButton::buttonColourId, effectAccent.withAlpha (0.35f));
                row.syncToggle->onClick = [this, floatParam] { cycleTempoSync (floatParam); };
                knobGridHost.addAndMakeVisible (*row.syncToggle);
                break;
            }

            knobGridHost.addAndMakeVisible (*row.label);
            knobGridHost.addAndMakeVisible (*row.widget());
            sliders.push_back (std::move (row));
        }
    }

    refreshLabelStyles();
    showPage (0);
    refresh();
    resized();
    repaint();
}

void ParameterPanel::browseInstalledModels()
{
    if (current == nullptr)
        return;

    if (current->getModelSlotCount() > 1)
    {
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

void ParameterPanel::showMidiLearnMenuForProcessor()
{
    if (current == nullptr || ! onMidiLearnRequested)
        return;

    std::vector<juce::AudioParameterFloat*> params;
    for (const auto& page : current->getParameterPages())
        for (auto* param : page)
            params.push_back (param);
    if (params.empty())
        return;

    juce::PopupMenu menu;
    for (size_t i = 0; i < params.size(); ++i)
    {
        const int cc = getMidiCcForParam ? getMidiCcForParam (params[i]) : -1;
        const auto name = params[i]->getName (48);
        menu.addItem ((int) i + 1, cc >= 0 ? "CC " + juce::String (cc) + " - " + name : name);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton)
                                              .withStandardItemHeight (touch::minTapTarget),
        [this, params] (int result)
        {
            if (result > 0 && result <= (int) params.size())
                showMidiLearnMenu (params[(size_t) result - 1]);
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

int ParameterPanel::contentHeightForRow (const SliderRow& row) const
{
    if (row.selector != nullptr)
        return row.selector->getPreferredHeight() + selectorLabelGap + knobLabelHeight;
    return knobContentHeight;
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
            row.valueBox->setVisible (shown);
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

    const int hostWidth = juce::jmax (knobPitch, availableWidth);
    const int columns = juce::jmax (1, hostWidth / knobPitch);
    const auto shown = visibleSliders();

    int contentHeight = 0;
    int rowCount = 0;
    for (size_t start = 0; start < shown.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (shown.size(), start + (size_t) columns);
        int rowHeight = 0;
        for (size_t k = start; k < end; ++k)
        {
            const auto& row = sliders[shown[k]];
            int h = contentHeightForRow (row);
            if (row.syncToggle != nullptr)
                h += syncToggleGap + syncToggleHeight;
            rowHeight = juce::jmax (rowHeight, h);
        }
        contentHeight += rowHeight;
        ++rowCount;
    }
    if (rowCount > 1)
        contentHeight += knobRowGap * (rowCount - 1);

    return juce::jmax (minDrawerHeight, drawerHeaderHeight + contentHeight);
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds();

    std::vector<int> tabWidths;
    int tabsWidth = 0;
    for (auto& tab : pageTabs)
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidth (juce::Font (13.0f, juce::Font::bold), tab->getButtonText());
        const int width = juce::jmax (touch::minTapTarget, (int) std::lround (textWidth) + 32);
        tabWidths.push_back (width);
        tabsWidth += width;
    }
    if (! tabWidths.empty())
        tabsWidth += 4 * ((int) tabWidths.size() - 1);

    const int headerHeight = juce::jmin (drawerHeaderHeight, area.getHeight());
    auto header = area.removeFromTop (headerHeight);
    const int controlY = juce::jmin (drawerControlY, juce::jmax (0, headerHeight - drawerControlHeight));

    int cursor = header.getRight() - drawerSidePadding;
    auto placeRight = [&] (juce::Component& component, int width)
    {
        if (! component.isVisible() || width <= 0)
            return;
        cursor -= width;
        component.setBounds (cursor, controlY, width, drawerControlHeight);
        cursor -= drawerGap;
    };

    placeRight (closeButton, pillWidthFor (closeButton.getButtonText(), true));
    placeRight (removeButton, pillWidthFor (removeButton.getButtonText(), true));
    placeRight (tone3000Button, pillWidthFor (tone3000Button.getButtonText(), true));
    placeRight (midiButton, pillWidthFor (midiButton.getButtonText(), true));
    placeRight (bypassSwitch, 104);

    if (! pageTabs.empty())
    {
        cursor -= tabsWidth;
        int tabX = cursor;
        for (size_t i = 0; i < pageTabs.size(); ++i)
        {
            pageTabs[i]->setBounds (tabX, controlY, tabWidths[i], drawerControlHeight);
            tabX += tabWidths[i] + 4;
        }
        cursor -= drawerGap;
    }

    const int rightContentLeft = juce::jmax (drawerSidePadding + 62, cursor);
    const int modelWidth = modelButton.isVisible() ? pillWidthFor (modelButton.getButtonText(), true) : 0;
    const int left = drawerSidePadding;
    headerIcon.setBounds (left, 21, drawerIconSize, drawerIconSize);

    const int textX = left + drawerIconSize + drawerGap;
    const int textWidth = juce::jmax (80, rightContentLeft - textX - (modelWidth > 0 ? modelWidth + drawerGap : 0) - drawerGap);
    titleLabel.setBounds (textX, 24, textWidth, 26);
    subtitleLabel.setBounds (textX, 50, textWidth, 18);

    if (modelButton.isVisible())
    {
        const int modelX = textX + textWidth + drawerGap;
        modelButton.setBounds (juce::jmin (modelX, juce::jmax (textX, rightContentLeft - modelWidth)),
                               controlY, modelWidth, drawerControlHeight);
    }

    pageTabsBounds = juce::Rectangle<int>();
    if (! pageTabs.empty())
    {
        juce::Rectangle<int> tabs;
        for (auto& tab : pageTabs)
            tabs = tabs.isEmpty() ? tab->getBounds() : tabs.getUnion (tab->getBounds());
        pageTabsBounds = tabs;
    }

    knobViewport.setBounds (area);

    const int hostWidth = juce::jmax (knobPitch, knobViewport.getWidth());
    const int columns = juce::jmax (1, hostWidth / knobPitch);
    const auto shown = visibleSliders();

    std::vector<std::pair<size_t, size_t>> rows;
    std::vector<int> rowHeights;
    int contentHeight = 0;
    for (size_t start = 0; start < shown.size(); start += (size_t) columns)
    {
        const size_t end = juce::jmin (shown.size(), start + (size_t) columns);
        rows.emplace_back (start, end);
        int rowHeight = 0;
        for (size_t k = start; k < end; ++k)
        {
            const auto& row = sliders[shown[k]];
            int h = contentHeightForRow (row);
            if (row.syncToggle != nullptr)
                h += syncToggleGap + syncToggleHeight;
            rowHeight = juce::jmax (rowHeight, h);
        }
        rowHeights.push_back (rowHeight);
        contentHeight += rowHeight;
    }
    if (rows.size() > 1)
        contentHeight += knobRowGap * (int) (rows.size() - 1);

    const int viewportHeight = juce::jmax (0, knobViewport.getHeight());
    const int hostHeight = juce::jmax (viewportHeight, contentHeight);
    const int startY = contentHeight <= viewportHeight ? (hostHeight - contentHeight) / 2 : 0;
    knobGridHost.setSize (hostWidth, hostHeight);

    int y = startY;
    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        const auto [start, end] = rows[rowIndex];
        const int count = (int) (end - start);
        const int rowWidth = count * knobCellWidth + (count - 1) * knobColumnGap;
        int x = juce::jmax (0, (hostWidth - rowWidth) / 2);

        for (size_t k = start; k < end; ++k)
        {
            auto& row = sliders[shown[k]];
            const int cellContentHeight = contentHeightForRow (row);

            if (row.selector != nullptr)
            {
                const int selectorHeight = row.selector->getPreferredHeight();
                row.widget()->setBounds (x + (knobCellWidth - SelectorSwitch::optionWidth) / 2,
                                         y, SelectorSwitch::optionWidth, selectorHeight);
                row.label->setBounds (x, y + selectorHeight + selectorLabelGap, knobCellWidth, knobLabelHeight);
            }
            else
            {
                if (row.fader)
                    row.widget()->setBounds (x + (knobCellWidth - touch::minTapTarget) / 2,
                                             y, touch::minTapTarget, knobDiameter);
                else
                    row.widget()->setBounds (x + (knobCellWidth - knobDiameter) / 2,
                                             y, knobDiameter, knobDiameter);
                row.label->setBounds (x, y + knobDiameter + knobLabelGap, knobCellWidth, knobLabelHeight);
                if (row.valueBox != nullptr)
                    row.valueBox->setBounds (x + (knobCellWidth - 92) / 2,
                                             y + knobDiameter + knobLabelGap + knobLabelHeight + knobLabelGap,
                                             92, knobValueHitHeight);
            }

            if (row.syncToggle != nullptr)
                row.syncToggle->setBounds (x, y + cellContentHeight + syncToggleGap,
                                           knobCellWidth, syncToggleHeight);

            x += knobPitch;
        }

        y += rowHeights[rowIndex] + (rowIndex + 1 < rows.size() ? knobRowGap : 0);
    }
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
        juce::DropShadow shadow (juce::Colours::black.withAlpha (0.55f), 30, { 0, -30 });
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
        g.setColour (t.surface);
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

    const auto handle = juce::Rectangle<float> (bounds.getCentreX() - 27.0f, (float) drawerHandleY, 54.0f, 5.0f);
    g.setColour (t.lineStrong);
    g.fillRoundedRectangle (handle, 3.0f);
}

void ParameterPanel::HeaderIcon::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    if (processor == nullptr)
        return;

    juce::AudioParameterFloat* cabParam = nullptr;
    if (auto* params = processor->getParameters())
        for (auto* p : params->getParameters (true))
            if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (p);
                f != nullptr && juce::String (f->paramID).endsWith ("_cab"))
                cabParam = f;
    const bool headOnly = cabParam != nullptr && cabParam->get() < 0.5f;

    const auto accent = findColour (accentColourId).isTransparent()
                            ? processor->getAccentColour()
                            : findColour (accentColourId);
    const bool boldStyle = t.blockStyle == theme::BlockStyle::filled;

    if (boldStyle)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (bounds, t.smallRadius);
    }
    else
    {
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds, t.smallRadius);
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);
    }

    // Effect icons are white SVG strokes; render once and tint the pixels so the glyph reads in the
    // drawer's category colour (the same trick EffectBlockComponent uses for the chain tiles).
    const int renderSize = 100;
    auto img = juce::Image (juce::Image::ARGB, renderSize, renderSize, true);
    {
        juce::Graphics ig (img);
        ig.setColour (juce::Colours::white);
        if (headOnly)
        {
            static const std::unique_ptr<juce::Drawable> headIcon =
                icon::loadSvg (IconData::amp_head_svg, IconData::amp_head_svgSize);
            icon::drawSvg (ig, juce::Rectangle<float> (0.0f, 0.0f, (float) renderSize, (float) renderSize), headIcon.get());
        }
        else
        {
            processor->drawIcon (ig, juce::Rectangle<float> (0.0f, 0.0f, (float) renderSize, (float) renderSize));
        }
    }

    const auto tint = boldStyle ? accent.contrasting (t.background, t.text) : accent;
    for (int y = 0; y < renderSize; ++y)
        for (int x = 0; x < renderSize; ++x)
        {
            const auto px = img.getPixelAt (x, y);
            const int alpha = px.getAlpha();
            if (alpha > 0)
                img.setPixelAt (x, y, tint.withMultipliedAlpha ((float) alpha / 255.0f));
        }

    g.drawImageWithin (img, (int) bounds.getX(), (int) bounds.getY(),
                       (int) bounds.getWidth(), (int) bounds.getHeight(),
                       juce::RectanglePlacement::stretchToFit);
}

void ParameterPanel::PageTabButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto accent = findColour (accentColourId);
    const auto visual = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
    const bool on = getToggleState();
    const bool boldStyle = t.blockStyle == theme::BlockStyle::filled;

    juce::Colour fill = juce::Colours::transparentBlack;
    juce::Colour textColour = t.textMuted;
    if (on)
    {
        if (boldStyle)
        {
            fill = accent.isTransparent() ? t.raisedHigh : accent;
            textColour = fill.contrasting (t.background, t.text);
        }
        else
        {
            fill = t.raisedHigh;
            textColour = t.text;
        }
    }
    else if (down || highlighted)
    {
        fill = t.raisedHigh.withAlpha (0.6f);
        textColour = t.text;
    }

    const float corner = juce::jmax (1.0f, t.smallRadius - 3.0f);
    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRoundedRectangle (visual, corner);
    }

    if (on && ! boldStyle)
    {
        g.setColour (accent.isTransparent() ? t.textMuted : accent);
        const auto underline = juce::Rectangle<float> (visual.getX() + corner, visual.getBottom() - 3.0f,
                                                       juce::jmax (0.0f, visual.getWidth() - corner * 2.0f), 3.0f);
        g.fillRoundedRectangle (underline, 1.5f);
    }

    g.setColour (textColour.withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::Font (13.0f, on ? juce::Font::bold : juce::Font::plain));
    g.drawText (getButtonText(), visual, juce::Justification::centred);
}

void ParameterPanel::BypassSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto accent = findColour (accentColourId);
    const bool consoleStyle = t.id == theme::Id::console;
    const auto bounds = getLocalBounds().toFloat();

    // Prototype order is label then switch; the whole 104px component is the tap target.
    g.setColour (on ? t.text : t.textMuted);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("Ligado", juce::Rectangle<float> (bounds.getX(), bounds.getY(), 44.0f, bounds.getHeight()),
                juce::Justification::centredLeft);

    const auto track = juce::Rectangle<float> (bounds.getRight() - 66.0f, bounds.getCentreY() - 18.0f, 66.0f, 36.0f);
    const float trackRadius = consoleStyle ? 4.0f : 18.0f;
    const float knobSize = 28.0f;
    const float knobY = track.getCentreY() - knobSize * 0.5f;
    const float knobX = on ? track.getRight() - 4.0f - knobSize : track.getX() + 4.0f;

    auto trackColour = on ? (accent.isTransparent() ? t.raisedHigh : accent) : t.raisedHigh;
    if (down)
        trackColour = trackColour.brighter (0.08f);
    else if (highlighted && ! on)
        trackColour = trackColour.brighter (0.04f);

    g.setColour (trackColour);
    g.fillRoundedRectangle (track, trackRadius);
    g.setColour (on ? juce::Colours::white : juce::Colour (0xff777777));
    g.fillEllipse (knobX, knobY, knobSize, knobSize);
}

void ParameterPanel::PillButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const bool hasText = getButtonText().isNotEmpty();
    const bool danger = glyph == Glyph::trash;

    auto fill = t.raised;
    if (down)
        fill = t.raisedHigh;
    else if (highlighted)
        fill = t.raisedHigh.brighter (0.04f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, t.smallRadius);
    g.setColour (t.line);
    g.drawRoundedRectangle (bounds, t.smallRadius, 1.0f);

    const auto alpha = isEnabled() ? 1.0f : 0.4f;
    juce::Colour glyphColour = danger ? juce::Colour (0xffff6b6b) : t.text;
    glyphColour = glyphColour.withAlpha (alpha);

    if (hasText)
    {
        const float glyphSize = 18.0f;
        const float glyphX = bounds.getX() + 16.0f;
        const float glyphY = bounds.getCentreY() - glyphSize * 0.5f;
        if (glyph != Glyph::none)
            drawGlyph (g, glyph, juce::Rectangle<float> (glyphX, glyphY, glyphSize, glyphSize), glyphColour);

        g.setColour (t.text.withAlpha (alpha));
        g.setFont (juce::Font (13.0f));
        const float textX = glyph != Glyph::none ? glyphX + glyphSize + 8.0f : bounds.getX() + 16.0f;
        g.drawText (getButtonText(), juce::Rectangle<float> (textX, bounds.getY(), bounds.getRight() - textX - 14.0f, bounds.getHeight()),
                    juce::Justification::centredLeft);
    }
    else
    {
        const float glyphSize = juce::jmin (20.0f, bounds.getHeight() - 12.0f);
        const auto glyphBounds = juce::Rectangle<float> (bounds.getCentreX() - glyphSize * 0.5f,
                                                          bounds.getCentreY() - glyphSize * 0.5f,
                                                          glyphSize, glyphSize);
        drawGlyph (g, glyph, glyphBounds, glyphColour);
    }
}

void ParameterPanel::ValueFieldButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = theme::current();
    const auto bounds = getLocalBounds().toFloat();
    const auto visual = bounds.withY (bounds.getCentreY() - 18.0f).withHeight (36.0f);
    const float radius = t.id == theme::Id::console ? 3.0f : 9.0f;

    g.setColour (down ? t.raisedHigh : (highlighted ? t.raisedHigh.brighter (0.04f) : t.raised));
    g.fillRoundedRectangle (visual, radius);
    g.setColour (t.line);
    g.drawRoundedRectangle (visual, radius, 1.0f);

    g.setColour (t.text.withAlpha (isEnabled() ? 1.0f : 0.4f));
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText (getButtonText(), visual.reduced (6.0f, 0.0f), juce::Justification::centred);
}

void ParameterPanel::FineTuneSlider::paint (juce::Graphics& g)
{
    const auto& t = theme::current();
    const auto accent = findColour (accentColourId).isTransparent()
                            ? findColour (juce::Slider::rotarySliderFillColourId)
                            : findColour (accentColourId);
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const bool disabled = ! isEnabled();

    const auto rotary = getRotaryParameters();
    const float startAngle = rotary.startAngleRadians;
    const float endAngle = rotary.endAngleRadians;
    const auto range = getNormalisableRange();
    const float norm = juce::jlimit (0.0f, 1.0f, (float) range.convertTo0to1 (getValue()));
    const float valueAngle = startAngle + (endAngle - startAngle) * norm;

    const float radius = d * 0.40f; // prototype R=44 inside a 110px viewBox
    juce::Colour arcColour = accent.withAlpha (disabled ? 0.35f : 1.0f);
    juce::Colour faceColour = t.text.withAlpha (disabled ? 0.35f : 1.0f);

    auto strokeArc = [&] (float from, float to, juce::Colour colour, float width)
    {
        if (to <= from + 0.0001f)
            return;
        strokeKnobArc (g, centre, radius, from, to, colour, width);
    };

    if (t.blockStyle == theme::BlockStyle::glow)
    {
        const float width = d * (8.0f / 110.0f);
        strokeArc (startAngle, endAngle, t.raisedHigh, width);
        // The prototype uses a coloured drop-shadow; JUCE has no SVG filter here, so a soft under-stroke
        // approximates the glow without changing the arc geometry.
        strokeArc (startAngle, valueAngle, arcColour.withAlpha (arcColour.getAlpha() * 0.28f), width + d * 0.045f);
        strokeArc (startAngle, valueAngle, arcColour, width);

        const float faceRadius = d * (31.0f / 110.0f);
        const auto face = juce::Rectangle<float> (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);
        juce::ColourGradient gradient (t.raisedHigh.brighter (0.08f), centre.x, bounds.getY(),
                                       t.surface.darker (0.05f), centre.x, bounds.getBottom(), false);
        g.setGradientFill (gradient);
        g.fillEllipse (face);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (face, 1.0f);

        const auto pointerEnd = knobPoint (centre, radius * 0.62f, valueAngle);
        g.setColour (juce::Colours::white.withAlpha (disabled ? 0.35f : 1.0f));
        juce::Path pointer;
        pointer.startNewSubPath (centre);
        pointer.lineTo (pointerEnd);
        g.strokePath (pointer, juce::PathStrokeType (d * (4.0f / 110.0f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }
    else if (t.id == theme::Id::console)
    {
        const float tickInner = d * (50.0f / 110.0f);
        const float tickOuter = d * (54.0f / 110.0f);
        g.setColour (t.textDim.withAlpha (disabled ? 0.35f : 1.0f));
        juce::PathStrokeType tickStroke (d * (1.5f / 110.0f), juce::PathStrokeType::curved,
                                         juce::PathStrokeType::butt);
        for (int i = 0; i <= 10; ++i)
        {
            const float angle = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            juce::Path tick;
            tick.startNewSubPath (knobPoint (centre, tickInner, angle));
            tick.lineTo (knobPoint (centre, tickOuter, angle));
            g.strokePath (tick, tickStroke);
        }

        const float width = d * (4.0f / 110.0f);
        strokeArc (startAngle, endAngle, t.raisedHigh, width);
        strokeArc (startAngle, valueAngle, arcColour, width);

        const float faceRadius = d * (33.0f / 110.0f);
        const auto face = juce::Rectangle<float> (centre.x - faceRadius, centre.y - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);
        g.setColour (t.raised.withAlpha (disabled ? 0.5f : 1.0f));
        g.fillEllipse (face);
        g.setColour (t.lineStrong);
        g.drawEllipse (face, d * (1.5f / 110.0f));

        const auto pointerEnd = knobPoint (centre, radius * 0.70f, valueAngle);
        juce::Path pointer;
        pointer.startNewSubPath (centre);
        pointer.lineTo (pointerEnd);
        g.setColour (arcColour);
        g.strokePath (pointer, juce::PathStrokeType (d * (3.0f / 110.0f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }
    else // Bold Blocks: solid accent disc inside a raised outer ring, dark pointer.
    {
        const float outerRadius = d * (46.0f / 110.0f);
        const auto outer = juce::Rectangle<float> (centre.x - outerRadius, centre.y - outerRadius,
                                                   outerRadius * 2.0f, outerRadius * 2.0f);
        g.setColour (t.raised.withAlpha (disabled ? 0.5f : 1.0f));
        g.fillEllipse (outer);

        const float width = d * (10.0f / 110.0f);
        strokeArc (startAngle, valueAngle, arcColour, width);

        const float discRadius = d * (30.0f / 110.0f);
        const auto disc = juce::Rectangle<float> (centre.x - discRadius, centre.y - discRadius,
                                                  discRadius * 2.0f, discRadius * 2.0f);
        g.setColour (arcColour.withAlpha (disabled ? 0.45f : 1.0f));
        g.fillEllipse (disc);

        const auto pointerEnd = knobPoint (centre, radius * 0.60f, valueAngle);
        juce::Path pointer;
        pointer.startNewSubPath (centre);
        pointer.lineTo (pointerEnd);
        g.setColour (juce::Colours::black.withAlpha (disabled ? 0.35f : 1.0f));
        g.strokePath (pointer, juce::PathStrokeType (d * (5.0f / 110.0f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        (void) faceColour;
    }

    if (fineDragArmed)
    {
        g.setColour (arcColour.withAlpha (0.35f));
        g.drawEllipse (bounds.reduced (3.0f), 2.0f);
    }
}

} // namespace openguitarmultifx
