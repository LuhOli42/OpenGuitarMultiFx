#pragma once

#include <juce_core/juce_core.h>

namespace openguitarmultifx::tone3000routing
{

/**
    Maps a TONE3000 Tone's (gear, format) pair -- the exact API enum
    strings, verified against their docs -- to where a downloaded file
    should land and which EffectRegistry block type it belongs in.

    format is the load-bearing field: "nam" only ever means
    NeuralAmpModelerCore can load it (NAMProcessor); "ir" only ever means
    it's a plain impulse response for convolution (IRLoaderProcessor).
    "aida-x"/"aa-snapshot"/"proteus" are other tools' model formats this
    app's engine cannot load at all -- `supported` is false for those, and
    the UI should refuse the download rather than hand this app's NAM
    engine a file it will fail to parse.
*/
struct GearRoute
{
    bool supported = false;
    juce::String subfolder;     // where to save it under the models directory
    juce::String registryRole;  // EffectRegistry key to auto-create/target, empty if none
    juce::String fileExtension; // includes the leading dot
};

inline GearRoute routeFor (const juce::String& gear, const juce::String& format)
{
    if (format == "nam")
    {
        if (gear == "pedal")
            return { true, "pedals", "NeuralPedal", ".nam" };

        // Split from each other on request: an amp-only capture (pairs with
        // a separate Cab block) and an all-in-one amp+cab capture are
        // different things to go looking for, even though NAMProcessor
        // loads either through the exact same code path (see NAMProcessor.h).
        if (gear == "amp")
            return { true, "amps", "NeuralAmp", ".nam" };

        if (gear == "amp-cab" || gear == "full-rig")
            return { true, "amp-cab", "NeuralAmpCab", ".nam" };

        // outboard / experimental nam captures -- loadable, but no chain
        // role fits automatically; the user places it themselves.
        return { true, "other-nam", {}, ".nam" };
    }

    if (format == "ir")
    {
        if (gear == "cab")
            return { true, "cabs", "Cab", ".wav" };

        if (gear == "space")
            return { true, "reverbs", "Reverb", ".wav" };

        return { true, "other-ir", {}, ".wav" };
    }

    return {}; // aida-x / aa-snapshot / proteus -- not a format this engine reads
}

/** Reverse of routeFor()'s subfolder choice, keyed by the *processor's*
    getName() (not the registry key -- see MainComponent's matchesRegistryRole
    for why the two need to be looked up differently). Lets the "Load
    file..." picker default straight into the right category folder instead
    of wherever it last was, so downloading into a category and then adding
    a block of that type doesn't require re-navigating to find the file. */
inline juce::String subfolderForProcessorName (const juce::String& processorName)
{
    if (processorName == "Neural Amp")         return "amps";
    if (processorName == "Neural Amp + Cab")   return "amp-cab";
    if (processorName == "Neural Pedal")       return "pedals";
    if (processorName == "Cab")                return "cabs";
    if (processorName == "Dynamic Cab")        return "cabs"; // same category as Cab -- just two IR slots
    if (processorName == "Reverb")             return "reverbs";
    return {};
}

/** File-picker wildcard for a processor's expected file type. */
inline juce::String fileWildcardForProcessorName (const juce::String& processorName)
{
    if (processorName == "Cab" || processorName == "Dynamic Cab" || processorName == "Reverb")
        return "*.wav";
    if (processorName == "Neural Amp" || processorName == "Neural Amp + Cab" || processorName == "Neural Pedal")
        return "*.nam";
    return "*.*";
}

/** The forward direction of subfolderForProcessorName(): which TONE3000
    `gears` query value a block's contextual search should use, so
    searching from inside a Neural Amp block only ever shows amp-only
    captures, and Neural Amp + Cab only ever shows amp+cab captures --
    deliberately two separate searches (on request), even though
    NAMProcessor loads either gear's .nam file through the exact same code
    path (see NAMProcessor.h). */
inline juce::String gearFilterForProcessorName (const juce::String& processorName)
{
    if (processorName == "Neural Amp")         return "amp";
    if (processorName == "Neural Amp + Cab")   return "amp-cab";
    if (processorName == "Neural Pedal")       return "pedal";
    if (processorName == "Cab")                return "cab";
    if (processorName == "Dynamic Cab")        return "cab";
    if (processorName == "Reverb")             return "space";
    return {};
}

} // namespace openguitarmultifx::tone3000routing
