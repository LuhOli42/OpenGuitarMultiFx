#pragma once

#include "Effects/EffectProcessor.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>

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

    void registerType (const juce::String& key, Creator creator);
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
        pedal's orders): eco = none (cheapest, audible aliasing on the hardest clippers), balanced = the default,
        high = the most the measurements say is worth it. Read when a pedal is CREATED, so set it before building the
        chain; the OGMFX_QUALITY environment variable (eco / balanced / high) sets the initial value. */
    enum class OversamplingQuality { eco, balanced, high };
    static void setOversamplingQuality (OversamplingQuality quality) noexcept;
    static OversamplingQuality getOversamplingQuality() noexcept;

private:
    std::map<juce::String, Creator> creators;
    std::map<juce::String, juce::String> displayNamesByKey;
    std::map<juce::String, juce::String> keysByDisplayName;
};

/** Registers every built-in effect processor. Concrete types stay decoupled
    from SignalGraph/AudioEngine -- this is the one place that knows them all. */
void registerBuiltInEffects (EffectRegistry& registry);

} // namespace openguitarmultifx
