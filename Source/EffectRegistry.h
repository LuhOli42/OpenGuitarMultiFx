#pragma once

#include "Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <set>
#include <memory>
#include <vector>

namespace openguitarmultifx
{

/**
    Factory mapping name -> EffectProcessor. Empty in Phase 0; concrete
    pedals (Gate, Compressor, Overdrive, ...) register themselves here in
    Phase 1. SignalGraph and AudioEngine never know about concrete effect
    types -- they only ever go through this factory.
*/
class EffectRegistry
{
public:
    using Creator = std::function<std::unique_ptr<EffectProcessor>()>;

    /** The Add-Effect browser's tabs. Category is registration metadata
        (passed to registerType()), NOT derived from display names --
        an earlier name-suffix classifier lived in MainComponent and kept
        falling behind (new effects silently landed in "Other"; bass and
        guitar amps can't be told apart by name alone). Every registered
        key maps to exactly one of these; there is no "other" bucket. */
    enum class Category
    {
        amps,
        overdriveBoost,
        distortion,
        fuzz,
        dynamics,
        modulation,
        delay,
        reverb,
        pitchFilter,
        pitch,
        neural,
        cabs,
        utility
    };

    void registerType (const juce::String& key, Category category, Creator creator,
                       const juce::String& subcategory = {});

    /** The tab this key's tile belongs in. An unregistered key reports
        utility (and jasserts) rather than crashing a UI that enumerated
        keys from somewhere else. */
    Category categoryForKey (const juce::String& key) const;

    /** The second-level grouping shown as chips inside a category (e.g.
        Amps -> Clean / Crunch / High-Gain / Bass). Same registration-metadata
        mechanism as category: passed to registerType(), never derived from
        display names. Empty = the key has no subcategory. */
    juce::String subcategoryForKey (const juce::String& key) const;

    /** The distinct subcategories registered inside a category, in the
        browser's chip order (canonical order first, any extras appended).
        The dialog only renders chips when this returns more than one entry. */
    juce::StringArray subcategoriesForCategory (Category category) const;

    /** User-facing tab label ("Amps", "Pitch/Filter", ...). */
    static juce::String categoryDisplayName (Category category);

    /** Every category, in the order the browser's rail shows them. */
    static const std::vector<Category>& categoriesInDisplayOrder();
    std::unique_ptr<EffectProcessor> create (const juce::String& key) const;
    juce::StringArray getRegisteredNames() const; // registry keys, e.g. "NeuralAmpCab" -- see displayNameForKey()

    /** What a fresh instance of this key's getName() actually returns (e.g.
        "Neural Amp + Cab" for key "NeuralAmpCab") -- built once at
        registerType() time by probing the creator, so it can never drift
        out of sync with what the processor really reports. Falls back to
        the key itself if unknown. Used for the "Add effect" menu, so it
        shows names a human wrote instead of raw registry keys. */
    juce::String displayNameForKey (const juce::String& key) const;

    /** The reverse: which registry key creates processors whose getName()
        returns exactly displayName. Needed by preset save/load, which has
        to go from a live chain block's getName() back to the key that
        recreates it. Empty if no registered type's name matches. */
    juce::String keyForDisplayName (const juce::String& displayName) const;

    /** How much the clipping pedals oversample (see docs/circuits/Oversampling.md for the measurements behind each
pedal's orders): eco = none (cheapest, DEFAULT -- the user's choice; audible aliasing on the hardest clippers),
        balanced = 2x on the DS-1/BD-2/HM-2, high = the most the measurements say is worth it. Read when a pedal is
        CREATED, so set it before building the chain; the OGMFX_QUALITY environment variable (eco / balanced / high)
        sets the initial value. */
    enum class OversamplingQuality { eco, balanced, high };
    static void setOversamplingQuality (OversamplingQuality quality) noexcept;
    static OversamplingQuality getOversamplingQuality() noexcept;

    /** The choice made in the Settings screen, kept in <app data>/OpenGuitarMultiFx/render_quality.txt. The
        environment variable wins when set (a development override); with neither, eco. */
    static void saveOversamplingQuality();

    /** True for the pedals whose construction depends on the quality tier: after it changes those (and only
        those) have to be rebuilt to pick the new tier up. */
    bool dependsOnOversamplingQuality (const juce::String& key) const;
    void markQualityDependent (const juce::String& key) { qualityDependentKeys.insert (key); }

private:
    std::map<juce::String, Creator> creators;
    std::map<juce::String, Category> categoriesByKey;
    std::map<juce::String, juce::String> subcategoriesByKey;
    std::map<juce::String, juce::String> displayNamesByKey;
    std::map<juce::String, juce::String> keysByDisplayName;
    std::set<juce::String> qualityDependentKeys;
};

/** Registers every built-in effect processor. Concrete types stay decoupled
    from SignalGraph/AudioEngine -- this is the one place that knows them all. */
void registerBuiltInEffects (EffectRegistry& registry);

} // namespace openguitarmultifx
