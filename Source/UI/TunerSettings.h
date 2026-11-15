#pragma once

#include <juce_core/juce_core.h>

namespace openguitarmultifx::tunersettings
{

/**
    Global tuner settings (not per preset -- like scene_midi.xml, they describe the rig on the floor):
    - `cc`: the footswitch CC that opens/closes the tuner (-1 = none; learned from the tuner screen).
    - `muteWhileOpen`: silence the output while the tuner is up, so the room does not hear the tuning.
*/
struct Settings
{
    int cc = -1;
    bool muteWhileOpen = false;
};

inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("tuner_settings.xml");
}

inline void load (Settings& settings, const juce::File& file = settingsFile())
{
    if (! file.existsAsFile())
        return;

    if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("Tuner"))
    {
        const int cc = xml->getIntAttribute ("cc", -1);
        settings.cc = cc >= 0 && cc <= 127 ? cc : -1;
        settings.muteWhileOpen = xml->getBoolAttribute ("muteWhileOpen", false);
    }
}

inline void save (const Settings& settings, const juce::File& file = settingsFile())
{
    juce::XmlElement xml ("Tuner");
    xml.setAttribute ("cc", settings.cc);
    xml.setAttribute ("muteWhileOpen", settings.muteWhileOpen);
    file.getParentDirectory().createDirectory();
    xml.writeTo (file);
}

} // namespace openguitarmultifx::tunersettings
