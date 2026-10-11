#pragma once

#include "MasterEq.h"

#include <juce_core/juce_core.h>

namespace openguitarmultifx::masterbus
{

/** The master bus state (output gain + the free-form EQ band list), remembered
    across launches in one small XML file under the same
    `userApplicationDataDirectory/OpenGuitarMultiFx` base as inputgain.txt /
    the tuning file -- app-level, NOT inside a preset: the master section
    trims the room/monitoring chain the whole rig plays through, so it must
    survive a preset switch the same way the interface's input gain does.
    See InputGainSettings.h / TuningSettings.h for the convention. */
struct State
{
    float gainDb = 0.0f;
    std::vector<MasterEqBand> bands;
};

inline juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("OpenGuitarMultiFx")
               .getChildFile ("master_bus.xml");
}

inline MasterEqBand::Type typeFromName (const juce::String& name)
{
    for (int i = 0; i < 6; ++i)
        if (name == MasterEq::displayName (MasterEq::typeForIndex (i)))
            return MasterEq::typeForIndex (i);
    return MasterEqBand::Type::peak;
}

inline const char* typeName (MasterEqBand::Type type) { return MasterEq::displayName (type); }

inline State loadState()
{
    State state;
    const auto file = settingsFile();
    if (! file.existsAsFile())
        return state;

    const auto xml = juce::XmlDocument::parse (file.loadFileAsString());
    if (xml == nullptr || ! xml->hasTagName ("MasterBus"))
        return state;

    state.gainDb = juce::jlimit (-60.0f, 12.0f, (float) xml->getDoubleAttribute ("gainDb", 0.0));

    for (auto* bandXml : xml->getChildIterator())
    {
        if (! bandXml->hasTagName ("Band"))
            continue;
        MasterEqBand band;
        band.type = typeFromName (bandXml->getStringAttribute ("type"));
        band.freqHz = juce::jlimit (10.0f, 22000.0f, (float) bandXml->getDoubleAttribute ("freq", 1000.0));
        band.gainDb = juce::jlimit (-24.0f, 24.0f, (float) bandXml->getDoubleAttribute ("gain", 0.0));
        band.q = juce::jlimit (0.1f, 24.0f, (float) bandXml->getDoubleAttribute ("q", 1.0));
        band.slopeDbOct = bandXml->getIntAttribute ("slope", 12);
        band.enabled = bandXml->getBoolAttribute ("enabled", true);
        state.bands.push_back (band);
        if (state.bands.size() >= MasterEq::maxBands)
            break;
    }
    return state;
}

inline void saveState (const State& state)
{
    juce::XmlElement xml ("MasterBus");
    xml.setAttribute ("gainDb", juce::String (state.gainDb, 2));

    for (const auto& band : state.bands)
    {
        auto* bandXml = xml.createNewChildElement ("Band");
        bandXml->setAttribute ("type", typeName (band.type));
        bandXml->setAttribute ("freq", juce::String (band.freqHz, 2));
        bandXml->setAttribute ("gain", juce::String (band.gainDb, 2));
        bandXml->setAttribute ("q", juce::String (band.q, 3));
        bandXml->setAttribute ("slope", band.slopeDbOct);
        bandXml->setAttribute ("enabled", band.enabled);
    }

    const auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (xml.toString());
}

} // namespace openguitarmultifx::masterbus
