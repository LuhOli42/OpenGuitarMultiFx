#include "TuningSettings.h"

namespace openguitarmultifx::tunings
{

namespace
{
    juce::File settingsFile()
    {
        // Same userApplicationDataDirectory/OpenGuitarMultiFx base as
        // MainComponent::getModelsDirectory()/getPresetsDirectory() -- one
        // flat file here rather than a subfolder, since there's only ever
        // this one thing to remember.
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("OpenGuitarMultiFx")
                   .getChildFile ("tuning.txt");
    }
}

TuningProfile loadSavedTuning()
{
    const auto file = settingsFile();
    if (! file.existsAsFile())
        return standardGuitar6(); // first launch, or the file was removed -- a sensible default

    const auto noteNames = juce::StringArray::fromTokens (file.loadFileAsString(), ",", "");

    TuningProfile profile;
    for (auto& name : noteNames)
    {
        const auto trimmed = name.trim();
        const auto hz = noteNameToFrequency (trimmed);
        if (hz > 0.0f)
            profile.push_back ({ trimmed, hz });
    }

    // A corrupted/empty/unparseable file is no better than a missing one.
    return profile.empty() ? standardGuitar6() : profile;
}

void saveTuning (const TuningProfile& tuning)
{
    juce::StringArray noteNames;
    for (auto& tuned : tuning)
        noteNames.add (tuned.noteName);

    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (noteNames.joinIntoString (","));
}

} // namespace openguitarmultifx::tunings
