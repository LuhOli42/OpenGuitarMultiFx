#pragma once

#include <juce_core/juce_core.h>

namespace openguitarmultifx::inputgain
{

/** The input sensitivity (dB) the user last chose, remembered across launches -- one flat file, not a new settings
    subsystem, because there is exactly one value to remember: it describes the interface plugged in (its preamp gain, whether the guitar is
    on a Hi-Z input), not a sound you dial up, so it does not belong inside a preset. See AudioEngine::setInputGainDb()
    for why it exists at all. */
inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("inputgain.txt");
}

inline float loadSavedGainDb()
{
    const auto file = settingsFile();
    if (! file.existsAsFile())
        return 0.0f;
    return juce::jlimit (-24.0f, 24.0f, file.loadFileAsString().trim().getFloatValue());
}

inline void saveGainDb (float db)
{
    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::String (db, 1));
}

} // namespace openguitarmultifx::inputgain
