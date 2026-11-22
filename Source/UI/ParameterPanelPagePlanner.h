#pragma once

#include "../Effects/EffectProcessor.h"
#include "SelectorSwitch.h"
#include "ScrollListSelector.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace openguitarmultifx::parameterPanelLayout
{

constexpr int pageWidthBudget = 1244;
constexpr int sectionGap = 16;
constexpr int knobWidth = 120;
constexpr int switchLabelHeight = 18;
constexpr int switchLabelGap = 4;
constexpr int switchStackGap = 10;
constexpr int faderCellWidth = 52;
constexpr int faderPitch = 64;
constexpr int faderExtraGap = 24;
constexpr int graphicEqPanelPadding = 14;

inline bool containsWholeToken (const juce::String& value, const juce::String& token)
{
    int tokenStart = 0;
    for (int i = 0; i <= value.length(); ++i)
    {
        if (i < value.length() && juce::CharacterFunctions::isLetterOrDigit (value[i]))
            continue;

        if (value.substring (tokenStart, i) == token)
            return true;
        tokenStart = i + 1;
    }
    return false;
}

inline bool isSelector (const EffectProcessor& processor, const juce::AudioParameterFloat& param)
{
    const auto range = param.getNormalisableRange();
    return range.interval >= 1.0f
        && ((range.end - range.start) / range.interval <= 8.0f
            || processor.prefersScrollingSelector (param))
        && ! processor.prefersSteppedKnob (param);
}

/** The positions of a stepped parameter its processor offers right now (isSelectorOptionAvailable). */
inline std::vector<int> availableSteps (const EffectProcessor& processor, const juce::AudioParameterFloat& param)
{
    std::vector<int> steps;
    const auto range = param.getNormalisableRange();
    const int count = (int) std::lround ((range.end - range.start) / range.interval) + 1;
    for (int step = 0; step < count; ++step)
        if (processor.isSelectorOptionAvailable (param, step))
            steps.push_back (step);
    return steps;
}

inline juce::StringArray optionsFor (juce::AudioParameterFloat& param, const std::vector<int>& steps)
{
    juce::StringArray options;
    const auto range = param.getNormalisableRange();
    for (int step : steps)
    {
        const float value = range.start + (float) step * range.interval;
        options.add (static_cast<juce::AudioProcessorParameter&> (param).getText (param.convertTo0to1 (value), 32));
    }
    return options;
}

/** Per shown step, the tab mask the processor gives it (getSelectorOptionGroups); empty when it gives none. */
inline std::vector<int> groupsFor (const EffectProcessor& processor, const juce::AudioParameterFloat& param,
                                   const std::vector<int>& steps)
{
    std::vector<int> groups;
    bool any = false;
    for (int step : steps)
    {
        groups.push_back (processor.getSelectorOptionGroups (param, step));
        any = any || groups.back() != 0;
    }
    return any ? groups : std::vector<int> {};
}

inline juce::StringArray optionsFor (juce::AudioParameterFloat& param)
{
    juce::StringArray options;
    const auto range = param.getNormalisableRange();
    const int steps = (int) std::lround ((range.end - range.start) / range.interval) + 1;
    for (int step = 0; step < steps; ++step)
    {
        const float value = range.start + (float) step * range.interval;
        options.add (static_cast<juce::AudioProcessorParameter&> (param).getText (param.convertTo0to1 (value), 32));
    }
    return options;
}

inline int selectorWidth (EffectProcessor& processor, juce::AudioParameterFloat& param)
{
    const auto options = optionsFor (param, availableSteps (processor, param));
    if (processor.prefersScrollingSelector (param))
    {
        ScrollListSelector list;
        list.setOptions (options);
        list.setGroups (groupsFor (processor, param, availableSteps (processor, param)));
        return list.getPreferredWidth();
    }
    SelectorSwitch measurement;
    measurement.setOptions (options);
    return measurement.getPreferredWidth();
}

inline int parameterLabelWidth (const juce::AudioParameterFloat& param)
{
    auto text = param.getName (64);
    if (theme::current().uppercaseLabels)
        text = text.toUpperCase();
    const juce::Font labelFont (juce::FontOptions (12.0f, juce::Font::bold));
    return (int) std::ceil (juce::GlyphArrangement::getStringWidth (labelFont, text)) + 2;
}

inline bool isGraphicEqSwitch (const juce::AudioParameterFloat& param, bool selector)
{
    return selector && containsWholeToken (param.getName (64).toLowerCase(), "eq");
}

inline bool isGraphicEqBand (const juce::AudioParameterFloat& param)
{
    return param.getName (64).endsWithIgnoreCase ("Hz");
}

inline bool isGraphicEqControl (const EffectProcessor& processor, juce::AudioParameterFloat& param)
{
    return isGraphicEqBand (param) || isGraphicEqSwitch (param, isSelector (processor, param));
}

inline bool isToneStackKnob (const juce::String& name)
{
    return name.contains ("bass") || name.contains ("mid") || name.contains ("middle")
        || name.contains ("treble") || name.contains ("presence");
}

inline bool isPriorityKnob (const juce::String& name)
{
    return name.contains ("gain") || name.contains ("drive") || name.contains ("volume")
        || name.contains ("master");
}

inline bool isChannelSpecificCoreKnob (const juce::String& name)
{
    return name.startsWith ("lead ");
}

inline bool isCoreKnob (const juce::String& name)
{
    return isToneStackKnob (name) || isPriorityKnob (name)
        || name.contains ("resonance") || name.contains ("depth")
        || name.contains ("contour") || name.contains ("tone");
}

inline int lineWidth (const std::vector<juce::AudioParameterFloat*>& controls,
                      EffectProcessor& processor)
{
    struct SwitchColumn
    {
        std::vector<juce::AudioParameterFloat*> controls;
        int width = 0;
        bool canStackTwoOption = false;
    };

    std::vector<juce::AudioParameterFloat*> knobs, faders, switches;
    juce::AudioParameterFloat* eqSwitch = nullptr;
    bool hasFaders = false;

    for (auto* param : controls)
    {
        if (param == nullptr)
            continue;
        const bool selector = isSelector (processor, *param);
        const bool fader = processor.prefersFader (*param);
        if (fader)
        {
            faders.push_back (param);
            hasFaders = true;
        }
        else if (selector)
        {
            if (eqSwitch == nullptr && isGraphicEqSwitch (*param, true))
                eqSwitch = param;
            else
                switches.push_back (param);
        }
        else
            knobs.push_back (param);
    }

    const bool hasKnobs = ! knobs.empty();
    const bool useGraphicEqPanel = hasFaders && (hasKnobs || eqSwitch != nullptr);
    if (useGraphicEqPanel && eqSwitch != nullptr)
        switches.erase (std::remove (switches.begin(), switches.end(), eqSwitch), switches.end());

    std::vector<SwitchColumn> columns;
    for (auto* param : switches)
    {
        const auto options = optionsFor (*param);
        const bool twoOption = options.size() == 2;
        const int width = juce::jmax (selectorWidth (processor, *param), parameterLabelWidth (*param));
        if (twoOption && ! columns.empty() && columns.back().canStackTwoOption
            && columns.back().controls.size() == 1)
        {
            columns.back().controls.push_back (param);
            columns.back().width = juce::jmax (columns.back().width, width);
            columns.back().canStackTwoOption = false;
        }
        else
        {
            columns.push_back ({ { param }, width, twoOption });
        }
    }

    const int leftColumnCount = hasKnobs ? (int) (columns.size() + 1) / 2 : (int) columns.size();
    auto widthOfColumns = [&] (int start, int end)
    {
        int width = 0;
        for (int i = start; i < end; ++i)
            width += columns[(size_t) i].width;
        if (end > start + 1)
            width += sectionGap * (end - start - 1);
        return width;
    };

    int faderStripWidth = 0;
    bool haveFader = false;
    for (auto* param : faders)
    {
        if (! isGraphicEqBand (*param))
            faderStripWidth += faderExtraGap;
        if (haveFader)
            faderStripWidth += faderPitch - faderCellWidth;
        faderStripWidth += faderCellWidth;
        haveFader = true;
    }

    int faderGroupWidth = faderStripWidth;
    if (useGraphicEqPanel)
    {
        const juce::Font titleFont (juce::FontOptions (11.0f, juce::Font::bold));
        const int titleWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, "GRAPHIC EQ"));
        const int unitsWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (titleFont, "HZ"));
        int headerWidth = titleWidth + 8 + unitsWidth;
        if (eqSwitch != nullptr)
            headerWidth += 8 + selectorWidth (processor, *eqSwitch) + 16;
        faderGroupWidth = juce::jmax (faderStripWidth, headerWidth) + graphicEqPanelPadding * 2;
    }

    std::vector<int> sections;
    const int leftWidth = widthOfColumns (0, leftColumnCount);
    const int rightWidth = widthOfColumns (leftColumnCount, (int) columns.size());
    if (leftWidth > 0)
        sections.push_back (leftWidth);
    if (! knobs.empty())
        sections.push_back ((int) knobs.size() * knobWidth + ((int) knobs.size() - 1) * sectionGap);
    if (rightWidth > 0)
        sections.push_back (rightWidth);
    if (faderGroupWidth > 0)
        sections.push_back (faderGroupWidth);

    int width = 0;
    for (int section : sections)
        width += section;
    if (sections.size() > 1)
        width += sectionGap * ((int) sections.size() - 1);
    return width;
}

inline bool isSelectorOnlyPage (const std::vector<juce::AudioParameterFloat*>& page,
                                const EffectProcessor& processor)
{
    return ! page.empty()
        && std::all_of (page.begin(), page.end(), [&] (auto* param)
        {
            return param != nullptr && isSelector (processor, *param);
        });
}

inline std::vector<std::vector<juce::AudioParameterFloat*>>
splitSourcePage (EffectProcessor& processor, const std::vector<juce::AudioParameterFloat*>& source,
                 int widthBudget = pageWidthBudget)
{
    std::vector<juce::AudioParameterFloat*> active;
    for (auto* param : source)
        if (param != nullptr && processor.isParameterActive (*param))
            active.push_back (param);

    if (active.size() <= 8)
        return { source };

    std::vector<juce::AudioParameterFloat*> core, switches, other, graphicEq;
    for (auto* param : active)
    {
        const bool selector = isSelector (processor, *param);
        if (isGraphicEqControl (processor, *param))
            graphicEq.push_back (param);
        else if (selector)
            switches.push_back (param);
        else if (! processor.prefersFader (*param) && isCoreKnob (param->getName (64).toLowerCase()))
            core.push_back (param);
        else
            other.push_back (param);
    }

    std::vector<juce::AudioParameterFloat*> priorityOrder;
    for (auto* param : core)
        if (! isChannelSpecificCoreKnob (param->getName (64).toLowerCase())
            && isToneStackKnob (param->getName (64).toLowerCase()))
            priorityOrder.push_back (param);
    for (auto* param : core)
        if (! isChannelSpecificCoreKnob (param->getName (64).toLowerCase())
            && ! isToneStackKnob (param->getName (64).toLowerCase())
            && isPriorityKnob (param->getName (64).toLowerCase()))
            priorityOrder.push_back (param);
    for (auto* param : core)
    {
        const auto name = param->getName (64).toLowerCase();
        if (! isChannelSpecificCoreKnob (name) && ! isToneStackKnob (name) && ! isPriorityKnob (name))
            priorityOrder.push_back (param);
    }

    std::vector<juce::AudioParameterFloat*> firstCore;
    for (auto* param : priorityOrder)
    {
        auto candidate = firstCore;
        candidate.push_back (param);
        if (firstCore.empty() || lineWidth (candidate, processor) <= widthBudget)
            firstCore.push_back (param);
    }

    auto addSwitches = [&] (const std::vector<juce::AudioParameterFloat*>& coreControls)
    {
        auto page = coreControls;
        for (auto* param : switches)
        {
            auto candidate = page;
            candidate.push_back (param);
            if (page.empty() || lineWidth (candidate, processor) <= widthBudget)
                page.push_back (param);
            else
                break;
        }
        return page;
    };

    auto firstPage = addSwitches (firstCore);

    std::vector<juce::AudioParameterFloat*> overflow;
    for (auto* param : active)
        if (std::find (firstPage.begin(), firstPage.end(), param) == firstPage.end()
            && std::find (graphicEq.begin(), graphicEq.end(), param) == graphicEq.end())
            overflow.push_back (param);

    std::vector<std::vector<juce::AudioParameterFloat*>> pages;
    if (! firstPage.empty())
        pages.push_back (firstPage);

    std::vector<juce::AudioParameterFloat*> currentPage;
    for (auto* param : overflow)
    {
        auto candidate = currentPage;
        candidate.push_back (param);
        if (! currentPage.empty() && lineWidth (candidate, processor) > widthBudget)
        {
            pages.push_back (std::move (currentPage));
            currentPage.clear();
        }
        currentPage.push_back (param);
    }
    if (! currentPage.empty())
        pages.push_back (std::move (currentPage));

    if (! graphicEq.empty())
    {
        if (! pages.empty())
        {
            auto candidate = pages.back();
            candidate.insert (candidate.end(), graphicEq.begin(), graphicEq.end());
            if (lineWidth (candidate, processor) <= widthBudget)
                pages.back() = std::move (candidate);
            else
                pages.push_back (std::move (graphicEq));
        }
        else
            pages.push_back (std::move (graphicEq));
    }

    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
    {
        auto& page = pages[pageIndex];
        if (! isSelectorOnlyPage (page, processor))
            continue;

        std::vector<size_t> donorPages;
        if (pageIndex > 0)
            donorPages.push_back (pageIndex - 1);
        if (pageIndex + 1 < pages.size())
            donorPages.push_back (pageIndex + 1);

        bool fixed = false;
        for (auto donorIndex : donorPages)
        {
            auto& donor = pages[donorIndex];
            std::vector<juce::AudioParameterFloat*> candidates;
            for (auto* param : donor)
                if (! isSelector (processor, *param) && ! processor.prefersFader (*param))
                    candidates.push_back (param);
            std::stable_sort (candidates.begin(), candidates.end(), [] (auto* a, auto* b)
            {
                const auto first = a->getName (64).toLowerCase();
                const auto second = b->getName (64).toLowerCase();
                const auto rank = [] (const juce::String& name)
                {
                    if (name.contains ("master")) return 0;
                    if (name.contains ("presence")) return 1;
                    return 2;
                };
                return rank (first) < rank (second);
            });

            for (auto* knob : candidates)
            {
                auto expanded = page;
                expanded.push_back (knob);
                auto reduced = donor;
                reduced.erase (std::remove (reduced.begin(), reduced.end(), knob), reduced.end());
                if (! isSelectorOnlyPage (reduced, processor)
                    && lineWidth (expanded, processor) <= widthBudget)
                {
                    page = std::move (expanded);
                    donor = std::move (reduced);
                    fixed = true;
                    break;
                }

                for (auto* switchParam : page)
                {
                    auto swappedPage = page;
                    auto swappedDonor = donor;
                    swappedPage.erase (std::remove (swappedPage.begin(), swappedPage.end(), switchParam),
                                       swappedPage.end());
                    swappedPage.push_back (knob);
                    swappedDonor.erase (std::remove (swappedDonor.begin(), swappedDonor.end(), knob),
                                        swappedDonor.end());
                    swappedDonor.push_back (switchParam);
                    if (! isSelectorOnlyPage (swappedDonor, processor)
                        && lineWidth (swappedPage, processor) <= widthBudget
                        && lineWidth (swappedDonor, processor) <= widthBudget)
                    {
                        page = std::move (swappedPage);
                        donor = std::move (swappedDonor);
                        fixed = true;
                        break;
                    }
                }
                if (fixed)
                    break;
            }
            if (fixed)
                break;
        }
    }

    pages.erase (std::remove_if (pages.begin(), pages.end(), [] (const auto& page) { return page.empty(); }),
                 pages.end());

    const auto declaredIndex = [&] (juce::AudioParameterFloat* param)
    {
        return std::find (active.begin(), active.end(), param) - active.begin();
    };
    for (auto& page : pages)
    {
        std::stable_sort (page.begin(), page.end(), [&] (auto* first, auto* second)
        {
            return declaredIndex (first) < declaredIndex (second);
        });

        const auto presence = std::find_if (page.begin(), page.end(), [] (auto* param)
        {
            return param->getName (64).equalsIgnoreCase ("Presence");
        });
        const auto master = std::find_if (page.begin(), page.end(), [] (auto* param)
        {
            return param->getName (64).equalsIgnoreCase ("Master");
        });
        if (presence != page.end() && master != page.end() && presence > master)
        {
            auto* presenceParam = *presence;
            page.erase (presence);
            const auto reorderedMaster = std::find_if (page.begin(), page.end(), [] (auto* param)
            {
                return param->getName (64).equalsIgnoreCase ("Master");
            });
            page.insert (reorderedMaster, presenceParam);
        }
    }

    return pages;
}

} // namespace openguitarmultifx::parameterPanelLayout
