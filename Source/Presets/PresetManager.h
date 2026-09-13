#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace openguitarmultifx
{

/**
    Plain file I/O for presets -- one XML file per preset, named
    "<preset name>.xml" under a presets directory. Deliberately knows
    nothing about EffectProcessor/EffectRegistry/the signal chain itself;
    MainComponent builds and interprets the XmlElement (see
    MainComponent::buildPresetXml()/applyPresetXml()) -- same separation
    Tone3000Manager keeps from the UI that drives it.

    Control-thread only, like every other file-I/O helper in this project.
*/
class PresetManager
{
public:
    explicit PresetManager (juce::File directoryToUse);

    /** Sorted, derived from *.xml file stems in the presets directory.
        Empty (not an error) if the directory doesn't exist yet -- no
        presets saved is a normal starting state. */
    juce::StringArray listPresetNames() const;

    /** nullptr if the file doesn't exist or fails to parse. */
    std::unique_ptr<juce::XmlElement> loadPreset (const juce::String& name) const;

    /** Creates the presets directory if needed. */
    bool savePreset (const juce::String& name, const juce::XmlElement& xml) const;

    bool deletePreset (const juce::String& name) const;

    /** The `number` attribute already stored in an existing preset's saved
        XML, or 0 if it doesn't exist yet / has none. A preset's number is
        assigned once (see nextAvailableNumber()) and kept on every
        re-save -- it's a stable identity, like a numbered slot on a
        hardware pedalboard, not a position in an alphabetical list. */
    int numberForExistingPreset (const juce::String& name) const;

    /** One past the highest `number` among all saved presets (1 if there
        are none yet) -- what a brand new preset should be numbered. */
    int nextAvailableNumber() const;

    /** The reverse of numberForExistingPreset(): which saved preset (if
        any) has this number. Empty string if none does -- e.g. a MIDI
        Program Change for a slot nothing's been saved into yet. Linear
        scan over every saved preset's XML; fine at pedalboard-preset-count
        scale (dozens, not thousands), not called from a hot path. */
    juce::String nameForNumber (int number) const;

private:
    juce::File fileFor (const juce::String& name) const;

    juce::File directory;
};

} // namespace openguitarmultifx
