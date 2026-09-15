#include "EffectRegistry.h"

#include "Effects/CompressorProcessor.h"
#include "Effects/DelayProcessor.h"
#include "Effects/TapeDelayProcessor.h"
#include "Effects/GateProcessor.h"
#include "Effects/IRLoaderProcessor.h"
#include "Effects/DynamicCabProcessor.h"
#include "Effects/NAMProcessor.h"
#include "Effects/OverdriveProcessor.h"
#include "Effects/PositiveGroundBoosterProcessor.h"
#include "Effects/DS1StyleDistortionProcessor.h"
#include "Effects/OD1StyleOverdriveProcessor.h"
#include "Effects/ReverbProcessor.h"
#include "Effects/SpringReverbProcessor.h"
#include "Effects/HallReverbProcessor.h"
#include "Effects/PlateReverbProcessor.h"
#include "Effects/RoomReverbProcessor.h"
#include "Effects/ShimmerReverbProcessor.h"
#include "Effects/GatedReverbProcessor.h"
#include "Effects/AnalogDelayProcessor.h"
#include "Effects/DualDelayProcessor.h"
#include "Effects/MultiTapDelayProcessor.h"
#include "Effects/TremoloProcessor.h"
#include "Effects/ChorusProcessor.h"
#include "Effects/VibratoProcessor.h"
#include "Effects/FlangerProcessor.h"
#include "Effects/PhaserProcessor.h"
#include "Effects/RotaryProcessor.h"
#include "Effects/UniVibeProcessor.h"
#include "Effects/PitchModProcessor.h"
#include "Effects/PitchShiftProcessor.h"
#include "Effects/OctaverProcessor.h"
#include "Effects/HarmonizerProcessor.h"
#include "Effects/LooperProcessor.h"
#include "Effects/PingPongDelayProcessor.h"
#include "Effects/ReverseDelayProcessor.h"
#include "Effects/HoldProcessor.h"

namespace openguitarmultifx
{

void registerBuiltInEffects (EffectRegistry& registry)
{
    registry.registerType ("NoiseGate", [] { return std::make_unique<GateProcessor>(); });
    registry.registerType ("Compressor", [] { return std::make_unique<CompressorProcessor>(); });
    registry.registerType ("Overdrive", [] { return std::make_unique<OverdriveProcessor>(); });

    // Physically-modelled (Ebers-Moll transistor, not neural) -- see
    // Source/Effects/AGENTS.md's decision log and docs/circuits/
    // PositiveGroundBooster.md for the full circuit-fidelity rationale.
    registry.registerType ("PositiveGroundBooster", [] { return std::make_unique<PositiveGroundBoosterProcessor>(); });
    registry.registerType ("DS1StyleDistortion", [] { return std::make_unique<DS1StyleDistortionProcessor>(); });
    registry.registerType ("OD1StyleOverdrive", [] { return std::make_unique<OD1StyleOverdriveProcessor>(); });

    // Same wrapper class, three chain roles -- only the .nam file loaded
    // into each instance determines whether it sounds like an amp, an
    // amp+cab, or a pedal. Amp and Amp+Cab process identically (see
    // NAMProcessor.h) and only exist as separate blocks so TONE3000 search
    // can be scoped to one gear at a time, per user request.
    registry.registerType ("NeuralAmp", [] { return std::make_unique<NAMProcessor> ("Neural Amp"); });
    registry.registerType ("NeuralAmpCab", [] { return std::make_unique<NAMProcessor> ("Neural Amp + Cab"); });
    registry.registerType ("NeuralPedal", [] { return std::make_unique<NAMProcessor> ("Neural Pedal"); });

    // Same story, one class, two roles -- IRLoaderProcessor convolves with
    // a WAV impulse response either way; "Cab" vs "Reverb" is just which
    // TONE3000 gear (cab vs space) the loaded IR came from. See
    // IRLoaderProcessor.h -- this is NOT the same technology as NAM above.
    registry.registerType ("Cab", [] { return std::make_unique<IRLoaderProcessor> ("Cab"); });
    registry.registerType ("Reverb", [] { return std::make_unique<IRLoaderProcessor> ("Reverb"); });

    // Two independently-loaded cab IRs crossfaded together -- static
    // multi-mic blending (Dynamics=0) and level-dependent blending
    // (Dynamics>0) in one processor, since they're the same DSP underneath
    // (two convolutions + a crossfade), not two separate features. See
    // DynamicCabProcessor.h.
    registry.registerType ("DynamicCab", [] { return std::make_unique<DynamicCabProcessor>(); });

    // Phase 2: Delay + Reverb. Working through the icon sheet's named
    // variants one at a time -- each remaining Delay/Reverb glyph stays
    // documented-but-unbuilt in docs/icons/AGENT-icon-notes.md until it
    // gets its own processor (only glyphs the user has already approved
    // get wired up; see that doc's rule on never guessing a new one).
    registry.registerType ("DigitalDelay", [] { return std::make_unique<DelayProcessor>(); });
    registry.registerType ("TapeDelay", [] { return std::make_unique<TapeDelayProcessor>(); });
    registry.registerType ("AnalogDelay", [] { return std::make_unique<AnalogDelayProcessor>(); });
    registry.registerType ("DualDelay", [] { return std::make_unique<DualDelayProcessor>(); });
    registry.registerType ("MultiTapDelay", [] { return std::make_unique<MultiTapDelayProcessor>(); });

    // Phase 3: Modulation.
    registry.registerType ("Tremolo", [] { return std::make_unique<TremoloProcessor>(); });
    registry.registerType ("Chorus", [] { return std::make_unique<ChorusProcessor>(); });
    registry.registerType ("Vibrato", [] { return std::make_unique<VibratoProcessor>(); });
    registry.registerType ("Flanger", [] { return std::make_unique<FlangerProcessor>(); });
    registry.registerType ("Phaser", [] { return std::make_unique<PhaserProcessor>(); });
    registry.registerType ("Rotary", [] { return std::make_unique<RotaryProcessor>(); });
    registry.registerType ("UniVibe", [] { return std::make_unique<UniVibeProcessor>(); });
    registry.registerType ("PitchMod", [] { return std::make_unique<PitchModProcessor>(); });

    // Phase 4: Pitch.
    registry.registerType ("PitchShift", [] { return std::make_unique<PitchShiftProcessor>(); });
    registry.registerType ("Octaver", [] { return std::make_unique<OctaverProcessor>(); });
    registry.registerType ("Harmonizer", [] { return std::make_unique<HarmonizerProcessor>(); });

    // Phase 5 (partial): Looper. Sits in the Delay category per the
    // reference sheet, not alongside the other Phase 4 Pitch effects.
    registry.registerType ("Looper", [] { return std::make_unique<LooperProcessor>(); });
    registry.registerType ("Ambient", [] { return std::make_unique<ReverbProcessor>(); });
    registry.registerType ("Spring", [] { return std::make_unique<SpringReverbProcessor>(); });
    registry.registerType ("Hall", [] { return std::make_unique<HallReverbProcessor>(); });
    registry.registerType ("Plate", [] { return std::make_unique<PlateReverbProcessor>(); });
    registry.registerType ("Room", [] { return std::make_unique<RoomReverbProcessor>(); });
    registry.registerType ("Shimmer", [] { return std::make_unique<ShimmerReverbProcessor>(); });
    registry.registerType ("GatedReverb", [] { return std::make_unique<GatedReverbProcessor>(); });
    registry.registerType ("PingPong", [] { return std::make_unique<PingPongDelayProcessor>(); });
    registry.registerType ("ReverseDelay", [] { return std::make_unique<ReverseDelayProcessor>(); });
    registry.registerType ("Hold", [] { return std::make_unique<HoldProcessor>(); });
}

void EffectRegistry::registerType (const juce::String& key, Creator creator)
{
    // Probing with a real (immediately discarded) instance instead of
    // asking the call site to also type the display name by hand: that
    // second string would have no way to stay in sync if a processor's
    // constructor default ever changes, and preset save/load's reverse
    // lookup (keyForDisplayName) needs this to always exactly match what
    // getName() really returns at runtime.
    if (auto probe = creator())
    {
        const juce::String displayName (probe->getName());
        displayNamesByKey[key] = displayName;
        keysByDisplayName[displayName] = key;
    }

    creators[key] = std::move (creator);
}

std::unique_ptr<EffectProcessor> EffectRegistry::create (const juce::String& key) const
{
    const auto it = creators.find (key);
    return it != creators.end() ? it->second() : nullptr;
}

juce::String EffectRegistry::displayNameForKey (const juce::String& key) const
{
    const auto it = displayNamesByKey.find (key);
    return it != displayNamesByKey.end() ? it->second : key;
}

juce::String EffectRegistry::keyForDisplayName (const juce::String& displayName) const
{
    const auto it = keysByDisplayName.find (displayName);
    return it != keysByDisplayName.end() ? it->second : juce::String();
}

juce::StringArray EffectRegistry::getRegisteredNames() const
{
    juce::StringArray names;
    for (const auto& [name, creator] : creators)
        names.add (name);
    return names;
}

} // namespace openguitarmultifx
