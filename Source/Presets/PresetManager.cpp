#include "PresetManager.h"

namespace openguitarmultifx
{

PresetManager::PresetManager (juce::File directoryToUse)
    : directory (std::move (directoryToUse))
{
}

juce::File PresetManager::fileFor (const juce::String& name) const
{
    return directory.getChildFile (juce::File::createLegalFileName (name) + ".xml");
}

juce::StringArray PresetManager::listPresetNames() const
{
    juce::StringArray names;

    if (! directory.isDirectory())
        return names;

    for (auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (file.getFileNameWithoutExtension());

    names.sort (true);
    return names;
}

std::unique_ptr<juce::XmlElement> PresetManager::loadPreset (const juce::String& name) const
{
    return juce::XmlDocument::parse (fileFor (name));
}

bool PresetManager::savePreset (const juce::String& name, const juce::XmlElement& xml) const
{
    directory.createDirectory();
    return xml.writeTo (fileFor (name));
}

bool PresetManager::deletePreset (const juce::String& name) const
{
    return fileFor (name).deleteFile();
}

int PresetManager::numberForExistingPreset (const juce::String& name) const
{
    if (auto xml = loadPreset (name))
        return xml->getIntAttribute ("number", 0);
    return 0;
}

int PresetManager::nextAvailableNumber() const
{
    int highest = 0;
    for (auto& name : listPresetNames())
        highest = juce::jmax (highest, numberForExistingPreset (name));
    return highest + 1;
}

juce::String PresetManager::nameForNumber (int number) const
{
    for (auto& name : listPresetNames())
        if (numberForExistingPreset (name) == number)
            return name;
    return {};
}

} // namespace openguitarmultifx
